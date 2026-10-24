///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Nuade Playback Plugin
//
// Implements RVPlaybackPlugin on top of nuade (https://github.com/emoon/nuade): UADE's Amiga player routines ported
// from 68000 assembly to C. Every player compiled into nuade is reachable through this one plugin.
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
#include <retrovert/settings.h>

#include "nuade_instance.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define FREQ NUADE_PLUGIN_FREQ
#define CHANNELS 2

// Metadata scans a song to find its length; stop at this much so an endless song cannot hold the scan.
#define MAX_SCAN_FRAMES ((uint64_t)FREQ * 30 * 60)
// The same bound for a scan, which counts ticks: 30 minutes at the 50 Hz of the usual Amiga player.
#define MAX_SCAN_TICKS (50 * 60 * 30)

#define EXTENSIONS_CAP 4096

RV_PLUGIN_USE_IO_API();
RV_PLUGIN_USE_METADATA_API();
RV_PLUGIN_USE_LOG_API();

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Host io for nuade: instrument and sample files a song names are read through the host, like the song itself.

static bool host_io_exists(struct NuadeIoPrivate* self, const char* url) {
    (void)self;
    return rv_io_exists(url);
}

static NuadeIoReadResult host_io_read(struct NuadeIoPrivate* self, const char* url) {
    (void)self;
    RVIoReadUrlResult res = rv_io_read_url_to_memory(url);
    return (NuadeIoReadResult) { res.data, res.data_size };
}

static void host_io_free(struct NuadeIoPrivate* self, void* memory) {
    (void)self;
    rv_io_free_url_to_memory(memory);
}

// The host io cannot list a directory.
static const NuadeIo g_host_io = { NUADE_IO_API_VERSION, nullptr, host_io_exists, host_io_read, host_io_free, nullptr };

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static NuadeSong* open_song(const char* url) {
    RVIoReadUrlResult read_res;

    if ((read_res = rv_io_read_url_to_memory(url)).data == nullptr) {
        rv_error("Failed to load %s to memory", url);
        return nullptr;
    }

    NuadeOpenParams params;
    memset(&params, 0, sizeof(params));
    params.path = url;
    params.data = read_res.data;
    params.size = (size_t)read_res.data_size;
    params.io = &g_host_io;

    NuadeSongResult res = nuade_open_ex(&params);
    rv_io_free_url_to_memory(read_res.data);

    if (res.song == nullptr) {
        rv_error("Failed to parse %s: %s", url, res.error.message);
        return nullptr;
    }
    return res.song;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool has_extension(const char* list, const char* ext, size_t len) {
    for (const char* p = list; *p;) {
        const char* end = strchr(p, ',');
        size_t n = end ? (size_t)(end - p) : strlen(p);
        if (n == len && memcmp(p, ext, len) == 0) {
            return true;
        }
        p += n + (end ? 1 : 0);
    }
    return false;
}

static const char* nuplug_supported_extensions(void) {
    static char list[EXTENSIONS_CAP];

    if (list[0] != '\0') {
        return list;
    }

    size_t used = 0;
    for (uint32_t i = 0; i < nuade_player_count(); i++) {
        const char* p = nuade_player_get(i)->extensions;
        while (p && *p) {
            const char* end = strchr(p, ',');
            size_t n = end ? (size_t)(end - p) : strlen(p);
            size_t need = n + (used ? 1 : 0);
            if (n > 0 && !has_extension(list, p, n) && used + need < sizeof(list)) {
                if (used) {
                    list[used++] = ',';
                }
                memcpy(&list[used], p, n);
                used += n;
                list[used] = '\0';
            }
            p = end ? end + 1 : p + n;
        }
    }
    return list;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static RVProbeResult nuplug_probe_can_play(uint8_t* data, uint64_t data_size, const char* url, uint64_t total_size) {
    (void)total_size;

    RVProbeResult result = RVProbeResult_Unsupported;

    for (uint32_t i = 0; i < nuade_player_count(); i++) {
        switch (nuade_player_get(i)->probe(data, (size_t)data_size, url)) {
            case NuadeProbe_Supported:
                return RVProbeResult_Supported;
            case NuadeProbe_Unsure:
                result = RVProbeResult_Unsure;
                break;
            case NuadeProbe_Unsupported:
                break;
        }
    }
    return result;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void* nuplug_create(const RVService* service_api) {
    (void)service_api;
    return calloc(1, sizeof(NuplugInstance));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void nuplug_close_song(NuplugInstance* inst) {
    nuplug_viz_reset(inst);
    if (inst->song) {
        nuade_close(inst->song);
        inst->song = nullptr;
    }
    inst->ended = false;
}

static int nuplug_destroy(void* user_data) {
    NuplugInstance* inst = (NuplugInstance*)user_data;

    nuplug_close_song(inst);
    free(inst);
    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static int nuplug_open_url(void* user_data, const char* url, uint32_t subsong, const RVService* service_api) {
    (void)service_api;
    NuplugInstance* inst = (NuplugInstance*)user_data;

    nuplug_close_song(inst);

    inst->song = open_song(url);
    if (inst->song == nullptr) {
        return -1;
    }

    const NuadeSongInfo* info = nuade_info(inst->song);
    uint32_t start = subsong < info->subsong_count ? subsong : info->default_subsong;

    if (!nuade_start(inst->song, start, FREQ)) {
        rv_error("Failed to start subsong %u of %s", start, url);
        nuplug_close_song(inst);
        return -1;
    }
    return 0;
}

static void nuplug_close_url(void* user_data) {
    nuplug_close_song((NuplugInstance*)user_data);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static RVReadInfo nuplug_read_data(void* user_data, RVReadData dest) {
    NuplugInstance* inst = (NuplugInstance*)user_data;
    RVAudioFormat format = { RVAudioStreamFormat_F32, CHANNELS, FREQ };

    if (inst->song == nullptr || inst->ended) {
        return (RVReadInfo) { format, 0, RVReadStatus_Finished };
    }

    uint32_t capacity_frames = dest.channels_output_max_bytes_size / (sizeof(float) * CHANNELS);
    uint32_t max_frames = dest.info.frame_count < capacity_frames ? dest.info.frame_count : capacity_frames;

    NuadeRenderResult res = nuade_render(inst->song, (float*)dest.channels_output, nullptr, max_frames);

    // The frames before the end are still played; the read after them finishes the stream.
    if (res.flags & NuadeRenderFlags_Ended) {
        inst->ended = true;
    }

    RVReadStatus status = (inst->ended && res.frames == 0) ? RVReadStatus_Finished : RVReadStatus_Ok;
    return (RVReadInfo) { format, res.frames, status };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static int64_t nuplug_seek_ms(void* user_data, int64_t ms) {
    NuplugInstance* inst = (NuplugInstance*)user_data;

    if (inst->song == nullptr || ms < 0) {
        return 0;
    }

    // nuade restarts the song and renders silently up to the frame, so a seek costs as much as playing that far.
    inst->ended = !nuade_seek(inst->song, (uint64_t)ms * FREQ / 1000);
    return (int64_t)(nuade_position(inst->song) * 1000 / FREQ);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void set_tag_if_present(RVMetadataId index, const char* tag, const char* value) {
    if (value != nullptr && value[0] != '\0') {
        rv_metadata_set_tag(index, tag, value);
    }
}

// Length of one pass of a subsong in seconds, 0 when it did not loop or end within MAX_SCAN_FRAMES.
//
// A tick player is scanned without mixing, which costs a fraction of the dry run that renders the audio; a
// self-rendering player has no scan and gets the dry run.
static double subsong_seconds(NuadeSong* song, uint32_t subsong) {
    NuadeScan* scan = nuade_scan(song, subsong, FREQ, MAX_SCAN_TICKS);
    if (scan != nullptr) {
        const double seconds = scan->truncated ? 0.0 : (double)scan->frames / FREQ;
        nuade_scan_free(scan);
        return seconds;
    }

    NuadeDuration duration;
    if (!nuade_duration(song, subsong, FREQ, MAX_SCAN_FRAMES, &duration) || duration.capped) {
        return 0.0;
    }
    return (double)duration.frames / FREQ;
}

static int nuplug_metadata(const char* url, const RVService* service_api) {
    (void)service_api;

    NuadeSong* song = open_song(url);
    if (song == nullptr) {
        return -1;
    }

    const NuadeSongInfo* info = nuade_info(song);

    const char* format = nuade_meta(song, NUADE_META_FORMAT);
    if (format == nullptr) {
        format = nuade_player(song)->name;
    }

    RVMetadataId index = rv_metadata_create_url(url);

    set_tag_if_present(index, RV_METADATA_TITLE_TAG, nuade_meta(song, NUADE_META_TITLE));
    set_tag_if_present(index, RV_METADATA_ARTIST_TAG, nuade_meta(song, NUADE_META_ARTIST));
    set_tag_if_present(index, RV_METADATA_SONGTYPE_TAG, format);
    set_tag_if_present(index, RV_METADATA_AUTHORINGTOOL_TAG, nuade_meta(song, NUADE_META_TOOL));
    set_tag_if_present(index, RV_METADATA_MESSAGE_TAG, nuade_meta(song, NUADE_META_MESSAGE));
    rv_metadata_set_tag_f64(index, RV_METADATA_LENGTH_TAG, subsong_seconds(song, info->default_subsong));

    for (uint32_t i = 0, c = nuade_instrument_count(song); i < c; ++i) {
        const char* name = nuade_instrument_name(song, i, nullptr);
        rv_metadata_add_instrument(index, name ? name : "");
    }

    if (info->subsong_count > 1) {
        for (uint32_t i = 0; i < info->subsong_count; ++i) {
            rv_metadata_add_subsong(index, i, "", (float)subsong_seconds(song, i));
        }
    }

    nuade_close(song);
    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Position and per-channel levels in the layout the host's event call expects, taken from the snapshot of the frame
// that was last rendered.
static void nuplug_event(void* user_data, uint8_t* data, uint64_t len) {
    NuplugInstance* inst = (NuplugInstance*)user_data;

    if (inst->song == nullptr || data == nullptr || len < 8) {
        return;
    }

    NuadeVoiceView voices[4];
    memset(voices, 0, sizeof(voices));
    NuadeSnapshotResult snap = nuade_snapshot(inst->song, nuade_position(inst->song), voices, 4);

    data[7] = snap.valid ? (uint8_t)(snap.snapshot.position.order & 0xFF) : 0;
    data[6] = snap.valid ? (uint8_t)(snap.snapshot.position.row & 0xFF) : 0;
    data[5] = 0;
    data[4] = 0;

    for (int i = 0; i < 4; i++) {
        float level = (snap.valid && voices[i].active) ? voices[i].volume : 0.0f;
        data[3 - i] = (uint8_t)(level < 0.0f ? 0 : (level > 1.0f ? 255 : level * 255.0f));
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void nuplug_static_init(const RVService* service_api) {
    rv_init_log_api(service_api);
    rv_init_io_api(service_api);
    rv_init_metadata_api(service_api);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static RVPlaybackPlugin g_nuade_plugin = {
    RV_PLAYBACK_PLUGIN_API_VERSION,
    "nuade",
    "0.0.1",
    "nuade b59fcd5",
    nuplug_probe_can_play,
    nuplug_supported_extensions,
    nuplug_create,
    nuplug_destroy,
    nuplug_event,
    nuplug_open_url,
    nuplug_close_url,
    nuplug_read_data,
    nuplug_seek_ms,
    nuplug_metadata,
    nuplug_static_init,
    NULL, // settings_updated
    NULL, // static_destroy
    nuplug_viz_info,
    nuplug_viz_columns,
    nuplug_viz_pattern_channels,
    nuplug_viz_scope_channels,
    nuplug_viz_position,
    nuplug_viz_channel_rows,
    nuplug_viz_cells,
    nuplug_viz_scope_enable,
    nuplug_viz_scope_samples,
    nuplug_viz_vu_levels,
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RV_EXPORT RVPlaybackPlugin* rv_playback_plugin(void) {
    return &g_nuade_plugin;
}
