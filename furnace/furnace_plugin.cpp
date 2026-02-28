///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Furnace Playback Plugin
//
// Implements RVPlaybackPlugin interface for Furnace tracker files (.fur),
// Deflemask files (.dmf), and FamiTracker files (.ftm) using the Furnace engine.
// Furnace emulates 50+ sound chips for multi-system chiptune playback.
// Audio output: Stereo F32 at 44100 Hz.
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

#ifdef _WIN32
#define strcasecmp _stricmp
#else
#include <strings.h>
#endif

// Furnace engine
#include "engine/engine.h"
#include "ta-log.h"

extern "C" {
#include <retrovert/io.h>
#include <retrovert/log.h>
#include <retrovert/metadata.h>
#include <retrovert/playback.h>
#include <retrovert/service.h>
}

// Stub for reportError (defined in Furnace's main.cpp which we don't compile)
void reportError(String what) {
    (void)what;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define FURNACE_SAMPLE_RATE 44100
#define FURNACE_CHANNELS 2
#define FURNACE_BUF_FRAMES 4096

static const RVIo* g_io_api = nullptr;
const RVLog* g_rv_log = nullptr;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct FurnaceReplayerData {
    DivEngine* engine;
    uint8_t* file_data;
    size_t file_size;
    float* left_buf;
    float* right_buf;
    bool playing;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static const char* furnace_supported_extensions(void) {
    return "fur,dmf,ftm,0cc,dnm,eft,tfm,tfe";
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void furnace_static_init(const RVService* service_api) {
    g_rv_log = RVService_get_log(service_api, RV_LOG_API_VERSION);

    // Initialize Furnace's internal logging to stderr to prevent crash
    // when writeLog() is called before initLog() (logOut would be null)
    initLog(stderr);

    // Suppress Furnace's verbose debug/trace logging (e.g. "opening config for read")
    // Default is LOGLEVEL_TRACE which floods stderr with internal messages
    logLevel = LOGLEVEL_ERROR;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void* furnace_create(const RVService* service_api) {
    FurnaceReplayerData* data = (FurnaceReplayerData*)calloc(1, sizeof(FurnaceReplayerData));
    if (!data) {
        return nullptr;
    }

    g_io_api = RVService_get_io(service_api, RV_IO_API_VERSION);

    data->engine = new (std::nothrow) DivEngine();
    if (!data->engine) {
        free(data);
        return nullptr;
    }

    data->engine->preInit();

    data->left_buf = (float*)calloc(FURNACE_BUF_FRAMES, sizeof(float));
    data->right_buf = (float*)calloc(FURNACE_BUF_FRAMES, sizeof(float));
    if (!data->left_buf || !data->right_buf) {
        delete data->engine;
        free(data->left_buf);
        free(data->right_buf);
        free(data);
        return nullptr;
    }

    return data;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static int furnace_destroy(void* user_data) {
    FurnaceReplayerData* data = (FurnaceReplayerData*)user_data;
    if (data->engine) {
        data->engine->quit(false);
        delete data->engine;
    }
    if (data->file_data) {
        RVIo_free_url_to_memory(g_io_api, data->file_data);
    }
    free(data->left_buf);
    free(data->right_buf);
    free(data);
    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool check_extension(const char* url, const char* ext) {
    const char* dot = strrchr(url, '.');
    return dot != nullptr && strcasecmp(dot + 1, ext) == 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static RVProbeResult furnace_probe_can_play(uint8_t* probe_data, uint64_t data_size, const char* url,
                                            uint64_t total_size) {
    (void)total_size;

    // Check magic bytes for Furnace native format
    if (data_size >= 16 && memcmp(probe_data, "-Furnace module-", 16) == 0) {
        return RVProbeResult_Supported;
    }

    // Check magic bytes for Deflemask format
    if (data_size >= 17 && memcmp(probe_data, ".DelekDefleMask.", 16) == 0) {
        return RVProbeResult_Supported;
    }

    // Check magic bytes for FamiTracker format
    if (data_size >= 18 && memcmp(probe_data, "FamiTracker Module", 18) == 0) {
        return RVProbeResult_Supported;
    }

    // Fall back to extension check
    if (url != nullptr) {
        if (check_extension(url, "fur") || check_extension(url, "dmf") || check_extension(url, "ftm")
            || check_extension(url, "0cc") || check_extension(url, "dnm") || check_extension(url, "eft")
            || check_extension(url, "tfm") || check_extension(url, "tfe")) {
            return RVProbeResult_Unsure;
        }
    }

    return RVProbeResult_Unsupported;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static int furnace_open(void* user_data, const char* url, uint32_t subsong, const RVService* service_api) {
    (void)service_api;

    FurnaceReplayerData* data = (FurnaceReplayerData*)user_data;

    // Clean up previous state
    if (data->playing) {
        data->engine->stop();
        data->engine->quit(false);
        delete data->engine;
        data->engine = new (std::nothrow) DivEngine();
        if (!data->engine) {
            return -1;
        }
        data->engine->preInit();
        data->playing = false;
    }
    if (data->file_data) {
        RVIo_free_url_to_memory(g_io_api, data->file_data);
        data->file_data = nullptr;
    }

    // Load file
    RVIoReadUrlResult read_res;
    if ((read_res = RVIo_read_url_to_memory(g_io_api, url)).data == nullptr) {
        rv_error("Furnace: Failed to load %s", url);
        return -1;
    }

    data->file_data = (uint8_t*)read_res.data;
    data->file_size = read_res.data_size;

    // Furnace load() takes ownership of a copy, so we give it a copy
    uint8_t* load_data = (uint8_t*)malloc(data->file_size);
    if (!load_data) {
        rv_error("Furnace: Failed to allocate memory for %s", url);
        return -1;
    }
    memcpy(load_data, data->file_data, data->file_size);

    if (!data->engine->load(load_data, data->file_size, url)) {
        rv_error("Furnace: Failed to load %s", url);
        free(load_data);
        return -1;
    }

    // Use dummy audio backend since we generate audio via nextBuf() calls,
    // not through a real audio device. This avoids the SDL requirement.
    data->engine->setAudio(DIV_AUDIO_DUMMY);
    data->engine->init();
    data->engine->initDispatch(true);
    data->engine->renderSamplesP();

    // Select subsong
    if (subsong > 0 && subsong < data->engine->song.subsong.size()) {
        data->engine->changeSongP(subsong);
    }

    data->engine->play();
    data->playing = true;

    rv_info("Furnace: Opened %s (subsong %u)", url, subsong);
    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void furnace_close(void* user_data) {
    FurnaceReplayerData* data = (FurnaceReplayerData*)user_data;

    if (data->playing) {
        data->engine->stop();
        data->playing = false;
    }

    if (data->file_data) {
        RVIo_free_url_to_memory(g_io_api, data->file_data);
        data->file_data = nullptr;
    }

    // Reset engine for next use
    data->engine->quit(false);
    delete data->engine;
    data->engine = new (std::nothrow) DivEngine();
    if (data->engine) {
        data->engine->preInit();
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static RVReadInfo furnace_read_data(void* user_data, RVReadData dest) {
    FurnaceReplayerData* data = (FurnaceReplayerData*)user_data;
    RVAudioFormat format = { RVAudioStreamFormat_F32, FURNACE_CHANNELS, FURNACE_SAMPLE_RATE };

    if (!data->playing || !data->engine) {
        return (RVReadInfo) { format, 0, RVReadStatus_Finished};
    }

    uint32_t max_frames = dest.channels_output_max_bytes_size / (sizeof(float) * FURNACE_CHANNELS);
    uint32_t frames_to_render = max_frames < FURNACE_BUF_FRAMES ? max_frames : FURNACE_BUF_FRAMES;

    // Clear buffers
    memset(data->left_buf, 0, frames_to_render * sizeof(float));
    memset(data->right_buf, 0, frames_to_render * sizeof(float));

    // Render into separate L/R buffers
    float* out_buf[2] = { data->left_buf, data->right_buf };
    data->engine->nextBuf(nullptr, out_buf, 0, 2, (unsigned int)frames_to_render);

    // Interleave L/R into output
    float* output = (float*)dest.channels_output;
    for (uint32_t i = 0; i < frames_to_render; i++) {
        output[i * 2] = data->left_buf[i];
        output[i * 2 + 1] = data->right_buf[i];
    }

    // Check end of song
    if (data->engine->endOfSong) {
        data->playing = false;
        return (RVReadInfo) { format, frames_to_render, RVReadStatus_Finished};
    }

    return (RVReadInfo) { format, frames_to_render, RVReadStatus_Ok};
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static int64_t furnace_seek(void* user_data, int64_t ms) {
    FurnaceReplayerData* data = (FurnaceReplayerData*)user_data;

    if (!data->engine || !data->playing) {
        return -1;
    }

    // Furnace doesn't have a direct time-based seek API.
    // Seek to beginning if ms is 0, otherwise not supported.
    if (ms == 0) {
        data->engine->setOrder(0);
        return 0;
    }

    return -1;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static int furnace_metadata(const char* url, const RVService* service_api) {
    const RVIo* io_api = RVService_get_io(service_api, RV_IO_API_VERSION);
    const RVMetadata* metadata_api = RVService_get_metadata(service_api, RV_METADATA_API_VERSION);

    if (!io_api || !metadata_api) {
        return -1;
    }

    RVIoReadUrlResult read_res;
    if ((read_res = RVIo_read_url_to_memory(io_api, url)).data == nullptr) {
        return -1;
    }

    // Create a temporary engine to read metadata
    DivEngine engine;
    engine.preInit();

    uint8_t* load_data = (uint8_t*)malloc(read_res.data_size);
    if (!load_data) {
        RVIo_free_url_to_memory(io_api, read_res.data);
        return -1;
    }
    memcpy(load_data, read_res.data, read_res.data_size);

    if (!engine.load(load_data, read_res.data_size)) {
        free(load_data);
        RVIo_free_url_to_memory(io_api, read_res.data);
        return -1;
    }

    RVMetadataId id = RVMetadata_create_url(metadata_api, url);

    if (!engine.song.name.empty()) {
        RVMetadata_set_tag(metadata_api, id, RV_METADATA_TITLE_TAG, engine.song.name.c_str());
    }
    if (!engine.song.author.empty()) {
        RVMetadata_set_tag(metadata_api, id, RV_METADATA_ARTIST_TAG, engine.song.author.c_str());
    }
    RVMetadata_set_tag(metadata_api, id, RV_METADATA_SONGTYPE_TAG, "Furnace");

    // Report subsongs
    size_t subsong_count = engine.song.subsong.size();
    if (subsong_count > 1) {
        for (size_t i = 0; i < subsong_count; i++) {
            const char* name = engine.song.subsong[i]->name.c_str();
            RVMetadata_add_subsong(metadata_api, id, (uint32_t)i, name, 0.0f);
        }
    }

    engine.quit(false);
    RVIo_free_url_to_memory(io_api, read_res.data);

    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void furnace_event(void* user_data, uint8_t* event_data, uint64_t len) {
    (void)user_data;
    (void)event_data;
    (void)len;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint32_t furnace_get_scope_data(void* user_data, int channel, float* buffer, uint32_t num_samples) {
    FurnaceReplayerData* data = (FurnaceReplayerData*)user_data;
    if (!data || !data->engine || !data->playing || !buffer) {
        return 0;
    }

    int total_chans = data->engine->getTotalChannelCount();
    if (channel < 0 || channel >= total_chans) {
        return 0;
    }

    DivDispatchOscBuffer* osc = data->engine->getOscBuffer(channel);
    if (!osc) {
        return 0;
    }

    // Read from the osc buffer (65536-entry S16 ring buffer with fixed-point needle)
    if (num_samples > 1024) {
        num_samples = 1024;
    }

    unsigned short write_pos = osc->needle >> 16;
    unsigned short read_pos = write_pos - (unsigned short)num_samples;

    for (uint32_t i = 0; i < num_samples; i++) {
        short sample = osc->data[read_pos];
        // -1 (0xffff) means "no data" in Furnace's osc buffer
        if (sample == -1) {
            buffer[i] = 0.0f;
        } else {
            buffer[i] = (float)sample / 32768.0f;
        }
        read_pos++;
    }

    return num_samples;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint32_t furnace_get_scope_channel_names(void* user_data, const char** names, uint32_t max_channels) {
    FurnaceReplayerData* data = static_cast<FurnaceReplayerData*>(user_data);
    if (data == nullptr || data->engine == nullptr)
        return 0;

    static char s_name_bufs[64][16];
    int total = data->engine->getTotalChannelCount();
    uint32_t count = total > 0 ? (uint32_t)total : 0;
    if (count > 64)
        count = 64;
    if (count > max_channels)
        count = max_channels;
    for (uint32_t i = 0; i < count; i++) {
        const char* ch_name = data->engine->getChannelShortName(i);
        if (ch_name != nullptr && ch_name[0] != '\0') {
            snprintf(s_name_bufs[i], sizeof(s_name_bufs[i]), "%s", ch_name);
        } else {
            snprintf(s_name_bufs[i], sizeof(s_name_bufs[i]), "Ch %u", i + 1);
        }
        names[i] = s_name_bufs[i];
    }
    return count;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static RVPlaybackPlugin g_furnace_plugin = {
    RV_PLAYBACK_PLUGIN_API_VERSION,
    "furnace",
    "0.1.0",
    "Furnace 0.6.8.3",
    furnace_probe_can_play,
    furnace_supported_extensions,
    furnace_create,
    furnace_destroy,
    furnace_event,
    furnace_open,
    furnace_close,
    furnace_read_data,
    furnace_seek,
    furnace_metadata,
    furnace_static_init,
    nullptr, // settings_updated
    nullptr, // get_tracker_info
    nullptr, // get_pattern_cell
    nullptr, // get_pattern_num_rows
    furnace_get_scope_data,
    nullptr, // static_destroy
    furnace_get_scope_channel_names,
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

extern "C" RV_EXPORT RVPlaybackPlugin* rv_playback_plugin(void) {
    return &g_furnace_plugin;
}
