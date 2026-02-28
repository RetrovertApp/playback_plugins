///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// 98fmplayer Playback Plugin
//
// Implements RVPlaybackPlugin interface for NEC PC-98 FMP (PLAY6) music format.
// FMP uses YM2608 (OPNA) FM synthesis. The OPNA emulator runs at 55467 Hz natively;
// this plugin resamples output to 48000 Hz using linear interpolation.
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

#include "fmdriver.h"
#include "fmdriver_fmp.h"
#include "opna.h"
#include "opnaadpcm.h"
#include "opnadrum.h"
#include "opnatimer.h"
#include "ppz8.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define strcasecmp _stricmp
#else
#include <strings.h>
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// OPNA native output rate (7987200 Hz master clock / 144)
// OPNA native output rate (7987200 Hz master clock / 144)
#define OPNA_RATE 55467
// Default song length (4 minutes) since FMP files don't embed duration
#define DEFAULT_LENGTH_MS (4 * 60 * 1000)
// PPZ8 mix volume (from 98fmplayer reference)
#define PPZ8_MIX_VOLUME 0xa000

static const RVIo* g_io_api = nullptr;
const RVLog* g_rv_log = nullptr;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct FmplayerData {
    struct opna opna;
    struct opna_timer timer;
    struct ppz8 ppz8;
    struct fmdriver_work work;
    struct driver_fmp fmp;
    uint8_t* adpcm_ram; // OPNA_ADPCM_RAM_SIZE bytes (256KB), heap allocated
    uint8_t* file_data; // Kept alive for driver access during playback
    int file_open;
    int elapsed_frames; // Output frames elapsed (at OPNA_RATE)
    int max_frames;     // Max output frames before song ends (at OPNA_RATE)
} FmplayerData;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// OPNA callback wrappers for fmdriver_work
//
// These callbacks receive (struct fmdriver_work *work) as first arg.
// We use work->opna (void*) to get back to the OPNA instance.

static void fmplayer_opna_writereg(struct fmdriver_work* work, unsigned addr, unsigned data) {
    struct opna* opna = (struct opna*)work->opna;
    opna_writereg(opna, addr, data);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static unsigned fmplayer_opna_readreg(struct fmdriver_work* work, unsigned addr) {
    struct opna* opna = (struct opna*)work->opna;
    return opna_readreg(opna, addr);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint8_t fmplayer_opna_status(struct fmdriver_work* work, bool a1) {
    (void)a1;
    struct opna* opna = (struct opna*)work->opna;
    // Status register: return timer flags
    return (uint8_t)(opna_readreg(opna, 0) & 0x03);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Timer interrupt callback: called by opna_timer when a timer expires.
// This uses the opna_timer callback pattern (void* userptr).

static void fmplayer_int_callback(void* userptr) {
    struct fmdriver_work* work = (struct fmdriver_work*)userptr;
    if (work->driver_opna_interrupt) {
        work->driver_opna_interrupt(work);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Timer mix callback: mix PPZ8 into the OPNA output.
// This uses the opna_timer callback pattern (void* userptr).

static void fmplayer_mix_callback(void* userptr, int16_t* buf, unsigned samples) {
    struct ppz8* ppz8 = (struct ppz8*)userptr;
    ppz8_mix(ppz8, buf, samples);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static const char* fmplayer_plugin_supported_extensions(void) {
    return "opi,ovi,ozi,fmp";
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void* fmplayer_plugin_create(const RVService* service_api) {
    FmplayerData* data = calloc(1, sizeof(FmplayerData));
    if (data == nullptr) {
        return nullptr;
    }

    // Allocate ADPCM RAM (256KB) on heap to avoid huge stack/struct
    data->adpcm_ram = calloc(1, OPNA_ADPCM_RAM_SIZE);
    if (data->adpcm_ram == nullptr) {
        free(data);
        return nullptr;
    }

    g_io_api = RVService_get_io(service_api, RV_IO_API_VERSION);
    return data;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static int fmplayer_plugin_destroy(void* user_data) {
    FmplayerData* data = (FmplayerData*)user_data;
    free(data->adpcm_ram);
    free(data->file_data);
    free(data);
    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Initialize OPNA emulator and wire up fmdriver_work callbacks

static void fmplayer_init_opna(FmplayerData* data) {
    opna_reset(&data->opna);
    // Note: drum ROM not loaded -- rhythm drums will be silent but playback works
    opna_adpcm_set_ram_256k(&data->opna.adpcm, data->adpcm_ram);
    opna_timer_reset(&data->timer, &data->opna);
    ppz8_init(&data->ppz8, OPNA_RATE, PPZ8_MIX_VOLUME);

    memset(&data->work, 0, sizeof(data->work));

    // Store OPNA pointer in work struct for callback access
    data->work.opna = &data->opna;

    // Wire OPNA register access callbacks
    data->work.opna_writereg = fmplayer_opna_writereg;
    data->work.opna_readreg = fmplayer_opna_readreg;
    data->work.opna_status = fmplayer_opna_status;
    data->work.ppz8 = &data->ppz8;

    // Set timer callbacks (opna_timer uses void* userptr pattern)
    opna_timer_set_int_callback(&data->timer, fmplayer_int_callback, &data->work);
    opna_timer_set_mix_callback(&data->timer, fmplayer_mix_callback, &data->ppz8);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static int fmplayer_plugin_open(void* user_data, const char* url, uint32_t subsong, const RVService* service_api) {
    (void)subsong;
    (void)service_api;

    FmplayerData* data = (FmplayerData*)user_data;

    // Clean up previous
    free(data->file_data);
    data->file_data = nullptr;
    data->file_open = 0;

    RVIoReadUrlResult read_res = RVIo_read_url_to_memory(g_io_api, url);
    if (read_res.data == nullptr) {
        rv_error("fmplayer: Failed to load %s to memory", url);
        return -1;
    }

    // FMP files are limited to 64KB
    if (read_res.data_size > 0xFFFF) {
        rv_error("fmplayer: File too large for FMP format: %s", url);
        RVIo_free_url_to_memory(g_io_api, read_res.data);
        return -1;
    }

    // Keep a copy of the file data (driver references it during playback)
    data->file_data = malloc((size_t)read_res.data_size);
    if (data->file_data == nullptr) {
        RVIo_free_url_to_memory(g_io_api, read_res.data);
        return -1;
    }
    memcpy(data->file_data, read_res.data, (size_t)read_res.data_size);

    // Initialize OPNA emulator
    fmplayer_init_opna(data);

    // Try loading as FMP format
    if (!fmp_load(&data->fmp, data->file_data, (uint16_t)read_res.data_size)) {
        rv_error("fmplayer: Failed to parse FMP file: %s", url);
        RVIo_free_url_to_memory(g_io_api, read_res.data);
        free(data->file_data);
        data->file_data = nullptr;
        return -1;
    }

    // Initialize the FMP driver (sets work->driver_opna_interrupt)
    fmp_init(&data->work, &data->fmp);

    RVIo_free_url_to_memory(g_io_api, read_res.data);

    data->file_open = 1;
    data->elapsed_frames = 0;
    data->max_frames = (int)(((int64_t)DEFAULT_LENGTH_MS * OPNA_RATE) / 1000);

    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void fmplayer_plugin_close(void* user_data) {
    FmplayerData* data = (FmplayerData*)user_data;
    free(data->file_data);
    data->file_data = nullptr;
    data->file_open = 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static RVProbeResult fmplayer_plugin_probe_can_play(uint8_t* probe_data, uint64_t data_size, const char* url,
                                                    uint64_t total_size) {
    (void)total_size;

    // Need at least a few bytes to check the FMP signature
    if (data_size < 8) {
        return RVProbeResult_Unsupported;
    }

    // FMP detection: 16-bit LE offset at byte 0 points to "FMC" or "ELF" signature
    uint16_t offset = (uint16_t)(probe_data[0] | (probe_data[1] << 8));
    if (offset + 3 <= data_size) {
        if (probe_data[offset] == 'F' && probe_data[offset + 1] == 'M' && probe_data[offset + 2] == 'C') {
            return RVProbeResult_Supported;
        }
        if (probe_data[offset] == 'E' && probe_data[offset + 1] == 'L' && probe_data[offset + 2] == 'F') {
            return RVProbeResult_Supported;
        }
    }

    // Extension-based fallback for FMP files
    if (url != nullptr) {
        const char* dot = strrchr(url, '.');
        if (dot != nullptr) {
            if (strcasecmp(dot, ".opi") == 0 || strcasecmp(dot, ".ovi") == 0 || strcasecmp(dot, ".ozi") == 0
                || strcasecmp(dot, ".fmp") == 0) {
                return RVProbeResult_Unsure;
            }
        }
    }

    return RVProbeResult_Unsupported;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static RVReadInfo fmplayer_plugin_read_data(void* user_data, RVReadData dest) {
    FmplayerData* data = (FmplayerData*)user_data;

    // Report native format: S16 stereo at OPNA native rate
    RVAudioFormat format = { RVAudioStreamFormat_S16, 2, OPNA_RATE };

    if (!data->file_open) {
        return (RVReadInfo) { format, 0, RVReadStatus_Error, 0 };
    }

    // Check song length limit or loop detection
    if (data->elapsed_frames >= data->max_frames || data->work.loop_cnt >= 2) {
        return (RVReadInfo) { format, 0, RVReadStatus_Finished, 0 };
    }

    // Calculate output frames at OPNA native rate
    uint32_t out_frames = dest.channels_output_max_bytes_size / (sizeof(int16_t) * 2);

    // Generate audio at OPNA native rate (55467 Hz, stereo S16) directly to output
    int16_t* output = (int16_t*)dest.channels_output;
    memset(output, 0, out_frames * 2 * sizeof(int16_t));
    opna_timer_mix(&data->timer, output, out_frames);

    data->elapsed_frames += (int)out_frames;

    RVReadStatus status = RVReadStatus_Ok;
    if (data->elapsed_frames >= data->max_frames || data->work.loop_cnt >= 2) {
        status = RVReadStatus_Finished;
    }

    return (RVReadInfo) { format, out_frames, status, 0 };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static int64_t fmplayer_plugin_seek(void* user_data, int64_t ms) {
    (void)user_data;
    (void)ms;
    return -1;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static int fmplayer_plugin_metadata(const char* url, const RVService* service_api) {
    const RVMetadata* metadata_api = RVService_get_metadata(service_api, RV_METADATA_API_VERSION);
    if (metadata_api == nullptr) {
        return -1;
    }

    RVMetadataId index = RVMetadata_create_url(metadata_api, url);
    RVMetadata_set_tag(metadata_api, index, RV_METADATA_SONGTYPE_TAG, "FMP");
    RVMetadata_set_tag(metadata_api, index, RV_METADATA_AUTHORINGTOOL_TAG, "NEC PC-98");
    RVMetadata_set_tag_f64(metadata_api, index, RV_METADATA_LENGTH_TAG, DEFAULT_LENGTH_MS / 1000.0);

    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void fmplayer_plugin_event(void* user_data, uint8_t* event_data, uint64_t len) {
    (void)user_data;
    (void)event_data;
    (void)len;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void fmplayer_plugin_static_init(const RVService* service_api) {
    g_rv_log = RVService_get_log(service_api, RV_LOG_API_VERSION);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static RVPlaybackPlugin g_fmplayer_plugin = {
    RV_PLAYBACK_PLUGIN_API_VERSION,
    "fmplayer",
    "0.0.1",
    "98fmplayer 0.1.14",
    fmplayer_plugin_probe_can_play,
    fmplayer_plugin_supported_extensions,
    fmplayer_plugin_create,
    fmplayer_plugin_destroy,
    fmplayer_plugin_event,
    fmplayer_plugin_open,
    fmplayer_plugin_close,
    fmplayer_plugin_read_data,
    fmplayer_plugin_seek,
    fmplayer_plugin_metadata,
    fmplayer_plugin_static_init,
    nullptr, // settings_updated
    nullptr, // get_tracker_info
    nullptr, // get_pattern_cell
    nullptr, // get_pattern_num_rows
    nullptr, // get_scope_data
    nullptr, // static_destroy
    nullptr, // get_scope_channel_names
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RV_EXPORT RVPlaybackPlugin* rv_playback_plugin(void) {
    return &g_fmplayer_plugin;
}
