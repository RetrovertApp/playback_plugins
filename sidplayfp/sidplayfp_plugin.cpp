///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// libsidplayfp Playback Plugin
//
// Implements RVPlaybackPlugin interface for Commodore 64 SID music formats.
// Based on libsidplayfp by Leandro Nini.
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <retrovert/io.h>
#include <retrovert/log.h>
#include <retrovert/metadata.h>
#include <retrovert/playback.h>
#include <retrovert/service.h>
#include <retrovert/settings.h>

#include <builders/residfp-builder/residfp.h>
#include <sidplayfp/SidConfig.h>
#include <sidplayfp/SidInfo.h>
#include <sidplayfp/sidplayfp.h>
#include <sidplayfp/SidTune.h>
#include <sidplayfp/SidTuneInfo.h>

#include <cstdlib>
#include <cstring>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define FREQ 48000
#define MAX_BUFFER_SIZE 8192

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static const RVIo* g_io_api = nullptr;
static const RVLog* g_rv_log = nullptr;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Ring buffer size - enough for several frames of audio
#define RING_BUFFER_SIZE (48000 / 10) // ~100ms of audio at 48kHz

struct SidPlayData {
    sidplayfp* engine;
    SidTune* tune;
    ReSIDfpBuilder* builder;
    short* mix_buffer;
    int mix_buffer_size;
    uint8_t* song_data;
    uint32_t song_data_size;

    // Ring buffer for smoothing sample delivery
    float* ring_buffer;      // Stereo interleaved F32 samples
    uint32_t ring_write_pos; // Write position (in frames)
    uint32_t ring_read_pos;  // Read position (in frames)
    uint32_t ring_frames;    // Number of frames currently in buffer

    bool scope_enabled;
    int sid_count; // Number of SID chips used by current tune (1-3)
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static const char* sidplayfp_supported_extensions(void) {
    return "sid,psid,rsid,mus,str,prg,p00,c64";
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void* sidplayfp_create(const RVService* service_api) {
    SidPlayData* data = new SidPlayData();
    memset(data, 0, sizeof(SidPlayData));

    g_io_api = RVService_get_io(service_api, RV_IO_API_VERSION);

    data->engine = new sidplayfp();
    data->builder = new ReSIDfpBuilder("ReSIDfp");

    // Create SID emulators (support up to 3 SIDs for multi-SID tunes)
    unsigned int maxsids = data->engine->info().maxsids();
    data->builder->create(maxsids);

    if (!data->builder->getStatus()) {
        rv_error("Failed to create SID builder: %s", data->builder->error());
    }

    data->mix_buffer = new short[MAX_BUFFER_SIZE * 2];
    data->mix_buffer_size = MAX_BUFFER_SIZE;

    // Allocate ring buffer for smoothing (stereo F32)
    data->ring_buffer = new float[RING_BUFFER_SIZE * 2];
    data->ring_write_pos = 0;
    data->ring_read_pos = 0;
    data->ring_frames = 0;

    return data;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static int sidplayfp_destroy(void* user_data) {
    SidPlayData* data = static_cast<SidPlayData*>(user_data);

    delete[] data->mix_buffer;
    delete[] data->ring_buffer;
    delete[] data->song_data;
    delete data->tune;
    delete data->builder;
    delete data->engine;
    delete data;

    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static int sidplayfp_open(void* user_data, const char* url, uint32_t subsong, const RVService* service_api) {
    (void)service_api;

    RVIoReadUrlResult read_res;

    if ((read_res = RVIo_read_url_to_memory(g_io_api, url)).data == nullptr) {
        rv_error("Failed to load %s to memory", url);
        return -1;
    }

    SidPlayData* data = static_cast<SidPlayData*>(user_data);

    // Free previous tune if any
    if (data->tune) {
        data->engine->load(nullptr);
        delete data->tune;
        data->tune = nullptr;
    }

    // Free previous song data
    delete[] data->song_data;
    data->song_data = nullptr;

    // Keep a copy of the song data (SidTune doesn't copy it)
    data->song_data_size = static_cast<uint32_t>(read_res.data_size);
    data->song_data = new uint8_t[data->song_data_size];
    memcpy(data->song_data, read_res.data, data->song_data_size);

    RVIo_free_url_to_memory(g_io_api, read_res.data);

    // Load tune from memory
    data->tune = new SidTune(data->song_data, data->song_data_size);

    if (!data->tune->getStatus()) {
        rv_error("Failed to load SID tune: %s", data->tune->statusString());
        delete data->tune;
        data->tune = nullptr;
        return -1;
    }

    // Select subsong (0 = default starting song)
    data->tune->selectSong(subsong);

    // Configure the engine
    SidConfig cfg;
    cfg.frequency = FREQ;
    cfg.samplingMethod = SidConfig::INTERPOLATE;
    cfg.fastSampling = false;
    cfg.playback = SidConfig::STEREO;
    cfg.sidEmulation = data->builder;

    // Set default SID model based on tune info, or default to 6581
    const SidTuneInfo* info = data->tune->getInfo();
    if (info) {
        cfg.defaultSidModel
            = (info->sidModel(0) == SidTuneInfo::SIDMODEL_8580) ? SidConfig::MOS8580 : SidConfig::MOS6581;
        cfg.defaultC64Model = (info->clockSpeed() == SidTuneInfo::CLOCK_NTSC) ? SidConfig::NTSC : SidConfig::PAL;
    }

    if (!data->engine->config(cfg)) {
        rv_error("Failed to configure sidplayfp: %s", data->engine->error());
        delete data->tune;
        data->tune = nullptr;
        return -1;
    }

    // Load tune into engine
    if (!data->engine->load(data->tune)) {
        rv_error("Failed to load tune into engine: %s", data->engine->error());
        delete data->tune;
        data->tune = nullptr;
        return -1;
    }

    // Initialize mixer for stereo output
    data->engine->initMixer(true);

    // Detect number of SID chips used by this tune
    data->sid_count = info ? info->sidChips() : 1;
    if (data->sid_count < 1)
        data->sid_count = 1;
    if (data->sid_count > 3)
        data->sid_count = 3;

    // Reset ring buffer for new playback
    data->ring_write_pos = 0;
    data->ring_read_pos = 0;
    data->ring_frames = 0;

    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void sidplayfp_close(void* user_data) {
    SidPlayData* data = static_cast<SidPlayData*>(user_data);

    if (data->tune) {
        data->engine->load(nullptr);
        delete data->tune;
        data->tune = nullptr;
    }

    delete[] data->song_data;
    data->song_data = nullptr;
    data->song_data_size = 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static RVProbeResult sidplayfp_probe_can_play(uint8_t* probe_data, uint64_t data_size, const char* url,
                                              uint64_t total_size) {
    (void)url;
    (void)total_size;

    if (data_size < 4) {
        return RVProbeResult_Unsupported;
    }

    // Check for PSID/RSID header
    if ((probe_data[0] == 'P' || probe_data[0] == 'R') && probe_data[1] == 'S' && probe_data[2] == 'I'
        && probe_data[3] == 'D') {
        return RVProbeResult_Supported;
    }

    return RVProbeResult_Unsupported;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static RVReadInfo sidplayfp_read_data(void* user_data, RVReadData dest) {
    SidPlayData* data = static_cast<SidPlayData*>(user_data);

    if (!data->tune) {
        return RVReadInfo { { RVAudioStreamFormat_F32, 2, FREQ }, 0, RVReadStatus_Error, 0 };
    }

    // Calculate how many frames the host wants
    uint32_t frames_needed = dest.channels_output_max_bytes_size / (sizeof(float) * 2);
    float* output = static_cast<float*>(dest.channels_output);

    // Fill ring buffer until we have enough frames
    while (data->ring_frames < frames_needed) {
        // Generate a batch of samples
        // PAL C64: 985248 Hz CPU, output at 48000 Hz = ~20.5 cycles/sample
        // Generate ~1024 frames worth at a time
        unsigned int batch_frames = 1024;
        unsigned int cycles = batch_frames * 21;
        int samples = data->engine->play(cycles);

        if (samples < 0) {
            rv_error("sidplayfp playback error: %s", data->engine->error());
            return RVReadInfo { { RVAudioStreamFormat_F32, 2, FREQ }, 0, RVReadStatus_Error, 0 };
        }

        if (samples == 0) {
            // No more samples - return what we have
            break;
        }

        // Mix to S16 buffer
        unsigned int mixed = data->engine->mix(data->mix_buffer, static_cast<unsigned int>(samples));
        uint32_t frames_generated = mixed / 2;

        // Convert S16 to F32 and write to ring buffer
        for (uint32_t i = 0; i < frames_generated; i++) {
            if (data->ring_frames >= RING_BUFFER_SIZE) {
                // Ring buffer full - shouldn't happen with reasonable request sizes
                break;
            }

            uint32_t write_idx = data->ring_write_pos * 2;
            data->ring_buffer[write_idx] = static_cast<float>(data->mix_buffer[i * 2]) / 32768.0f;
            data->ring_buffer[write_idx + 1] = static_cast<float>(data->mix_buffer[i * 2 + 1]) / 32768.0f;

            data->ring_write_pos = (data->ring_write_pos + 1) % RING_BUFFER_SIZE;
            data->ring_frames++;
        }
    }

    // Copy frames from ring buffer to output
    uint32_t frames_to_output = frames_needed;
    if (frames_to_output > data->ring_frames) {
        frames_to_output = data->ring_frames;
    }

    for (uint32_t i = 0; i < frames_to_output; i++) {
        uint32_t read_idx = data->ring_read_pos * 2;
        output[i * 2] = data->ring_buffer[read_idx];
        output[i * 2 + 1] = data->ring_buffer[read_idx + 1];

        data->ring_read_pos = (data->ring_read_pos + 1) % RING_BUFFER_SIZE;
        data->ring_frames--;
    }

    RVAudioFormat format = { RVAudioStreamFormat_F32, 2, FREQ };
    return RVReadInfo { format, static_cast<uint16_t>(frames_to_output), RVReadStatus_Ok, 0 };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static int64_t sidplayfp_seek(void* user_data, int64_t ms) {
    (void)user_data;
    (void)ms;
    // Seeking not supported for SID files
    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static int sidplayfp_metadata(const char* url, const RVService* service_api) {
    RVIoReadUrlResult read_res;

    const RVIo* io_api = RVService_get_io(service_api, RV_IO_API_VERSION);
    const RVMetadata* metadata_api = RVService_get_metadata(service_api, RV_METADATA_API_VERSION);

    if (io_api == nullptr || metadata_api == nullptr) {
        return -1;
    }

    if ((read_res = RVIo_read_url_to_memory(io_api, url)).data == nullptr) {
        rv_error("Failed to load %s to memory", url);
        return -1;
    }

    SidTune tune(static_cast<const uint8_t*>(read_res.data), static_cast<uint32_t>(read_res.data_size));

    if (!tune.getStatus()) {
        RVIo_free_url_to_memory(io_api, read_res.data);
        return -1;
    }

    const SidTuneInfo* info = tune.getInfo();
    if (!info) {
        RVIo_free_url_to_memory(io_api, read_res.data);
        return -1;
    }

    RVMetadataId index = RVMetadata_create_url(metadata_api, url);

    // Info strings: 0=title, 1=author, 2=released
    if (info->numberOfInfoStrings() > 0) {
        RVMetadata_set_tag(metadata_api, index, RV_METADATA_TITLE_TAG, info->infoString(0));
    }
    if (info->numberOfInfoStrings() > 1) {
        RVMetadata_set_tag(metadata_api, index, RV_METADATA_ARTIST_TAG, info->infoString(1));
    }
    if (info->numberOfInfoStrings() > 2) {
        RVMetadata_set_tag(metadata_api, index, RV_METADATA_DATE_TAG, info->infoString(2));
    }

    // Format string (PSID, RSID, etc.)
    RVMetadata_set_tag(metadata_api, index, RV_METADATA_SONGTYPE_TAG, info->formatString());

    // SID model info - include in message
    const char* sid_model = "Unknown";
    switch (info->sidModel(0)) {
        case SidTuneInfo::SIDMODEL_6581:
            sid_model = "MOS 6581";
            break;
        case SidTuneInfo::SIDMODEL_8580:
            sid_model = "MOS 8580";
            break;
        case SidTuneInfo::SIDMODEL_ANY:
            sid_model = "Any SID";
            break;
        default:
            break;
    }
    RVMetadata_set_tag(metadata_api, index, RV_METADATA_MESSAGE_TAG, sid_model);

    // Length is not available in SID files themselves (would need HVSC Songlengths.md5)
    RVMetadata_set_tag_f64(metadata_api, index, RV_METADATA_LENGTH_TAG, 0.0);

    // Add subsongs if more than one
    unsigned int songs = info->songs();
    if (songs > 1) {
        for (unsigned int i = 1; i <= songs; i++) {
            tune.selectSong(i);
            const SidTuneInfo* sub_info = tune.getInfo();
            const char* sub_title = "";
            if (sub_info && sub_info->numberOfInfoStrings() > 0) {
                sub_title = sub_info->infoString(0);
            }
            RVMetadata_add_subsong(metadata_api, index, i, sub_title, 0.0f);
        }
    }

    RVIo_free_url_to_memory(io_api, read_res.data);
    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void sidplayfp_event(void* user_data, uint8_t* event_data, uint64_t len) {
    (void)user_data;
    (void)event_data;
    (void)len;
    // Event reporting not implemented for SID
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void sidplayfp_static_init(const RVService* service_api) {
    g_rv_log = RVService_get_log(service_api, RV_LOG_API_VERSION);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint32_t sidplayfp_get_scope_data(void* user_data, int channel, float* buffer, uint32_t num_samples) {
    SidPlayData* data = static_cast<SidPlayData*>(user_data);
    if (data == nullptr || data->builder == nullptr || buffer == nullptr) {
        return 0;
    }

    if (!data->scope_enabled) {
        data->builder->enableScopeCapture(true);
        data->scope_enabled = true;
    }

    return data->builder->getScopeData(channel, buffer, num_samples);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint32_t sidplayfp_get_scope_channel_names(void* user_data, const char** names, uint32_t max_channels) {
    SidPlayData* data = static_cast<SidPlayData*>(user_data);

    // Single SID: "Voice 1", "Voice 2", "Voice 3"
    static const char* s_single_names[] = { "Voice 1", "Voice 2", "Voice 3" };

    // Multi SID: "SID 1 V1" .. "SID 3 V3"
    static const char* s_multi_names[] = {
        "SID 1 V1", "SID 1 V2", "SID 1 V3", "SID 2 V1", "SID 2 V2", "SID 2 V3", "SID 3 V1", "SID 3 V2", "SID 3 V3",
    };

    int sid_count = (data != nullptr) ? data->sid_count : 1;
    if (sid_count < 1)
        sid_count = 1;

    const char** src = (sid_count > 1) ? s_multi_names : s_single_names;
    uint32_t count = static_cast<uint32_t>(sid_count) * 3;
    if (count > max_channels)
        count = max_channels;
    for (uint32_t i = 0; i < count; i++)
        names[i] = src[i];
    return count;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static RVPlaybackPlugin g_sidplayfp_plugin = {
    RV_PLAYBACK_PLUGIN_API_VERSION,
    "sidplayfp",
    "0.0.1",
    "libsidplayfp 2.16.0",
    sidplayfp_probe_can_play,
    sidplayfp_supported_extensions,
    sidplayfp_create,
    sidplayfp_destroy,
    sidplayfp_event,
    sidplayfp_open,
    sidplayfp_close,
    sidplayfp_read_data,
    sidplayfp_seek,
    sidplayfp_metadata,
    sidplayfp_static_init,
    nullptr, // settings_updated

    // Tracker visualization API (not applicable to SID)
    nullptr, // get_tracker_info
    nullptr, // get_pattern_cell
    nullptr, // get_pattern_num_rows
    sidplayfp_get_scope_data,
    nullptr, // static_destroy
    sidplayfp_get_scope_channel_names,
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

extern "C" {
RV_EXPORT RVPlaybackPlugin* rv_playback_plugin(void) {
    return &g_sidplayfp_plugin;
}
}
