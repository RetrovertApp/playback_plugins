///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ZXTune Playback Plugin
//
// Implements RVPlaybackPlugin interface for ZX Spectrum and other chiptune formats
// (PT3, PT2, AY, STC, STP, VTX, ASC, PSG, etc.)
// Based on ZXTune r5100 by Vitamin/CAIG.
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <retrovert/io.h>
#include <retrovert/log.h>
#include <retrovert/metadata.h>
#include <retrovert/playback.h>
#include <retrovert/service.h>

#include "binary/container_factories.h"
#include "core/service.h"
#include "devices/aym/src/psg.h"
#include "error.h"
#include "module/attributes.h"
#include "module/holder.h"
#include "module/information.h"
#include "module/players/pipeline.h"
#include "module/renderer.h"
#include "module/track_information.h"
#include "parameters/container.h"
#include "sound/chunk.h"
#include "time/duration.h"

#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <string.h>
#define strcasecmp _stricmp
#else
#include <strings.h>
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define FREQ 48000

static const RVIo* g_io_api = nullptr;
const RVLog* g_rv_log = nullptr;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Module detection callback for finding modules in containers

class ModulesDetector : public Module::DetectCallback {
  public:
    Parameters::Container::Ptr CreateInitialProperties(const StringView /*subpath*/) const override {
        return Parameters::Container::Create();
    }

    void ProcessModule(const ZXTune::DataLocation& location, const ZXTune::Plugin& /*decoder*/,
                       Module::Holder::Ptr holder) override {
        modules_.push_back(holder);
    }

    Log::ProgressCallback* GetProgress() const override {
        return nullptr;
    }

    const std::vector<Module::Holder::Ptr>& GetModules() const {
        return modules_;
    }

    void Clear() {
        modules_.clear();
    }

  private:
    std::vector<Module::Holder::Ptr> modules_;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct ZXTuneData {
    std::shared_ptr<const ZXTune::Service> service;
    Module::Holder::Ptr holder;
    Module::Renderer::Ptr renderer;
    Module::Information::Ptr info;
    std::shared_ptr<Parameters::Container> sound_params;
    ModulesDetector detector;
    Sound::Chunk chunk;
    size_t chunk_offset;
    std::vector<Module::Holder::Ptr> subsong_holders;
    int current_subsong;
    AymScopeState scope_state;
    bool scope_enabled;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Helper to load file and detect modules - avoids duplication between open and metadata

static bool load_and_detect_modules(const char* url, const ZXTune::Service& service, ModulesDetector& detector,
                                    const RVIo* io_api) {
    RVIoReadUrlResult read_res = RVIo_read_url_to_memory(io_api, url);
    if (read_res.data == nullptr) {
        rv_error("Failed to load %s to memory", url);
        return false;
    }

    auto dump = std::make_unique<Binary::Dump>(read_res.data_size);
    std::memcpy(dump->data(), read_res.data, read_res.data_size);
    RVIo_free_url_to_memory(io_api, read_res.data);

    auto container = Binary::CreateContainer(std::move(dump));
    if (!container) {
        rv_error("Failed to create binary container for %s", url);
        return false;
    }

    detector.Clear();

    try {
        service.DetectModules(container, detector);
    } catch (const Error& e) {
        rv_error("ZXTune failed to detect modules in %s: %s", url, e.ToString().c_str());
        return false;
    }

    if (detector.GetModules().empty()) {
        rv_error("ZXTune found no modules in %s", url);
        return false;
    }

    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static const char* zxtune_supported_extensions(void) {
    // ZX Spectrum AY/YM formats
    return "pt3,pt2,pt1,stc,stp,sqt,psc,asc,psm,vtx,ym,psg,ay,ayc,"
           // Other AY formats
           "ts,ftc,gtr,dmm,str,dst,fdi,chi,"
           // TurboSound FM
           "tfc,tfd,tf0,"
           // SAA1099
           "cop,"
           // Digital (note: ahx handled by hively, sid by sidplayfp)
           "pdt,"
           // AY Emul (ZXAYEMUL/EMUL magic detected by probe)
           "emul,"
           // Containers
           "scl,trd,hrip";
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void* zxtune_create(const RVService* service_api) {
    if (service_api == nullptr) {
        return nullptr;
    }

    auto* data = new (std::nothrow) ZXTuneData();
    if (data == nullptr) {
        return nullptr;
    }

    data->service = ZXTune::Service::Create(Parameters::Container::Create());
    data->sound_params = Parameters::Container::Create();
    data->chunk_offset = 0;
    data->current_subsong = 0;
    data->scope_enabled = false;

    g_io_api = RVService_get_io(service_api, RV_IO_API_VERSION);

    return data;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static int zxtune_destroy(void* user_data) {
    auto* data = static_cast<ZXTuneData*>(user_data);
    delete data;
    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static int zxtune_open(void* user_data, const char* url, uint32_t subsong, const RVService* /*service_api*/) {
    auto* data = static_cast<ZXTuneData*>(user_data);

    // Clear previous state
    data->subsong_holders.clear();
    data->holder.reset();
    data->renderer.reset();
    data->info.reset();
    data->chunk = Sound::Chunk();
    data->chunk_offset = 0;

    if (!load_and_detect_modules(url, *data->service, data->detector, g_io_api)) {
        return -1;
    }

    // Store all modules for subsong support
    const auto& modules = data->detector.GetModules();
    data->subsong_holders = modules;

    // Select subsong (0-based index, 0 = first/default)
    size_t subsong_index = (subsong > 0 && subsong <= modules.size()) ? (subsong - 1) : 0;
    data->current_subsong = static_cast<int>(subsong_index);
    data->holder = modules[subsong_index];

    // Create renderer with 48kHz sample rate
    try {
        data->renderer = CreatePipelinedRenderer(*data->holder, data->sound_params);
    } catch (const Error& e) {
        rv_error("ZXTune failed to create renderer for %s: %s", url, e.ToString().c_str());
        return -1;
    }

    data->info = data->holder->GetModuleInformation();

    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void zxtune_close(void* user_data) {
    auto* data = static_cast<ZXTuneData*>(user_data);

    // Disable scope capture before releasing renderer
    if (data->scope_enabled) {
        g_aym_scope_state = nullptr;
        data->scope_enabled = false;
    }

    data->holder.reset();
    data->renderer.reset();
    data->info.reset();
    data->chunk = Sound::Chunk();
    data->chunk_offset = 0;
    data->subsong_holders.clear();
    data->detector.Clear();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static RVProbeResult zxtune_probe_can_play(uint8_t* probe_data, uint64_t data_size, const char* url,
                                           uint64_t /*total_size*/) {
    // Try to detect module format by examining data
    // ZXTune handles many formats, so we'll do basic header checks

    if (data_size < 4) {
        return RVProbeResult_Unsupported;
    }

    // Check for ZXAYEMUL signature (AY files)
    if (data_size >= 8 && probe_data[0] == 'Z' && probe_data[1] == 'X' && probe_data[2] == 'A'
        && probe_data[3] == 'Y') {
        return RVProbeResult_Supported;
    }

    // Check for EMUL header (AY files alternate)
    if (data_size >= 8 && probe_data[0] == 'E' && probe_data[1] == 'M' && probe_data[2] == 'U'
        && probe_data[3] == 'L') {
        return RVProbeResult_Supported;
    }

    // Check for YM file signatures
    if (data_size >= 4) {
        // YM2!, YM3!, YM3b, YM4!, YM5!, YM6!
        if (probe_data[0] == 'Y' && probe_data[1] == 'M' && (probe_data[2] >= '2' && probe_data[2] <= '6')) {
            return RVProbeResult_Supported;
        }
    }

    // Check for VTX signature
    if (data_size >= 4
        && ((probe_data[0] == 'a' && probe_data[1] == 'y') || (probe_data[0] == 'y' && probe_data[1] == 'm'))) {
        return RVProbeResult_Supported;
    }

    // Check for PSG signature
    if (data_size >= 4 && probe_data[0] == 'P' && probe_data[1] == 'S' && probe_data[2] == 'G') {
        return RVProbeResult_Supported;
    }

    // For other formats, rely on extension
    if (url != nullptr) {
        const char* ext = strrchr(url, '.');
        if (ext != nullptr) {
            ext++; // Skip the dot
            // Check common ZX Spectrum extensions
            const char* supported[] = { "pt3", "pt2", "pt1", "stc", "stp", "sqt", "psc", "asc",  "psm",  "vtx", "ym",
                                        "psg", "ay",  "ayc", "ts",  "ftc", "gtr", "dmm", "str",  "dst",  "fdi", "chi",
                                        "tfc", "tfd", "tf0", "cop", "pdt", "scl", "trd", "hrip", nullptr };

            for (int i = 0; supported[i] != nullptr; i++) {
                if (strcasecmp(ext, supported[i]) == 0) {
                    return RVProbeResult_Supported;
                }
            }
        }
    }

    return RVProbeResult_Unsupported;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static RVReadInfo zxtune_read_data(void* user_data, RVReadData dest) {
    auto* data = static_cast<ZXTuneData*>(user_data);
    RVAudioFormat format = { RVAudioStreamFormat_S16, 2, FREQ };

    if (!data->renderer) {
        return RVReadInfo { format, 0, RVReadStatus_Error};
    }

    auto* output = static_cast<int16_t*>(dest.channels_output);
    uint32_t max_frames = dest.channels_output_max_bytes_size / (sizeof(int16_t) * 2);
    uint32_t frames_written = 0;

    while (frames_written < max_frames) {
        // If we've consumed all samples from current chunk, render more
        if (data->chunk_offset >= data->chunk.size()) {
            data->chunk = data->renderer->Render();
            data->chunk_offset = 0;

            if (data->chunk.empty()) {
                if (frames_written == 0) {
                    return RVReadInfo { format, 0, RVReadStatus_Finished};
                }
                break;
            }
        }

        // Copy S16 samples from chunk to output
        size_t samples_available = data->chunk.size() - data->chunk_offset;
        size_t frames_needed = max_frames - frames_written;
        size_t frames_to_copy = std::min(samples_available, frames_needed);

        for (size_t i = 0; i < frames_to_copy; i++) {
            const Sound::Sample& sample = data->chunk[data->chunk_offset + i];
            output[(frames_written + i) * 2] = static_cast<int16_t>(sample.Left());
            output[(frames_written + i) * 2 + 1] = static_cast<int16_t>(sample.Right());
        }

        data->chunk_offset += frames_to_copy;
        frames_written += static_cast<uint32_t>(frames_to_copy);
    }

    return RVReadInfo { format, static_cast<uint16_t>(frames_written), RVReadStatus_Ok};
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static int64_t zxtune_seek(void* user_data, int64_t ms) {
    auto* data = static_cast<ZXTuneData*>(user_data);

    if (!data->renderer) {
        return -1;
    }

    try {
        data->renderer->SetPosition(Time::Instant<Time::Millisecond>(static_cast<uint64_t>(ms)));
        data->chunk = Sound::Chunk();
        data->chunk_offset = 0;
        return ms;
    } catch (const Error&) {
        return -1;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static int zxtune_metadata(const char* url, const RVService* service_api) {
    const RVIo* io_api = RVService_get_io(service_api, RV_IO_API_VERSION);
    const RVMetadata* metadata_api = RVService_get_metadata(service_api, RV_METADATA_API_VERSION);

    if (io_api == nullptr || metadata_api == nullptr) {
        return -1;
    }

    // Create temporary service and detector for metadata extraction
    auto service = ZXTune::Service::Create(Parameters::Container::Create());
    ModulesDetector detector;

    if (!load_and_detect_modules(url, *service, detector, io_api)) {
        return -1;
    }

    const auto& modules = detector.GetModules();

    RVMetadataId index = RVMetadata_create_url(metadata_api, url);

    // Get properties from first module
    const auto& holder = modules[0];
    const Parameters::Accessor::Ptr props = holder->GetModuleProperties();

    // Title
    if (auto title = props->FindString(Module::ATTR_TITLE)) {
        RVMetadata_set_tag(metadata_api, index, RV_METADATA_TITLE_TAG, title->c_str());
    }

    // Author
    if (auto author = props->FindString(Module::ATTR_AUTHOR)) {
        RVMetadata_set_tag(metadata_api, index, RV_METADATA_ARTIST_TAG, author->c_str());
    }

    // Program/tracker that created the file
    std::string format_str;
    if (auto program = props->FindString(Module::ATTR_PROGRAM)) {
        format_str = *program;
    } else if (auto type = props->FindString(Module::ATTR_TYPE)) {
        format_str = *type;
    }
    if (!format_str.empty()) {
        RVMetadata_set_tag(metadata_api, index, RV_METADATA_SONGTYPE_TAG, format_str.c_str());
    }

    // Comment
    if (auto comment = props->FindString(Module::ATTR_COMMENT)) {
        RVMetadata_set_tag(metadata_api, index, RV_METADATA_MESSAGE_TAG, comment->c_str());
    }

    // Date
    if (auto date = props->FindString(Module::ATTR_DATE)) {
        RVMetadata_set_tag(metadata_api, index, RV_METADATA_DATE_TAG, date->c_str());
    }

    // Duration
    auto info = holder->GetModuleInformation();
    if (info) {
        uint64_t duration_ms = info->Duration().Get();
        if (duration_ms > 0) {
            RVMetadata_set_tag_f64(metadata_api, index, RV_METADATA_LENGTH_TAG, duration_ms / 1000.0);
        }
    }

    // Add subsongs if there are multiple modules in the container
    if (modules.size() > 1) {
        for (size_t i = 0; i < modules.size(); i++) {
            const auto& sub_holder = modules[i];
            const auto sub_props = sub_holder->GetModuleProperties();
            auto sub_info = sub_holder->GetModuleInformation();

            std::string sub_title;
            if (auto title = sub_props->FindString(Module::ATTR_TITLE)) {
                sub_title = *title;
            }

            float length = 0.0f;
            if (sub_info) {
                length = sub_info->Duration().Get() / 1000.0f;
            }

            RVMetadata_add_subsong(metadata_api, index, static_cast<uint32_t>(i + 1), sub_title.c_str(), length);
        }
    }

    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void zxtune_event(void* user_data, uint8_t* event_data, uint64_t len) {
    auto* data = static_cast<ZXTuneData*>(user_data);

    if (!data->renderer || event_data == nullptr || len < 8) {
        return;
    }

    // Clear event data (we don't have detailed tracking info from ZXTune's public API)
    std::memset(event_data, 0, len);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void zxtune_static_init(const RVService* service_api) {
    g_rv_log = RVService_get_log(service_api, RV_LOG_API_VERSION);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Per-channel scope visualization using AY/YM chip per-channel levels.
// Captures 3-channel waveform data from the PSG emulator's internal state.

static uint32_t zxtune_get_scope_data(void* user_data, int channel, float* buffer, uint32_t num_samples) {
    auto* data = static_cast<ZXTuneData*>(user_data);
    if (data == nullptr || buffer == nullptr) {
        return 0;
    }

    if (channel < 0 || channel >= AYM_SCOPE_NUM_CHANNELS) {
        return 0;
    }

    // Auto-enable scope capture on first call
    if (!data->scope_enabled) {
        data->scope_state.Reset();
        g_aym_scope_state = &data->scope_state;
        data->scope_enabled = true;
    }

    return aym_scope_get_data(&data->scope_state, channel, buffer, num_samples);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint32_t zxtune_get_scope_channel_names(void* user_data, const char** names, uint32_t max_channels) {
    (void)user_data;
    static const char* s_names[] = { "AY A", "AY B", "AY C" };
    uint32_t count = 3;
    if (count > max_channels)
        count = max_channels;
    for (uint32_t i = 0; i < count; i++)
        names[i] = s_names[i];
    return count;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static RVPlaybackPlugin g_zxtune_plugin = {
    RV_PLAYBACK_PLUGIN_API_VERSION,
    "zxtune",
    "0.0.1",
    "ZXTune r5100",
    zxtune_probe_can_play,
    zxtune_supported_extensions,
    zxtune_create,
    zxtune_destroy,
    zxtune_event,
    zxtune_open,
    zxtune_close,
    zxtune_read_data,
    zxtune_seek,
    zxtune_metadata,
    zxtune_static_init,
    nullptr, // settings_updated
    nullptr, // get_tracker_info (not implemented yet)
    nullptr, // get_pattern_cell
    nullptr, // get_pattern_num_rows
    zxtune_get_scope_data,
    nullptr, // static_destroy
    zxtune_get_scope_channel_names,
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

extern "C" {
RV_EXPORT RVPlaybackPlugin* rv_playback_plugin(void) {
    return &g_zxtune_plugin;
}
}
