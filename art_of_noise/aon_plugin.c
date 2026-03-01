///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Art of Noise Playback Plugin
//
// Implements RVPlaybackPlugin interface for AON4 (4-channel) and AON8 (8-channel) tracker formats.
// Based on the aon_player C11 replayer library.
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#ifndef nullptr
#define nullptr ((void*)0)
#endif

#include <retrovert/io.h>
#include <retrovert/log.h>
#include <retrovert/metadata.h>
#include <retrovert/playback.h>
#include <retrovert/service.h>
#include <retrovert/settings.h>

#include "aon_player.h"

#include <stdlib.h>
#include <string.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define FREQ 48000

RV_PLUGIN_USE_IO_API();
RV_PLUGIN_USE_METADATA_API();
RV_PLUGIN_USE_LOG_API();

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct AonPluginData {
    AonSong* song;
    void* song_data;
    int scope_enabled;
} AonPluginData;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static const char* aon_supported_extensions(void) {
    return "aon";
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void* aon_create(const RVService* service_api) {
    void* data = malloc(sizeof(AonPluginData));
    memset(data, 0, sizeof(AonPluginData));

    return data;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static int aon_destroy(void* user_data) {
    AonPluginData* data = (AonPluginData*)user_data;

    if (data->song) {
        aon_song_destroy(data->song);
    }

    free(data->song_data);
    free(data);
    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static int aon_open(void* user_data, const char* url, uint32_t subsong, const RVService* service_api) {
    (void)service_api;

    RVIoReadUrlResult read_res;

    if ((read_res = rv_io_read_url_to_memory(url)).data == nullptr) {
        rv_error("Failed to load %s to memory", url);
        return -1;
    }

    AonPluginData* data = (AonPluginData*)user_data;

    // Free previous song if any
    if (data->song) {
        aon_song_destroy(data->song);
        data->song = nullptr;
    }
    free(data->song_data);
    data->song_data = nullptr;

    data->song = aon_song_create(read_res.data, (uint32_t)read_res.data_size);
    if (data->song == nullptr) {
        rv_error("Failed to parse %s", url);
        rv_io_free_url_to_memory(read_res.data);
        return -1;
    }

    // Keep a copy of the file data alive for sample playback
    data->song_data = malloc((size_t)read_res.data_size);
    memcpy(data->song_data, read_res.data, (size_t)read_res.data_size);
    rv_io_free_url_to_memory(read_res.data);

    aon_song_set_subsong(data->song, (int)subsong);
    aon_song_set_sample_rate(data->song, FREQ);
    aon_song_start(data->song);

    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void aon_close(void* user_data) {
    AonPluginData* data = (AonPluginData*)user_data;

    if (data->song) {
        aon_song_destroy(data->song);
        data->song = nullptr;
    }

    free(data->song_data);
    data->song_data = nullptr;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static RVProbeResult aon_probe_can_play(uint8_t* data, uint64_t data_size, const char* url, uint64_t total_size) {
    (void)url;
    (void)total_size;

    if (data_size < 4) {
        return RVProbeResult_Unsupported;
    }

    if (memcmp(data, "AON4", 4) == 0 || memcmp(data, "AON8", 4) == 0) {
        return RVProbeResult_Supported;
    }

    return RVProbeResult_Unsupported;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static RVReadInfo aon_read_data(void* user_data, RVReadData dest) {
    AonPluginData* data = (AonPluginData*)user_data;

    uint32_t max_frames = dest.channels_output_max_bytes_size / (sizeof(float) * 2);
    float* output = (float*)dest.channels_output;

    if (data->song == nullptr || aon_song_is_finished(data->song)) {
        RVAudioFormat format = { RVAudioStreamFormat_F32, 2, FREQ };
        return (RVReadInfo){ format, 0, RVReadStatus_Finished };
    }

    int frames_decoded = aon_song_decode(data->song, output, (int)max_frames);

    RVAudioFormat format = { RVAudioStreamFormat_F32, 2, FREQ };
    RVReadStatus status = aon_song_is_finished(data->song) && frames_decoded == 0
                              ? RVReadStatus_Finished
                              : RVReadStatus_Ok;

    return (RVReadInfo){ format, (uint32_t)frames_decoded, status };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static int64_t aon_seek(void* user_data, int64_t ms) {
    (void)user_data;
    (void)ms;
    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static int aon_metadata(const char* url, const RVService* service_api) {
    (void)service_api;
    RVIoReadUrlResult read_res;

    if ((read_res = rv_io_read_url_to_memory(url)).data == nullptr) {
        rv_error("Failed to load %s to memory", url);
        return -1;
    }

    AonSong* song = aon_song_create(read_res.data, (uint32_t)read_res.data_size);
    if (song == nullptr) {
        rv_io_free_url_to_memory(read_res.data);
        return -1;
    }

    const AonSongMetadata* meta = aon_song_get_metadata(song);

    // Determine song type from magic bytes
    const char* song_type = "AON4";
    if (read_res.data_size >= 4 && memcmp(read_res.data, "AON8", 4) == 0) {
        song_type = "AON8";
    }

    RVMetadataId index = rv_metadata_create_url(url);

    rv_metadata_set_tag(index, RV_METADATA_TITLE_TAG, meta->song_name);
    rv_metadata_set_tag(index, RV_METADATA_SONGTYPE_TAG, song_type);
    rv_metadata_set_tag(index, RV_METADATA_ARTIST_TAG, meta->author);
    rv_metadata_set_tag_f64(index, RV_METADATA_LENGTH_TAG, 0.0);

    for (int i = 0; i < meta->num_instruments; ++i) {
        const char* name = aon_song_get_instrument_name(song, (uint8_t)i);
        if (name) {
            rv_metadata_add_instrument(index, name);
        }
    }

    aon_song_destroy(song);
    rv_io_free_url_to_memory(read_res.data);

    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void aon_event(void* user_data, uint8_t* data, uint64_t len) {
    (void)user_data;
    (void)data;
    (void)len;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void aon_static_init(const RVService* service_api) {
    rv_init_log_api(service_api);
    rv_init_io_api(service_api);
    rv_init_metadata_api(service_api);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Tracker visualization API

static int aon_get_tracker_info(void* user_data, RVTrackerInfo* info) {
    AonPluginData* data = (AonPluginData*)user_data;

    if (data->song == nullptr) {
        return -1;
    }

    const AonSongMetadata* meta = aon_song_get_metadata(data->song);
    const AonPlaybackState* state = aon_song_get_playback_state(data->song);

    info->num_patterns = meta->num_patterns;
    info->num_channels = meta->num_channels;
    info->num_orders = meta->num_positions;
    info->num_samples = meta->num_instruments;
    info->current_pattern = state->pattern;
    info->current_row = state->row;
    info->current_order = state->position;
    info->rows_per_pattern = AON_ROWS_PER_PATTERN;
    info->channels_synchronized = 1;

    strncpy(info->module_type, "aon", sizeof(info->module_type) - 1);
    info->module_type[sizeof(info->module_type) - 1] = '\0';

    strncpy(info->song_name, meta->song_name, sizeof(info->song_name) - 1);
    info->song_name[sizeof(info->song_name) - 1] = '\0';

    int copy_count = meta->num_instruments < 32 ? meta->num_instruments : 32;
    for (int i = 0; i < copy_count; ++i) {
        const char* name = aon_song_get_instrument_name(data->song, (uint8_t)i);
        if (name) {
            strncpy(info->sample_names[i], name, sizeof(info->sample_names[0]) - 1);
            info->sample_names[i][sizeof(info->sample_names[0]) - 1] = '\0';
        }
    }

    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static int aon_get_pattern_cell(void* user_data, int pattern, int row, int channel, RVPatternCell* cell) {
    AonPluginData* data = (AonPluginData*)user_data;

    if (data->song == nullptr) {
        return -1;
    }

    AonPatternCell aon_cell;
    if (!aon_song_get_pattern_cell(data->song, (uint8_t)pattern, (uint8_t)row, (uint8_t)channel, &aon_cell)) {
        return -1;
    }

    cell->note = aon_cell.note;
    cell->instrument = aon_cell.instrument;
    cell->volume = 0;
    cell->effect = aon_cell.effect;
    cell->effect_param = aon_cell.effect_arg;

    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static int aon_get_pattern_num_rows(void* user_data, int pattern) {
    AonPluginData* data = (AonPluginData*)user_data;

    if (data->song == nullptr || pattern < 0) {
        return 0;
    }

    return AON_ROWS_PER_PATTERN;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint32_t aon_get_scope_data(void* user_data, int channel, float* buffer, uint32_t num_samples) {
    AonPluginData* data = (AonPluginData*)user_data;
    if (data == nullptr || data->song == nullptr || buffer == nullptr) {
        return 0;
    }

    if (!data->scope_enabled) {
        aon_song_enable_scope_capture(data->song, 1);
        data->scope_enabled = 1;
    }

    return aon_song_get_scope_data(data->song, channel, buffer, num_samples);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint32_t aon_get_scope_channel_names(void* user_data, const char** names, uint32_t max_channels) {
    AonPluginData* data = (AonPluginData*)user_data;
    static const char* s_names[] = {
        "Voice 0", "Voice 1", "Voice 2", "Voice 3",
        "Voice 4", "Voice 5", "Voice 6", "Voice 7",
    };

    uint32_t count = AON_MAX_CHANNELS;
    if (data != nullptr && data->song != nullptr) {
        const AonSongMetadata* meta = aon_song_get_metadata(data->song);
        if (meta) {
            count = meta->num_channels;
        }
    }

    if (count > max_channels) {
        count = max_channels;
    }

    for (uint32_t i = 0; i < count; i++) {
        names[i] = s_names[i];
    }

    return count;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static RVPlaybackPlugin g_aon_plugin = {
    RV_PLAYBACK_PLUGIN_API_VERSION,
    "art_of_noise",
    "0.0.1",
    "aon_player 1.0",
    aon_probe_can_play,
    aon_supported_extensions,
    aon_create,
    aon_destroy,
    aon_event,
    aon_open,
    aon_close,
    aon_read_data,
    aon_seek,
    aon_metadata,
    aon_static_init,
    NULL, // settings_updated

    // Tracker visualization API
    aon_get_tracker_info,
    aon_get_pattern_cell,
    aon_get_pattern_num_rows,

    // Scope visualization API
    aon_get_scope_data,
    NULL, // static_destroy
    aon_get_scope_channel_names,
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RV_EXPORT RVPlaybackPlugin* rv_playback_plugin(void) {
    return &g_aon_plugin;
}
