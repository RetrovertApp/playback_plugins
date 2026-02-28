///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ASAP Playback Plugin
//
// Implements RVPlaybackPlugin interface for Atari 8-bit music formats (SAP, CMC, RMT, TMC, etc.)
// Based on ASAP (Another Slight Atari Player) by Piotr Fusik.
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// C11 nullptr compatibility
#ifndef nullptr
#define nullptr ((void*)0)
#endif

#include <retrovert/io.h>
#include <retrovert/log.h>
#include <retrovert/metadata.h>
#include <retrovert/playback.h>
#include <retrovert/service.h>

#include "asap.h"

#include <stdlib.h>
#include <string.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define OUTPUT_SAMPLE_RATE 48000 // Match system audio rate

static const RVIo* g_io_api = nullptr;
const RVLog* g_rv_log = nullptr;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct AsapReplayerData {
    ASAP* asap;
    int current_subsong;
    int num_subsongs;
} AsapReplayerData;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static const char* asap_supported_extensions(void) {
    return "sap,cmc,cm3,cmr,cms,dmc,dlt,mpt,mpd,rmt,tmc,tm8,tm2,fc";
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void* asap_create(const RVService* service_api) {
    AsapReplayerData* data = malloc(sizeof(AsapReplayerData));
    if (data == nullptr) {
        return nullptr;
    }
    memset(data, 0, sizeof(AsapReplayerData));

    data->asap = ASAP_New();
    if (data->asap == nullptr) {
        free(data);
        return nullptr;
    }

    // Note: Sample rate is set in asap_open() after loading the file
    g_io_api = RVService_get_io(service_api, RV_IO_API_VERSION);

    return data;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static int asap_destroy(void* user_data) {
    AsapReplayerData* data = (AsapReplayerData*)user_data;

    if (data->asap) {
        ASAP_Delete(data->asap);
    }

    free(data);
    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static int asap_open(void* user_data, const char* url, uint32_t subsong, const RVService* service_api) {
    (void)service_api;

    RVIoReadUrlResult read_res;

    if ((read_res = RVIo_read_url_to_memory(g_io_api, url)).data == nullptr) {
        rv_error("Failed to load %s to memory", url);
        return -1;
    }

    AsapReplayerData* data = (AsapReplayerData*)user_data;

    if (!ASAP_Load(data->asap, url, read_res.data, (int)read_res.data_size)) {
        rv_error("ASAP failed to parse %s", url);
        RVIo_free_url_to_memory(g_io_api, read_res.data);
        return -1;
    }

    // Set sample rate to match system audio
    ASAP_SetSampleRate(data->asap, OUTPUT_SAMPLE_RATE);

    const ASAPInfo* info = ASAP_GetInfo(data->asap);
    data->num_subsongs = ASAPInfo_GetSongs(info);

    // Use default subsong if none specified
    if (subsong == 0) {
        subsong = (uint32_t)ASAPInfo_GetDefaultSong(info);
    }
    data->current_subsong = (int)subsong;

    // Get duration for this subsong (-1 means unknown/infinite)
    int duration = ASAPInfo_GetDuration(info, data->current_subsong);

    rv_info("ASAP: Playing subsong %d, duration=%d ms, channels=%d, NTSC=%d", data->current_subsong, duration,
            ASAPInfo_GetChannels(info), ASAPInfo_IsNtsc(info) ? 1 : 0);

    if (!ASAP_PlaySong(data->asap, data->current_subsong, duration)) {
        rv_error("ASAP failed to play subsong %d of %s", data->current_subsong, url);
        RVIo_free_url_to_memory(g_io_api, read_res.data);
        return -1;
    }

    RVIo_free_url_to_memory(g_io_api, read_res.data);
    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void asap_close(void* user_data) {
    // ASAP doesn't have an explicit close/unload - loading a new file replaces the old one
    (void)user_data;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static RVProbeResult asap_probe_can_play(uint8_t* data, uint64_t data_size, const char* url, uint64_t total_size) {
    (void)total_size;

    // Check for SAP format header (need at least 4 bytes)
    if (data_size >= 4 && data[0] == 'S' && data[1] == 'A' && data[2] == 'P' && data[3] == '\r') {
        return RVProbeResult_Supported;
    }

    // For other formats, rely on file extension via ASAPInfo
    if (url != nullptr && ASAPInfo_IsOurFile(url)) {
        return RVProbeResult_Supported;
    }

    return RVProbeResult_Unsupported;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static RVReadInfo asap_read_data(void* user_data, RVReadData dest) {
    AsapReplayerData* data = (AsapReplayerData*)user_data;

    const ASAPInfo* info = ASAP_GetInfo(data->asap);
    int channels = ASAPInfo_GetChannels(info);

    // Calculate how many S16 frames fit in the output buffer
    uint32_t max_frames = dest.channels_output_max_bytes_size / (sizeof(int16_t) * (uint32_t)channels);

    // Generate S16 samples directly to output buffer
    int bytes_needed = (int)(max_frames * (uint32_t)channels * sizeof(int16_t));
    int bytes_generated
        = ASAP_Generate(data->asap, (uint8_t*)dest.channels_output, bytes_needed, ASAPSampleFormat_S16_L_E);

    int frames_generated = bytes_generated / ((int)sizeof(int16_t) * channels);

    // Report actual channel count - host handles mono->stereo upmix
    RVAudioFormat format = { RVAudioStreamFormat_S16, (uint32_t)channels, OUTPUT_SAMPLE_RATE };
    RVReadStatus status = (frames_generated == 0) ? RVReadStatus_Finished : RVReadStatus_Ok;
    return (RVReadInfo) { format, (uint16_t)frames_generated, status};
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static int64_t asap_seek(void* user_data, int64_t ms) {
    AsapReplayerData* data = (AsapReplayerData*)user_data;

    if (ASAP_Seek(data->asap, (int)ms)) {
        return ms;
    }
    return -1;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static int asap_metadata(const char* url, const RVService* service_api) {
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

    ASAPInfo* info = ASAPInfo_New();
    if (info == nullptr) {
        RVIo_free_url_to_memory(io_api, read_res.data);
        return -1;
    }

    if (!ASAPInfo_Load(info, url, read_res.data, (int)read_res.data_size)) {
        ASAPInfo_Delete(info);
        RVIo_free_url_to_memory(io_api, read_res.data);
        return -1;
    }

    RVMetadataId index = RVMetadata_create_url(metadata_api, url);

    const char* title = ASAPInfo_GetTitle(info);
    const char* author = ASAPInfo_GetAuthor(info);

    if (title && title[0] != '\0') {
        RVMetadata_set_tag(metadata_api, index, RV_METADATA_TITLE_TAG, title);
    } else {
        // Use filename as title if no title in file
        RVMetadata_set_tag(metadata_api, index, RV_METADATA_TITLE_TAG, ASAPInfo_GetTitleOrFilename(info));
    }

    if (author && author[0] != '\0') {
        RVMetadata_set_tag(metadata_api, index, RV_METADATA_ARTIST_TAG, author);
    }

    RVMetadata_set_tag(metadata_api, index, RV_METADATA_SONGTYPE_TAG, "ASAP (Atari 8-bit)");

    // Get duration of default song
    int default_song = ASAPInfo_GetDefaultSong(info);
    int duration_ms = ASAPInfo_GetDuration(info, default_song);
    if (duration_ms > 0) {
        RVMetadata_set_tag_f64(metadata_api, index, RV_METADATA_LENGTH_TAG, duration_ms / 1000.0);
    }

    // Add subsongs if more than one
    int num_songs = ASAPInfo_GetSongs(info);
    if (num_songs > 1) {
        for (int i = 0; i < num_songs; i++) {
            int subsong_duration = ASAPInfo_GetDuration(info, i);
            float length = subsong_duration > 0 ? subsong_duration / 1000.0f : 0.0f;
            RVMetadata_add_subsong(metadata_api, index, i, "", length);
        }
    }

    ASAPInfo_Delete(info);
    RVIo_free_url_to_memory(io_api, read_res.data);

    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void asap_event(void* user_data, uint8_t* event_data, uint64_t len) {
    AsapReplayerData* data = (AsapReplayerData*)user_data;

    if (data->asap == nullptr || event_data == nullptr || len < 8) {
        return;
    }

    // Position info (not tracking pattern/row for ASAP)
    event_data[7] = 0;
    event_data[6] = 0;
    event_data[5] = 0;
    event_data[4] = 0;

    // VU meters from POKEY channel volumes (4 channels, 0-15 scaled to 0-255)
    for (int i = 0; i < 4; i++) {
        int vol = ASAP_GetPokeyChannelVolume(data->asap, i);
        event_data[3 - i] = (uint8_t)(vol * 17); // Scale 0-15 to 0-255
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void asap_static_init(const RVService* service_api) {
    g_rv_log = RVService_get_log(service_api, RV_LOG_API_VERSION);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static RVPlaybackPlugin g_asap_plugin = {
    RV_PLAYBACK_PLUGIN_API_VERSION,
    "asap",
    "0.0.1",
    ASAPInfo_VERSION,
    asap_probe_can_play,
    asap_supported_extensions,
    asap_create,
    asap_destroy,
    asap_event,
    asap_open,
    asap_close,
    asap_read_data,
    asap_seek,
    asap_metadata,
    asap_static_init,
    nullptr, // settings_updated
    nullptr, // get_tracker_info (not a tracker format)
    nullptr, // get_pattern_cell
    nullptr, // get_pattern_num_rows
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RV_EXPORT RVPlaybackPlugin* rv_playback_plugin(void) {
    return &g_asap_plugin;
}
