///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Ixalance Playback Plugin
//
// Implements RVPlaybackPlugin interface for IXS (Impulse Tracker eXtendable Sequencer) files using
// webixs by Juergen Wothke, reverse-engineered from Shortcut Software's player.
// Audio output: Stereo S16 at 44100 Hz.
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifdef _WIN32
#define strcasecmp _stricmp
#else
#include <strings.h>
#endif

#include "PlayerIXS.h"

extern "C" {
#include <retrovert/io.h>
#include <retrovert/log.h>
#include <retrovert/metadata.h>
#include <retrovert/playback.h>
#include <retrovert/service.h>
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define IXS_SAMPLE_RATE 44100
#define IXS_CHANNELS 2

RV_PLUGIN_USE_IO_API();
RV_PLUGIN_USE_METADATA_API();
RV_PLUGIN_USE_LOG_API();

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct IxalanceData {
    IXS::PlayerIXS* player;
    uint8_t* file_data;
    bool playing;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static const char* ixalance_supported_extensions(void) {
    return "ixs";
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void ixalance_static_init(const RVService* service_api) {
    rv_init_log_api(service_api);
    rv_init_io_api(service_api);
    rv_init_metadata_api(service_api);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void* ixalance_create(const RVService* service_api) {
    (void)service_api;

    IxalanceData* data = (IxalanceData*)calloc(1, sizeof(IxalanceData));
    if (!data) {
        return nullptr;
    }

    data->player = IXS::IXS__PlayerIXS__createPlayer_00405d90(IXS_SAMPLE_RATE);
    if (!data->player) {
        free(data);
        return nullptr;
    }

    return data;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static int ixalance_destroy(void* user_data) {
    IxalanceData* data = (IxalanceData*)user_data;

    if (data->player) {
        (*data->player->vftable->delete0)(data->player);
    }
    if (data->file_data) {
        rv_io_free_url_to_memory(data->file_data);
    }
    free(data);
    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static RVProbeResult ixalance_probe_can_play(uint8_t* probe_data, uint64_t data_size, const char* url,
                                             uint64_t total_size) {
    (void)total_size;

    // Check for IXS! magic at the beginning of the file
    if (data_size >= 4) {
        if (probe_data[0] == 'I' && probe_data[1] == 'X' && probe_data[2] == 'S' && probe_data[3] == '!') {
            return RVProbeResult_Supported;
        }
    }

    // Fall back to extension check
    if (url != nullptr) {
        const char* dot = strrchr(url, '.');
        if (dot != nullptr && strcasecmp(dot, ".ixs") == 0) {
            return RVProbeResult_Unsure;
        }
    }

    return RVProbeResult_Unsupported;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static int ixalance_open(void* user_data, const char* url, uint32_t subsong, const RVService* service_api) {
    (void)subsong;
    (void)service_api;

    IxalanceData* data = (IxalanceData*)user_data;

    // Clean up previous state
    if (data->file_data) {
        rv_io_free_url_to_memory(data->file_data);
        data->file_data = nullptr;
    }
    data->playing = false;

    // Destroy and recreate player for clean state
    if (data->player) {
        (*data->player->vftable->delete0)(data->player);
    }
    data->player = IXS::IXS__PlayerIXS__createPlayer_00405d90(IXS_SAMPLE_RATE);
    if (!data->player) {
        rv_error("IXS: Failed to create player");
        return -1;
    }

    RVIoReadUrlResult read_res;
    if ((read_res = rv_io_read_url_to_memory(url)).data == nullptr) {
        rv_error("IXS: Failed to load %s", url);
        return -1;
    }

    data->file_data = (uint8_t*)read_res.data;

    // Load the IXS file data
    char result = (*data->player->vftable->loadIxsFileData)(
        data->player, data->file_data, (uint32_t)read_res.data_size, nullptr, nullptr, nullptr);

    if (result != 0) {
        rv_error("IXS: Failed to load file data from %s", url);
        rv_io_free_url_to_memory(data->file_data);
        data->file_data = nullptr;
        return -1;
    }

    // Initialize audio output
    (*data->player->vftable->initAudioOut)(data->player);
    data->playing = true;

    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void ixalance_close(void* user_data) {
    IxalanceData* data = (IxalanceData*)user_data;

    data->playing = false;

    if (data->file_data) {
        rv_io_free_url_to_memory(data->file_data);
        data->file_data = nullptr;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static RVReadInfo ixalance_read_data(void* user_data, RVReadData dest) {
    IxalanceData* data = (IxalanceData*)user_data;

    RVAudioFormat format = {RVAudioStreamFormat_S16, IXS_CHANNELS, IXS_SAMPLE_RATE};

    if (!data->playing || !data->player) {
        return (RVReadInfo){format, 0, RVReadStatus_Finished};
    }

    // Check if song has ended
    if ((*data->player->vftable->isSongEnd)(data->player)) {
        data->playing = false;
        return (RVReadInfo){format, 0, RVReadStatus_Finished};
    }

    // Generate one block of audio
    (*data->player->vftable->genAudio)(data->player);

    // Get the generated audio buffer and length
    uint8_t* audio_buf = (*data->player->vftable->getAudioBuffer)(data->player);
    uint32_t num_frames = (*data->player->vftable->getAudioBufferLen)(data->player);

    if (!audio_buf || num_frames == 0) {
        return (RVReadInfo){format, 0, RVReadStatus_Ok};
    }

    // Calculate how many frames we can fit in the output buffer
    uint32_t bytes_per_frame = sizeof(int16_t) * IXS_CHANNELS;
    uint32_t max_frames = dest.channels_output_max_bytes_size / bytes_per_frame;
    uint32_t frames_to_copy = num_frames < max_frames ? num_frames : max_frames;

    memcpy(dest.channels_output, audio_buf, frames_to_copy * bytes_per_frame);

    // Check if song ended after generating
    if ((*data->player->vftable->isSongEnd)(data->player)) {
        data->playing = false;
        return (RVReadInfo){format, frames_to_copy, RVReadStatus_Finished};
    }

    return (RVReadInfo){format, frames_to_copy, RVReadStatus_Ok};
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static int64_t ixalance_seek(void* user_data, int64_t ms) {
    (void)user_data;
    (void)ms;
    return 0; // No seeking support
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static int ixalance_metadata(const char* url, const RVService* service_api) {
    (void)service_api;

    RVIoReadUrlResult read_res;
    if ((read_res = rv_io_read_url_to_memory(url)).data == nullptr) {
        return -1;
    }

    uint8_t* file_data = (uint8_t*)read_res.data;

    RVMetadataId id = rv_metadata_create_url(url);
    rv_metadata_set_tag(id, RV_METADATA_SONGTYPE_TAG, "IXS");

    // Try to extract song title from the IXS file
    // IXS! magic (4 bytes) + itHeadOffset (4) + offset1 (4) + offset2 (4) + packedLen (4) + outputVolume (4) = 24
    // Then 32 bytes of song title
    if (read_res.data_size >= 56) {
        uint32_t magic = *(uint32_t*)file_data;
        if (magic == 0x21535849) { // "IXS!"
            char title[33];
            memcpy(title, file_data + 24, 32);
            title[32] = '\0';
            // Trim trailing spaces/nulls
            for (int i = 31; i >= 0; i--) {
                if (title[i] == ' ' || title[i] == '\0') {
                    title[i] = '\0';
                } else {
                    break;
                }
            }
            if (title[0] != '\0') {
                rv_metadata_set_tag(id, RV_METADATA_TITLE_TAG, title);
            }
        }
    }

    rv_io_free_url_to_memory(read_res.data);
    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void ixalance_event(void* user_data, uint8_t* event_data, uint64_t len) {
    (void)user_data;
    (void)event_data;
    (void)len;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static RVPlaybackPlugin g_ixalance_plugin = {
    RV_PLAYBACK_PLUGIN_API_VERSION,
    "ixalance",
    "0.0.1",
    "webixs (Juergen Wothke)",
    ixalance_probe_can_play,
    ixalance_supported_extensions,
    ixalance_create,
    ixalance_destroy,
    ixalance_event,
    ixalance_open,
    ixalance_close,
    ixalance_read_data,
    ixalance_seek,
    ixalance_metadata,
    ixalance_static_init,
    nullptr, // settings_updated
    nullptr, // get_tracker_info
    nullptr, // get_pattern_cell
    nullptr, // get_pattern_num_rows
    nullptr, // get_scope_data
    nullptr, // static_destroy
    nullptr, // get_scope_channel_names
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

extern "C" RV_EXPORT RVPlaybackPlugin* rv_playback_plugin(void) {
    return &g_ixalance_plugin;
}
