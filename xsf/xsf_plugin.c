///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// xSF Playback Plugin
//
// Implements RVPlaybackPlugin interface for PSF (Portable Sound Format) files.
// All xSF formats share the PSF container: 3-byte "PSF" magic + 1-byte version byte.
// The version byte identifies the target platform emulator.
//
// Currently supported:
//   PSF  (0x01) - PlayStation 1     via highly_experimental
//   PSF2 (0x02) - PlayStation 2     via highly_experimental
//   SSF  (0x11) - Sega Saturn       via highly_theoretical
//   DSF  (0x12) - Sega Dreamcast    via highly_theoretical
//   USF  (0x21) - Nintendo 64       via lazyusf2
//   GSF  (0x22) - Game Boy Advance  via viogsf
//   SNSF (0x23) - Super Nintendo    via snsf9x
//   2SF  (0x24) - Nintendo DS       via vio2sf
//   QSF  (0x41) - Capcom QSound     via highly_quixotic
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

#include "psflib.h"
#include "xsf_2sf.h"
#include "xsf_gsf.h"
#include "xsf_psf.h"
#include "xsf_psflib_bridge.h"
#include "xsf_qsf.h"
#include "xsf_snsf.h"
#include "xsf_ssf.h"
#include "xsf_usf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define strcasecmp _stricmp
#else
#include <strings.h>
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define XSF_DEFAULT_LENGTH_MS (180 * 1000) // 3 minutes default if no length tag

static const RVIo* g_io_api = nullptr;
const RVLog* g_rv_log = nullptr;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Emulator wrapper vtable - uniform interface for all xSF emulator backends

typedef struct XsfEmulator {
    void* (*create)(void);
    int (*load)(void* context, const uint8_t* exe, size_t exe_size, const uint8_t* reserved, size_t reserved_size);
    int (*start)(void* state, int psf_version);
    int (*post_load)(void* state); // Called after all psf_load callbacks complete (can be nullptr)
    int (*render)(void* state, int16_t* buffer, int frames);
    int (*sample_rate)(void* state);
    int (*seek_reset)(void* state);
    void (*destroy)(void* state);
    // Optional: for PSF2 which needs psf2fs
    void* (*get_psf2fs)(void* state);
    int (*load_psf2)(void* context, const uint8_t* exe, size_t exe_size, const uint8_t* reserved, size_t reserved_size);
    // Optional: per-emulator info callback for format-specific tags
    int (*info)(void* state, const char* name, const char* value);
} XsfEmulator;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// PSF/PSF2 emulator (highly_experimental)

static const XsfEmulator s_psf_emulator = {
    .create = xsf_psf_create,
    .load = xsf_psf_load,
    .start = xsf_psf_start,
    .post_load = nullptr,
    .render = xsf_psf_render,
    .sample_rate = xsf_psf_sample_rate,
    .seek_reset = xsf_psf_seek_reset,
    .destroy = xsf_psf_destroy,
    .get_psf2fs = xsf_psf_get_psf2fs,
    .load_psf2 = xsf_psf2_load,
    .info = nullptr,
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// 2SF emulator (vio2sf)

static const XsfEmulator s_2sf_emulator = {
    .create = xsf_2sf_create,
    .load = xsf_2sf_load,
    .start = xsf_2sf_start,
    .post_load = xsf_2sf_post_load,
    .render = xsf_2sf_render,
    .sample_rate = xsf_2sf_sample_rate,
    .seek_reset = xsf_2sf_seek_reset,
    .destroy = xsf_2sf_destroy,
    .get_psf2fs = nullptr,
    .load_psf2 = nullptr,
    .info = xsf_2sf_info,
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// USF emulator (lazyusf2)

static const XsfEmulator s_usf_emulator = {
    .create = xsf_usf_create,
    .load = xsf_usf_load,
    .start = xsf_usf_start,
    .post_load = nullptr,
    .render = xsf_usf_render,
    .sample_rate = xsf_usf_sample_rate,
    .seek_reset = xsf_usf_seek_reset,
    .destroy = xsf_usf_destroy,
    .get_psf2fs = nullptr,
    .load_psf2 = nullptr,
    .info = xsf_usf_info,
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// SSF/DSF emulator (highly_theoretical)

static const XsfEmulator s_ssf_emulator = {
    .create = xsf_ssf_create,
    .load = xsf_ssf_load,
    .start = xsf_ssf_start,
    .post_load = xsf_ssf_post_load,
    .render = xsf_ssf_render,
    .sample_rate = xsf_ssf_sample_rate,
    .seek_reset = xsf_ssf_seek_reset,
    .destroy = xsf_ssf_destroy,
    .get_psf2fs = nullptr,
    .load_psf2 = nullptr,
    .info = nullptr,
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// QSF emulator (highly_quixotic)

static const XsfEmulator s_qsf_emulator = {
    .create = xsf_qsf_create,
    .load = xsf_qsf_load,
    .start = xsf_qsf_start,
    .post_load = xsf_qsf_post_load,
    .render = xsf_qsf_render,
    .sample_rate = xsf_qsf_sample_rate,
    .seek_reset = xsf_qsf_seek_reset,
    .destroy = xsf_qsf_destroy,
    .get_psf2fs = nullptr,
    .load_psf2 = nullptr,
    .info = nullptr,
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// GSF emulator (viogsf)

static const XsfEmulator s_gsf_emulator = {
    .create = xsf_gsf_create,
    .load = xsf_gsf_load,
    .start = xsf_gsf_start,
    .post_load = xsf_gsf_post_load,
    .render = xsf_gsf_render,
    .sample_rate = xsf_gsf_sample_rate,
    .seek_reset = xsf_gsf_seek_reset,
    .destroy = xsf_gsf_destroy,
    .get_psf2fs = nullptr,
    .load_psf2 = nullptr,
    .info = nullptr,
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// SNSF emulator (snsf9x)

static const XsfEmulator s_snsf_emulator = {
    .create = xsf_snsf_create,
    .load = xsf_snsf_load,
    .start = xsf_snsf_start,
    .post_load = xsf_snsf_post_load,
    .render = xsf_snsf_render,
    .sample_rate = xsf_snsf_sample_rate,
    .seek_reset = xsf_snsf_seek_reset,
    .destroy = xsf_snsf_destroy,
    .get_psf2fs = nullptr,
    .load_psf2 = nullptr,
    .info = nullptr,
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct XsfReplayerData {
    void* emu_state;
    const XsfEmulator* emulator;
    XsfFileContext file_ctx;
    psf_file_callbacks psf_callbacks;
    int psf_version;
    int sample_rate;
    // Metadata from PSF tags
    char title[256];
    char artist[256];
    char game[256];
    int length_ms;
    int fade_ms;
    // URL for seek (reload)
    char url[2048];
    // Scope capture state
    int scope_enabled;
} XsfReplayerData;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static const char* xsf_plugin_supported_extensions(void) {
    return "psf,minipsf,psflib,psf2,minipsf2,psf2lib,"
           "ssf,minissf,ssflib,dsf,minidsf,dsflib,"
           "usf,miniusf,usflib,"
           "gsf,minigsf,gsflib,"
           "snsf,minisnsf,snsflib,"
           "2sf,mini2sf,2sflib,"
           "qsf,miniqsf,qsflib";
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Select emulator based on PSF version byte

static const XsfEmulator* xsf_get_emulator(uint8_t version) {
    switch (version) {
        case 0x01:
        case 0x02:
            return &s_psf_emulator;
        case 0x11:
        case 0x12:
            return &s_ssf_emulator;
        case 0x21:
            return &s_usf_emulator;
        case 0x22:
            return &s_gsf_emulator;
        case 0x23:
            return &s_snsf_emulator;
        case 0x24:
            return &s_2sf_emulator;
        case 0x41:
            return &s_qsf_emulator;
        default:
            return nullptr;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Check if a PSF version byte is supported

static int xsf_version_supported(uint8_t version) {
    return xsf_get_emulator(version) != nullptr;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Parse PSF tag time format: "seconds.decimal", "mm:ss.decimal", "hh:mm:ss.decimal"
// Returns milliseconds, or -1 on parse failure.

static int xsf_parse_time_tag(const char* value) {
    if (value == nullptr || value[0] == '\0') {
        return -1;
    }

    int parts[3] = { 0, 0, 0 };
    int part_count = 0;
    const char* p = value;

    // Parse colon-separated integer parts
    while (*p && part_count < 3) {
        int v = 0;
        int has_digit = 0;
        while (*p >= '0' && *p <= '9') {
            v = v * 10 + (*p - '0');
            has_digit = 1;
            p++;
        }
        if (!has_digit) {
            break;
        }
        parts[part_count++] = v;
        if (*p == ':') {
            p++;
        } else {
            break;
        }
    }

    if (part_count == 0) {
        return -1;
    }

    // Convert to seconds
    int seconds;
    if (part_count == 1) {
        seconds = parts[0];
    } else if (part_count == 2) {
        seconds = parts[0] * 60 + parts[1];
    } else {
        seconds = parts[0] * 3600 + parts[1] * 60 + parts[2];
    }

    int ms = seconds * 1000;

    // Parse decimal part
    if (*p == '.' || *p == ',') {
        p++;
        int frac = 0;
        int frac_digits = 0;
        while (*p >= '0' && *p <= '9' && frac_digits < 3) {
            frac = frac * 10 + (*p - '0');
            frac_digits++;
            p++;
        }
        // Normalize to milliseconds (pad with zeros)
        while (frac_digits < 3) {
            frac *= 10;
            frac_digits++;
        }
        ms += frac;
    }

    return ms;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// psflib info callback - receives metadata key/value pairs from PSF tags

static int xsf_info_callback(void* context, const char* name, const char* value) {
    XsfReplayerData* data = (XsfReplayerData*)context;

    if (strcasecmp(name, "length") == 0) {
        int ms = xsf_parse_time_tag(value);
        if (ms > 0) {
            data->length_ms = ms;
        }
    } else if (strcasecmp(name, "fade") == 0) {
        int ms = xsf_parse_time_tag(value);
        if (ms > 0) {
            data->fade_ms = ms;
        }
    } else if (strcasecmp(name, "title") == 0) {
        strncpy(data->title, value, sizeof(data->title) - 1);
        data->title[sizeof(data->title) - 1] = '\0';
    } else if (strcasecmp(name, "artist") == 0) {
        strncpy(data->artist, value, sizeof(data->artist) - 1);
        data->artist[sizeof(data->artist) - 1] = '\0';
    } else if (strcasecmp(name, "game") == 0) {
        strncpy(data->game, value, sizeof(data->game) - 1);
        data->game[sizeof(data->game) - 1] = '\0';
    } else if (strcasecmp(name, "_refresh") == 0) {
        // Handled internally by the PSF emulator
    }

    // Forward to emulator-specific info handler for format-specific tags
    if (data->emulator != nullptr && data->emulator->info != nullptr && data->emu_state != nullptr) {
        data->emulator->info(data->emu_state, name, value);
    }

    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void* xsf_plugin_create(const RVService* service_api) {
    XsfReplayerData* data = (XsfReplayerData*)calloc(1, sizeof(XsfReplayerData));
    if (data == nullptr) {
        return nullptr;
    }

    g_io_api = RVService_get_io(service_api, RV_IO_API_VERSION);
    data->length_ms = -1;
    data->fade_ms = 0;

    return data;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static int xsf_plugin_destroy(void* user_data) {
    XsfReplayerData* data = (XsfReplayerData*)user_data;

    if (data->emu_state != nullptr && data->emulator != nullptr) {
        data->emulator->destroy(data->emu_state);
    }

    free(data);
    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static int xsf_plugin_open(void* user_data, const char* url, uint32_t subsong, const RVService* service_api) {
    (void)subsong;
    (void)service_api;

    XsfReplayerData* data = (XsfReplayerData*)user_data;

    // Clean up previous state
    if (data->emu_state != nullptr && data->emulator != nullptr) {
        data->emulator->destroy(data->emu_state);
        data->emu_state = nullptr;
        data->emulator = nullptr;
    }

    // Reset metadata and scope state
    data->title[0] = '\0';
    data->artist[0] = '\0';
    data->game[0] = '\0';
    data->length_ms = -1;
    data->fade_ms = 0;
    data->scope_enabled = 0;

    // Save URL for seek
    strncpy(data->url, url, sizeof(data->url) - 1);
    data->url[sizeof(data->url) - 1] = '\0';

    // Set up psflib file callbacks using our IO bridge
    xsf_file_context_init(&data->file_ctx, g_io_api, url);
    data->psf_callbacks.path_separators = "\\/|:";
    data->psf_callbacks.context = &data->file_ctx;
    data->psf_callbacks.fopen = xsf_fopen;
    data->psf_callbacks.fread = xsf_fread;
    data->psf_callbacks.fseek = xsf_fseek;
    data->psf_callbacks.fclose = xsf_fclose;
    data->psf_callbacks.ftell = xsf_ftell;

    // Phase 1: Probe the file to determine PSF version and read metadata
    int version
        = psf_load(url, &data->psf_callbacks, 0, nullptr, nullptr, xsf_info_callback, data, 0, nullptr, nullptr);
    if (version <= 0) {
        rv_error("xSF: failed to determine PSF version for %s", url);
        return -1;
    }

    data->psf_version = version;

    // Select the right emulator for this version
    const XsfEmulator* emulator = xsf_get_emulator((uint8_t)version);
    if (emulator == nullptr) {
        rv_error("xSF: unsupported PSF version 0x%02x in %s", version, url);
        return -1;
    }
    data->emulator = emulator;

    // Create emulator state
    data->emu_state = emulator->create();
    if (data->emu_state == nullptr) {
        rv_error("xSF: failed to create emulator state for %s", url);
        return -1;
    }

    // Initialize emulator for this PSF version
    if (emulator->start(data->emu_state, version) != 0) {
        rv_error("xSF: failed to start emulator for %s", url);
        emulator->destroy(data->emu_state);
        data->emu_state = nullptr;
        return -1;
    }

    // Phase 2: Load the actual program data
    // For PSF2, use the psf2fs load callback; for PSF1, use the direct load callback
    psf_load_callback load_cb;
    void* load_ctx;

    if (version == 2 && emulator->load_psf2 != nullptr) {
        load_cb = emulator->load_psf2;
        load_ctx = data->emu_state;
    } else {
        load_cb = emulator->load;
        load_ctx = data->emu_state;
    }

    if (psf_load(url, &data->psf_callbacks, (uint8_t)version, load_cb, load_ctx, xsf_info_callback, data, 0, nullptr,
                 nullptr)
        < 0) {
        rv_error("xSF: failed to load PSF data from %s", url);
        emulator->destroy(data->emu_state);
        data->emu_state = nullptr;
        return -1;
    }

    // Post-load finalization (e.g. 2SF needs to init NDS after ROM data is accumulated)
    if (emulator->post_load != nullptr) {
        if (emulator->post_load(data->emu_state) != 0) {
            rv_error("xSF: post_load failed for %s", url);
            emulator->destroy(data->emu_state);
            data->emu_state = nullptr;
            return -1;
        }
    }

    data->sample_rate = emulator->sample_rate(data->emu_state);

    rv_info("xSF: opened %s (version=0x%02x, rate=%d)", url, version, data->sample_rate);
    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void xsf_plugin_close(void* user_data) {
    XsfReplayerData* data = (XsfReplayerData*)user_data;

    if (data->emu_state != nullptr && data->emulator != nullptr) {
        data->emulator->destroy(data->emu_state);
        data->emu_state = nullptr;
        data->emulator = nullptr;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static RVProbeResult xsf_plugin_probe_can_play(uint8_t* probe_data, uint64_t data_size, const char* url,
                                               uint64_t total_size) {
    (void)url;
    (void)total_size;

    // PSF magic: "PSF" at offset 0, version byte at offset 3
    if (data_size >= 4 && probe_data[0] == 'P' && probe_data[1] == 'S' && probe_data[2] == 'F') {
        uint8_t version = probe_data[3];
        if (xsf_version_supported(version)) {
            return RVProbeResult_Supported;
        }
    }

    // Extension-based fallback for mini*/lib files (these reference other files and may be tiny)
    if (url != nullptr) {
        const char* dot = strrchr(url, '.');
        if (dot != nullptr) {
            dot++; // skip the dot
            // PSF/PSF2 extensions
            if (strcasecmp(dot, "psf") == 0 || strcasecmp(dot, "minipsf") == 0 || strcasecmp(dot, "psflib") == 0
                || strcasecmp(dot, "psf2") == 0 || strcasecmp(dot, "minipsf2") == 0
                || strcasecmp(dot, "psf2lib") == 0) {
                return RVProbeResult_Unsure;
            }
            // SSF/DSF extensions
            if (strcasecmp(dot, "ssf") == 0 || strcasecmp(dot, "minissf") == 0 || strcasecmp(dot, "ssflib") == 0
                || strcasecmp(dot, "dsf") == 0 || strcasecmp(dot, "minidsf") == 0 || strcasecmp(dot, "dsflib") == 0) {
                return RVProbeResult_Unsure;
            }
            // USF extensions
            if (strcasecmp(dot, "usf") == 0 || strcasecmp(dot, "miniusf") == 0 || strcasecmp(dot, "usflib") == 0) {
                return RVProbeResult_Unsure;
            }
            // GSF extensions
            if (strcasecmp(dot, "gsf") == 0 || strcasecmp(dot, "minigsf") == 0 || strcasecmp(dot, "gsflib") == 0) {
                return RVProbeResult_Unsure;
            }
            // SNSF extensions
            if (strcasecmp(dot, "snsf") == 0 || strcasecmp(dot, "minisnsf") == 0 || strcasecmp(dot, "snsflib") == 0) {
                return RVProbeResult_Unsure;
            }
            // 2SF extensions
            if (strcasecmp(dot, "2sf") == 0 || strcasecmp(dot, "mini2sf") == 0 || strcasecmp(dot, "2sflib") == 0) {
                return RVProbeResult_Unsure;
            }
            // QSF extensions
            if (strcasecmp(dot, "qsf") == 0 || strcasecmp(dot, "miniqsf") == 0 || strcasecmp(dot, "qsflib") == 0) {
                return RVProbeResult_Unsure;
            }
        }
    }

    return RVProbeResult_Unsupported;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static RVReadInfo xsf_plugin_read_data(void* user_data, RVReadData dest) {
    XsfReplayerData* data = (XsfReplayerData*)user_data;

    if (data->emu_state == nullptr || data->emulator == nullptr) {
        RVAudioFormat format = { RVAudioStreamFormat_S16, 2, 44100 };
        return (RVReadInfo) { format, 0, RVReadStatus_Error, 0 };
    }

    // Report native format: S16 stereo at emulator's native sample rate
    uint32_t native_rate = (uint32_t)data->sample_rate;
    RVAudioFormat format = { RVAudioStreamFormat_S16, 2, native_rate };

    uint32_t max_frames = dest.channels_output_max_bytes_size / (sizeof(int16_t) * 2);

    int rendered = data->emulator->render(data->emu_state, (int16_t*)dest.channels_output, (int)max_frames);
    if (rendered <= 0) {
        return (RVReadInfo) { format, 0, RVReadStatus_Finished, 0 };
    }

    return (RVReadInfo) { format, (uint32_t)rendered, RVReadStatus_Ok, 0 };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static int64_t xsf_plugin_seek(void* user_data, int64_t ms) {
    XsfReplayerData* data = (XsfReplayerData*)user_data;

    if (data->emu_state == nullptr || data->emulator == nullptr) {
        return -1;
    }

    // Only seek-to-start is supported (reset and re-render)
    if (ms == 0 && data->emulator->seek_reset != nullptr) {
        if (data->emulator->seek_reset(data->emu_state) == 0) {
            // Reload the PSF data after reset
            psf_load_callback load_cb;
            void* load_ctx;

            if (data->psf_version == 2 && data->emulator->load_psf2 != nullptr) {
                load_cb = data->emulator->load_psf2;
                load_ctx = data->emu_state;
            } else {
                load_cb = data->emulator->load;
                load_ctx = data->emu_state;
            }

            psf_load(data->url, &data->psf_callbacks, (uint8_t)data->psf_version, load_cb, load_ctx, nullptr, nullptr,
                     0, nullptr, nullptr);

            // Post-load finalization after reload
            if (data->emulator->post_load != nullptr) {
                data->emulator->post_load(data->emu_state);
            }

            return 0;
        }
    }

    return -1;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static int xsf_plugin_metadata(const char* url, const RVService* service_api) {
    const RVIo* io_api = RVService_get_io(service_api, RV_IO_API_VERSION);
    const RVMetadata* metadata_api = RVService_get_metadata(service_api, RV_METADATA_API_VERSION);

    if (io_api == nullptr || metadata_api == nullptr) {
        return -1;
    }

    // Set up temp data for metadata parsing
    XsfReplayerData temp_data;
    memset(&temp_data, 0, sizeof(temp_data));
    temp_data.length_ms = -1;

    // Set up psflib file callbacks
    XsfFileContext file_ctx;
    xsf_file_context_init(&file_ctx, io_api, url);

    psf_file_callbacks callbacks;
    callbacks.path_separators = "\\/|:";
    callbacks.context = &file_ctx;
    callbacks.fopen = xsf_fopen;
    callbacks.fread = xsf_fread;
    callbacks.fseek = xsf_fseek;
    callbacks.fclose = xsf_fclose;
    callbacks.ftell = xsf_ftell;

    // Load metadata only (version=0)
    int version = psf_load(url, &callbacks, 0, nullptr, nullptr, xsf_info_callback, &temp_data, 0, nullptr, nullptr);
    if (version <= 0) {
        return -1;
    }

    RVMetadataId index = RVMetadata_create_url(metadata_api, url);

    if (temp_data.title[0] != '\0') {
        RVMetadata_set_tag(metadata_api, index, RV_METADATA_TITLE_TAG, temp_data.title);
    }
    if (temp_data.artist[0] != '\0') {
        RVMetadata_set_tag(metadata_api, index, RV_METADATA_ARTIST_TAG, temp_data.artist);
    }
    if (temp_data.game[0] != '\0') {
        RVMetadata_set_tag(metadata_api, index, RV_METADATA_SONGTYPE_TAG, temp_data.game);
    }

    // Set platform name
    const char* platform = "Unknown";
    switch (version) {
        case 0x01:
            platform = "PlayStation";
            break;
        case 0x02:
            platform = "PlayStation 2";
            break;
        case 0x11:
            platform = "Sega Saturn";
            break;
        case 0x12:
            platform = "Sega Dreamcast";
            break;
        case 0x21:
            platform = "Nintendo 64";
            break;
        case 0x22:
            platform = "Game Boy Advance";
            break;
        case 0x23:
            platform = "Super Nintendo";
            break;
        case 0x24:
            platform = "Nintendo DS";
            break;
        case 0x41:
            platform = "Capcom QSound";
            break;
    }
    RVMetadata_set_tag(metadata_api, index, RV_METADATA_AUTHORINGTOOL_TAG, platform);

    if (temp_data.length_ms > 0) {
        double length_sec = (double)temp_data.length_ms / 1000.0;
        RVMetadata_set_tag_f64(metadata_api, index, RV_METADATA_LENGTH_TAG, length_sec);
    }

    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void xsf_plugin_event(void* user_data, uint8_t* event_data, uint64_t len) {
    (void)user_data;
    (void)event_data;
    (void)len;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// BIOS data loaded from external file (must remain valid for program lifetime)
static uint8_t* s_psx_bios_copy = nullptr;

// Well-known BIOS filename. The host application must provide this file
// alongside the plugin (or in a data directory accessible via the IO API).
// The file is a pre-processed PS2 IOP BIOS created with the mkhebios tool
// from the highly_experimental library.
static const char* s_bios_search_paths[] = {
    "psf_bios/scph10000_he.bin",
    "bios/scph10000_he.bin",
    "scph10000_he.bin",
};

static void xsf_plugin_static_init(const RVService* service_api) {
    g_rv_log = RVService_get_log(service_api, RV_LOG_API_VERSION);
    const RVIo* io = RVService_get_io(service_api, RV_IO_API_VERSION);

    if (io == nullptr) {
        rv_error("PSF: IO API not available, PSF/PSF2 playback will be disabled");
        return;
    }

    // Search for external BIOS file in well-known locations
    RVIoReadUrlResult bios_result = {0};
    const char* found_path = nullptr;

    for (int i = 0; i < (int)(sizeof(s_bios_search_paths) / sizeof(s_bios_search_paths[0])); i++) {
        if (RVIo_exists(io, s_bios_search_paths[i])) {
            bios_result = RVIo_read_url_to_memory(io, s_bios_search_paths[i]);
            if (bios_result.data != nullptr && bios_result.data_size > 0) {
                found_path = s_bios_search_paths[i];
                break;
            }
        }
    }

    if (bios_result.data == nullptr || bios_result.data_size == 0) {
        rv_info("PSF: No BIOS file found, PSF/PSF2 playback will be disabled. "
                "Place scph10000_he.bin in psf_bios/ to enable it.");
        return;
    }

    // Make a mutable copy (highly_experimental may modify BIOS data for endian swaps)
    s_psx_bios_copy = (uint8_t*)malloc((size_t)bios_result.data_size);
    if (s_psx_bios_copy != nullptr) {
        memcpy(s_psx_bios_copy, bios_result.data, (size_t)bios_result.data_size);
        if (xsf_psf_init_bios(s_psx_bios_copy, (uint32_t)bios_result.data_size) == 0) {
            rv_info("PSF: BIOS loaded from '%s' (%u bytes)", found_path, (uint32_t)bios_result.data_size);
        } else {
            rv_error("PSF: BIOS validation failed for '%s'", found_path);
            free(s_psx_bios_copy);
            s_psx_bios_copy = nullptr;
        }
    }

    RVIo_free_url_to_memory(io, bios_result.data);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Per-channel scope data retrieval. Currently only PSF (highly_experimental) supports scope capture.
// Other emulators gracefully return 0.

static uint32_t xsf_get_scope_data(void* user_data, int channel, float* buffer, uint32_t num_samples) {
    XsfReplayerData* data = (XsfReplayerData*)user_data;
    if (data == nullptr || data->emu_state == nullptr || buffer == nullptr) {
        return 0;
    }

    // Only PSF emulator supports scope capture
    if (data->emulator != &s_psf_emulator) {
        return 0;
    }

    // Auto-enable scope on first request
    if (!data->scope_enabled) {
        xsf_psf_enable_scope_capture(data->emu_state, 1);
        data->scope_enabled = 1;
    }

    return xsf_psf_get_scope_data(data->emu_state, channel, buffer, num_samples);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void xsf_plugin_static_destroy(void) {
    free(s_psx_bios_copy);
    s_psx_bios_copy = nullptr;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint32_t xsf_get_scope_channel_names(void* user_data, const char** names, uint32_t max_channels) {
    XsfReplayerData* data = (XsfReplayerData*)user_data;
    if (data == nullptr || data->emu_state == nullptr)
        return 0;

    // Only PSF emulator supports scope capture
    if (data->emulator != &s_psf_emulator) {
        return 0;
    }

    static char s_name_bufs[48][16];
    int total = xsf_psf_get_scope_channel_count(data->emu_state);
    if (total <= 0)
        return 0;

    uint32_t count = (uint32_t)total;
    if (count > 48)
        count = 48;
    if (count > max_channels)
        count = max_channels;
    for (uint32_t i = 0; i < count; i++) {
        snprintf(s_name_bufs[i], sizeof(s_name_bufs[i]), "SPU %u", i + 1);
        names[i] = s_name_bufs[i];
    }
    return count;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static RVPlaybackPlugin g_xsf_plugin = {
    RV_PLAYBACK_PLUGIN_API_VERSION,
    "xsf",
    "0.1.0",
    "highly_experimental, vio2sf, lazyusf2, highly_theoretical, highly_quixotic, viogsf (kode54), snsf9x (loveemu)",
    xsf_plugin_probe_can_play,
    xsf_plugin_supported_extensions,
    xsf_plugin_create,
    xsf_plugin_destroy,
    xsf_plugin_event,
    xsf_plugin_open,
    xsf_plugin_close,
    xsf_plugin_read_data,
    xsf_plugin_seek,
    xsf_plugin_metadata,
    xsf_plugin_static_init,
    nullptr, // settings_updated
    nullptr, // get_tracker_info
    nullptr, // get_pattern_cell
    nullptr, // get_pattern_num_rows
    xsf_get_scope_data,
    xsf_plugin_static_destroy,
    xsf_get_scope_channel_names,
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RV_EXPORT RVPlaybackPlugin* rv_playback_plugin(void) {
    return &g_xsf_plugin;
}
