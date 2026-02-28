///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Klystrack Playback Plugin
//
// Implements RVPlaybackPlugin interface for Klystrack music files (.kt) using
// the klystron sound engine via the KSND wrapper API.
// Audio output: Stereo S16 at 44100 Hz, converted to F32 for the host.
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#ifndef nullptr
#define nullptr ((void*)0)
#endif

#include <retrovert/io.h>
#include <retrovert/log.h>
#include <retrovert/metadata.h>
#include <retrovert/playback.h>
#include <retrovert/service.h>

#include "ksnd.h"

#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define strcasecmp _stricmp
#else
#include <strings.h>
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define KT_SAMPLE_RATE 44100
#define KT_CHANNELS 2

static const RVIo* g_io_api = nullptr;
const RVLog* g_rv_log = nullptr;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct KlystrackReplayerData {
    KPlayer* player;
    KSong* song;
    int length_ms;
    int elapsed_frames;
    bool finished;
    bool scope_enabled;
} KlystrackReplayerData;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static const char* klystrack_supported_extensions(void) {
    return "kt";
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void klystrack_static_init(const RVService* service_api) {
    g_rv_log = RVService_get_log(service_api, RV_LOG_API_VERSION);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void* klystrack_create(const RVService* service_api) {
    KlystrackReplayerData* data = malloc(sizeof(KlystrackReplayerData));
    if (data == nullptr) {
        return nullptr;
    }
    memset(data, 0, sizeof(KlystrackReplayerData));

    g_io_api = RVService_get_io(service_api, RV_IO_API_VERSION);

    data->player = KSND_CreatePlayerUnregistered(KT_SAMPLE_RATE);
    if (data->player == nullptr) {
        rv_error("Klystrack: Failed to create player");
        free(data);
        return nullptr;
    }

    // Set quality to 2x oversampling for decent sound
    KSND_SetPlayerQuality(data->player, 1);

    return data;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static int klystrack_destroy(void* user_data) {
    KlystrackReplayerData* data = (KlystrackReplayerData*)user_data;

    if (data->song) {
        KSND_FreeSong(data->song);
    }
    if (data->player) {
        KSND_FreePlayer(data->player);
    }
    free(data);
    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static RVProbeResult klystrack_probe_can_play(uint8_t* probe_data, uint64_t data_size, const char* url,
                                              uint64_t total_size) {
    (void)probe_data;
    (void)data_size;
    (void)total_size;

    // No known magic bytes for .kt files - use extension
    if (url != nullptr) {
        const char* dot = strrchr(url, '.');
        if (dot != nullptr && strcasecmp(dot, ".kt") == 0) {
            return RVProbeResult_Unsure;
        }
    }

    return RVProbeResult_Unsupported;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static int klystrack_open(void* user_data, const char* url, uint32_t subsong, const RVService* service_api) {
    (void)subsong;
    (void)service_api;

    KlystrackReplayerData* data = (KlystrackReplayerData*)user_data;

    // Free previous song
    if (data->song) {
        KSND_Stop(data->player);
        KSND_FreeSong(data->song);
        data->song = nullptr;
    }

    RVIoReadUrlResult read_res;
    if ((read_res = RVIo_read_url_to_memory(g_io_api, url)).data == nullptr) {
        rv_error("Klystrack: Failed to load %s", url);
        return -1;
    }

    data->song = KSND_LoadSongFromMemory(data->player, read_res.data, (int)read_res.data_size);
    RVIo_free_url_to_memory(g_io_api, read_res.data);

    if (data->song == nullptr) {
        rv_error("Klystrack: Failed to parse %s", url);
        return -1;
    }

    // Disable looping (KSND_SetLooping is inverted: 1 = disable loop)
    KSND_SetLooping(data->player, 1);

    // Get duration
    int song_length = KSND_GetSongLength(data->song);
    data->length_ms = KSND_GetPlayTime(data->song, song_length);

    // Start playback
    KSND_PlaySong(data->player, data->song, 0);

    data->elapsed_frames = 0;
    data->finished = false;

    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void klystrack_close(void* user_data) {
    KlystrackReplayerData* data = (KlystrackReplayerData*)user_data;

    if (data->player) {
        KSND_Stop(data->player);
    }
    if (data->song) {
        KSND_FreeSong(data->song);
        data->song = nullptr;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static RVReadInfo klystrack_read_data(void* user_data, RVReadData dest) {
    KlystrackReplayerData* data = (KlystrackReplayerData*)user_data;
    RVAudioFormat format = { RVAudioStreamFormat_S16, KT_CHANNELS, KT_SAMPLE_RATE };

    if (data->song == nullptr || data->finished) {
        return (RVReadInfo) { format, 0, RVReadStatus_Finished, 0 };
    }

    uint32_t max_frames = dest.channels_output_max_bytes_size / (sizeof(int16_t) * KT_CHANNELS);

    // KSND_FillBuffer takes buffer length in bytes, outputs S16 directly to output buffer
    int bytes_to_fill = (int)(max_frames * KT_CHANNELS * sizeof(int16_t));
    KSND_FillBuffer(data->player, (int16_t*)dest.channels_output, bytes_to_fill);

    // Check if playback position indicates end
    int pos = KSND_GetPlayPosition(data->player);
    int song_len = KSND_GetSongLength(data->song);
    if (pos >= song_len && song_len > 0) {
        data->finished = true;
    }

    data->elapsed_frames += (int)max_frames;

    RVReadStatus status = data->finished ? RVReadStatus_Finished : RVReadStatus_Ok;
    return (RVReadInfo) { format, max_frames, status, 0 };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static int64_t klystrack_seek(void* user_data, int64_t ms) {
    KlystrackReplayerData* data = (KlystrackReplayerData*)user_data;

    if (data->song == nullptr || data->player == nullptr) {
        return -1;
    }

    // Restart from beginning and skip to position
    // klystrack doesn't have arbitrary seek, so restart
    KSND_Stop(data->player);
    KSND_PlaySong(data->player, data->song, 0);
    data->finished = false;

    // Skip audio to reach target time
    int frames_to_skip = (int)(ms * KT_SAMPLE_RATE / 1000);
    int16_t skip_buf[512 * KT_CHANNELS];
    while (frames_to_skip > 0) {
        int chunk = frames_to_skip > 512 ? 512 : frames_to_skip;
        KSND_FillBuffer(data->player, skip_buf, chunk * KT_CHANNELS * (int)sizeof(int16_t));
        frames_to_skip -= chunk;
    }

    data->elapsed_frames = (int)(ms * KT_SAMPLE_RATE / 1000);
    return ms;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static int klystrack_metadata(const char* url, const RVService* service_api) {
    const RVIo* io_api = RVService_get_io(service_api, RV_IO_API_VERSION);
    const RVMetadata* metadata_api = RVService_get_metadata(service_api, RV_METADATA_API_VERSION);

    if (io_api == nullptr || metadata_api == nullptr) {
        return -1;
    }

    RVIoReadUrlResult read_res;
    if ((read_res = RVIo_read_url_to_memory(io_api, url)).data == nullptr) {
        return -1;
    }

    RVMetadataId id = RVMetadata_create_url(metadata_api, url);
    RVMetadata_set_tag(metadata_api, id, RV_METADATA_SONGTYPE_TAG, "Klystrack");

    // Load song to extract metadata
    KPlayer* temp_player = KSND_CreatePlayerUnregistered(KT_SAMPLE_RATE);
    if (temp_player != nullptr) {
        KSong* song = KSND_LoadSongFromMemory(temp_player, read_res.data, (int)read_res.data_size);
        if (song != nullptr) {
            KSongInfo info;
            memset(&info, 0, sizeof(info));
            if (KSND_GetSongInfo(song, &info) != nullptr) {
                if (info.song_title != nullptr && info.song_title[0] != '\0') {
                    RVMetadata_set_tag(metadata_api, id, RV_METADATA_TITLE_TAG, info.song_title);
                }
            }

            int song_len = KSND_GetSongLength(song);
            int length_ms = KSND_GetPlayTime(song, song_len);
            if (length_ms > 0) {
                RVMetadata_set_tag_f64(metadata_api, id, RV_METADATA_LENGTH_TAG, (double)length_ms / 1000.0);
            }

            KSND_FreeSong(song);
        }
        KSND_FreePlayer(temp_player);
    }

    RVIo_free_url_to_memory(io_api, read_res.data);
    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void klystrack_event(void* user_data, uint8_t* event_data, uint64_t len) {
    (void)user_data;
    (void)event_data;
    (void)len;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint32_t klystrack_get_scope_data(void* user_data, int channel, float* buffer, uint32_t num_samples) {
    KlystrackReplayerData* data = (KlystrackReplayerData*)user_data;
    if (data == nullptr || data->player == nullptr || buffer == nullptr) {
        return 0;
    }

    if (!data->scope_enabled) {
        KSND_EnableScopeCapture(data->player, 1);
        data->scope_enabled = true;
    }

    return KSND_GetScopeData(data->player, channel, buffer, num_samples);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static RVPlaybackPlugin g_klystrack_plugin = {
    RV_PLAYBACK_PLUGIN_API_VERSION,
    "klystrack",
    "0.0.1",
    "klystron (kometbomb)",
    klystrack_probe_can_play,
    klystrack_supported_extensions,
    klystrack_create,
    klystrack_destroy,
    klystrack_event,
    klystrack_open,
    klystrack_close,
    klystrack_read_data,
    klystrack_seek,
    klystrack_metadata,
    klystrack_static_init,
    nullptr, // settings_updated
    nullptr, // get_tracker_info
    nullptr, // get_pattern_cell
    nullptr, // get_pattern_num_rows
    klystrack_get_scope_data,
    nullptr, // static_destroy
    nullptr, // get_scope_channel_names (no channel count API available)
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RV_EXPORT RVPlaybackPlugin* rv_playback_plugin(void) {
    return &g_klystrack_plugin;
}
