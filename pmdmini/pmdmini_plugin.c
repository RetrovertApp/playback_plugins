///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// PMDmini Playback Plugin
//
// Implements RVPlaybackPlugin interface for NEC PC-98 PMD music format using pmdmini library.
// PMD uses YM2608 (OPNA) FM synthesis. The library uses global state so only one file can be
// open at a time (fine since only one song plays at a time).
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// C11 nullptr compatibility
#ifndef nullptr
#define nullptr ((void*)0)
#endif

#include <retrovert/log.h>
#include <retrovert/metadata.h>
#include <retrovert/playback.h>
#include <retrovert/service.h>

#include "pmdmini.h"

#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define strcasecmp _stricmp
#else
#include <strings.h>
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define PMD_SAMPLE_RATE 44100

const RVLog* g_rv_log = nullptr;
static int g_pmd_initialized = 0;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Forward declarations for scope capture functions (defined in opna.cpp and psg.cpp via patch)
void pmd_scope_enable(int enable);
unsigned int pmd_fm_scope_get_data(int channel, float* buffer, unsigned int num_samples);
unsigned int psg_scope_get_data(int channel, float* buffer, unsigned int num_samples);

typedef struct PmdReplayerData {
    int file_open;
    int length_sec;
    int elapsed_frames;
    bool scope_enabled;
} PmdReplayerData;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Extract the directory path from a URL/filepath

static void extract_directory(const char* path, char* dir, size_t dir_size) {
    const char* last_sep = strrchr(path, '/');
#ifdef _WIN32
    const char* last_bsep = strrchr(path, '\\');
    if (last_bsep != nullptr && (last_sep == nullptr || last_bsep > last_sep)) {
        last_sep = last_bsep;
    }
#endif
    if (last_sep != nullptr) {
        size_t len = (size_t)(last_sep - path);
        if (len >= dir_size) {
            len = dir_size - 1;
        }
        memcpy(dir, path, len);
        dir[len] = '\0';
    } else {
        dir[0] = '.';
        dir[1] = '\0';
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static const char* pmdmini_plugin_supported_extensions(void) {
    return "m,m2";
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void* pmdmini_plugin_create(const RVService* service_api) {
    PmdReplayerData* data = malloc(sizeof(PmdReplayerData));
    if (data == nullptr) {
        return nullptr;
    }
    memset(data, 0, sizeof(PmdReplayerData));

    return data;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static int pmdmini_plugin_destroy(void* user_data) {
    PmdReplayerData* data = (PmdReplayerData*)user_data;

    if (data->file_open) {
        pmd_stop();
    }

    free(data);
    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static int pmdmini_plugin_open(void* user_data, const char* url, uint32_t subsong, const RVService* service_api) {
    (void)subsong;
    (void)service_api;

    PmdReplayerData* data = (PmdReplayerData*)user_data;

    // Close previous if any (global state)
    if (data->file_open) {
        pmd_stop();
        data->file_open = 0;
    }

    // Extract directory for PCM sample loading
    char dir[2048];
    extract_directory(url, dir, sizeof(dir));

    // pmd_play takes a filesystem path directly
    if (pmd_play(url, dir) != 0) {
        rv_error("PMDmini: failed to open %s", url);
        return -1;
    }

    data->file_open = 1;

    // Get song duration
    data->length_sec = pmd_length_sec();
    data->elapsed_frames = 0;

    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void pmdmini_plugin_close(void* user_data) {
    PmdReplayerData* data = (PmdReplayerData*)user_data;

    if (data->file_open) {
        pmd_stop();
        data->file_open = 0;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static RVProbeResult pmdmini_plugin_probe_can_play(uint8_t* probe_data, uint64_t data_size, const char* url,
                                                   uint64_t total_size) {
    (void)total_size;

    // PMD header check: byte[1] indicates channel count offset.
    // pmdmini 2.0.0 requires byte[1] == 0x18 (12 channels) or 0x1A (13 channels).
    // This gives us a reliable way to identify PMD files and take priority over
    // other plugins (like adplug) that also handle the .m extension.
    if (data_size >= 2 && (probe_data[1] == 0x18 || probe_data[1] == 0x1A)) {
        return RVProbeResult_Supported;
    }

    // Fall back to extension check for files we couldn't validate by header
    if (url != nullptr) {
        const char* dot = strrchr(url, '.');
        if (dot != nullptr) {
            if (strcasecmp(dot, ".m") == 0 || strcasecmp(dot, ".m2") == 0) {
                return RVProbeResult_Unsure;
            }
        }
    }

    return RVProbeResult_Unsupported;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static RVReadInfo pmdmini_plugin_read_data(void* user_data, RVReadData dest) {
    PmdReplayerData* data = (PmdReplayerData*)user_data;
    RVAudioFormat format = { RVAudioStreamFormat_S16, 2, PMD_SAMPLE_RATE };

    if (!data->file_open) {
        return (RVReadInfo) { format, 0, RVReadStatus_Error};
    }

    // Check if song ended (based on duration)
    if (data->length_sec > 0 && data->elapsed_frames / PMD_SAMPLE_RATE >= data->length_sec) {
        return (RVReadInfo) { format, 0, RVReadStatus_Finished};
    }

    // Calculate how many S16 stereo frames fit in the output buffer
    uint32_t max_frames = dest.channels_output_max_bytes_size / (sizeof(int16_t) * 2);

    // pmd_renderer outputs interleaved stereo S16 directly to output buffer
    pmd_renderer((int16_t*)dest.channels_output, (int)max_frames);

    data->elapsed_frames += (int)max_frames;

    // Check if song ended after rendering
    RVReadStatus status = RVReadStatus_Ok;
    if (data->length_sec > 0 && data->elapsed_frames / PMD_SAMPLE_RATE >= data->length_sec) {
        status = RVReadStatus_Finished;
    }

    return (RVReadInfo) { format, (uint32_t)max_frames, status};
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static int64_t pmdmini_plugin_seek(void* user_data, int64_t ms) {
    (void)user_data;
    (void)ms;

    // pmdmini has no native seek - would require stop+play+skip frames
    // Return -1 to indicate seek is not supported
    return -1;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static int pmdmini_plugin_metadata(const char* url, const RVService* service_api) {
    const RVMetadata* metadata_api = RVService_get_metadata(service_api, RV_METADATA_API_VERSION);

    if (metadata_api == nullptr) {
        return -1;
    }

    // Extract directory for PCM sample loading
    char dir[2048];
    extract_directory(url, dir, sizeof(dir));

    // Open the PMD file to extract metadata
    if (pmd_play(url, dir) != 0) {
        return -1;
    }

    RVMetadataId index = RVMetadata_create_url(metadata_api, url);

    // Get title (may be Shift-JIS encoded)
    char title[256];
    memset(title, 0, sizeof(title));
    pmd_get_title(title);
    if (title[0] != '\0') {
        RVMetadata_set_tag(metadata_api, index, RV_METADATA_TITLE_TAG, title);
    }

    // Get composer (may be Shift-JIS encoded)
    char composer[256];
    memset(composer, 0, sizeof(composer));
    pmd_get_compo(composer);
    if (composer[0] != '\0') {
        RVMetadata_set_tag(metadata_api, index, RV_METADATA_ARTIST_TAG, composer);
    }

    // Set song type
    RVMetadata_set_tag(metadata_api, index, RV_METADATA_SONGTYPE_TAG, "PMD");
    RVMetadata_set_tag(metadata_api, index, RV_METADATA_AUTHORINGTOOL_TAG, "NEC PC-98");

    // Get duration
    int length = pmd_length_sec();
    if (length > 0) {
        RVMetadata_set_tag_f64(metadata_api, index, RV_METADATA_LENGTH_TAG, (double)length);
    }

    pmd_stop();
    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void pmdmini_plugin_event(void* user_data, uint8_t* event_data, uint64_t len) {
    (void)user_data;
    (void)event_data;
    (void)len;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void pmdmini_plugin_static_init(const RVService* service_api) {
    g_rv_log = RVService_get_log(service_api, RV_LOG_API_VERSION);

    if (!g_pmd_initialized) {
        pmd_init();
        pmd_setrate(PMD_SAMPLE_RATE);
        g_pmd_initialized = 1;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint32_t pmdmini_plugin_get_scope_data(void* user_data, int channel, float* buffer, uint32_t num_samples) {
    PmdReplayerData* data = (PmdReplayerData*)user_data;
    if (data == nullptr || !data->file_open || buffer == nullptr) {
        return 0;
    }

    if (!data->scope_enabled) {
        pmd_scope_enable(1);
        data->scope_enabled = true;
    }

    // Channels 0-5: FM, channels 6-8: SSG
    if (channel < 6) {
        return pmd_fm_scope_get_data(channel, buffer, num_samples);
    } else if (channel < 9) {
        return psg_scope_get_data(channel - 6, buffer, num_samples);
    }

    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint32_t pmdmini_plugin_get_scope_channel_names(void* user_data, const char** names, uint32_t max_channels) {
    (void)user_data;
    static const char* s_names[] = { "FM 1", "FM 2", "FM 3", "FM 4", "FM 5", "FM 6", "SSG 1", "SSG 2", "SSG 3" };
    uint32_t count = 9;
    if (count > max_channels)
        count = max_channels;
    for (uint32_t i = 0; i < count; i++)
        names[i] = s_names[i];
    return count;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static RVPlaybackPlugin g_pmdmini_plugin = {
    RV_PLAYBACK_PLUGIN_API_VERSION,
    "pmdmini",
    "0.0.1",
    "pmdmini 2.0.0",
    pmdmini_plugin_probe_can_play,
    pmdmini_plugin_supported_extensions,
    pmdmini_plugin_create,
    pmdmini_plugin_destroy,
    pmdmini_plugin_event,
    pmdmini_plugin_open,
    pmdmini_plugin_close,
    pmdmini_plugin_read_data,
    pmdmini_plugin_seek,
    pmdmini_plugin_metadata,
    pmdmini_plugin_static_init,
    nullptr, // settings_updated
    nullptr, // get_tracker_info
    nullptr, // get_pattern_cell
    nullptr, // get_pattern_num_rows
    pmdmini_plugin_get_scope_data,
    nullptr, // static_destroy
    pmdmini_plugin_get_scope_channel_names,
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RV_EXPORT RVPlaybackPlugin* rv_playback_plugin(void) {
    return &g_pmdmini_plugin;
}
