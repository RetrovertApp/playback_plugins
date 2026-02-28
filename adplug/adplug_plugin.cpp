///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// AdPlug Playback Plugin
//
// Implements RVPlaybackPlugin interface for OPL/AdLib music formats using AdPlug:
// CMF, DRO, IMF, RAW, ROL, HSC, A2M, and 50+ other formats.
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <retrovert/io.h>
#include <retrovert/log.h>
#include <retrovert/metadata.h>
#include <retrovert/playback.h>
#include <retrovert/service.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// AdPlug headers
#include "adplug.h"
#include "binstr.h"
#include "emuopl.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Module-local globals for API access

static const RVIo* g_io_api = nullptr;
const RVLog* g_rv_log = nullptr;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Constants

static const uint32_t SAMPLE_RATE = 48000;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Memory-based file provider for AdPlug
//
// AdPlug uses CFileProvider for file I/O. We implement a memory-based provider
// that wraps data loaded from our VFS.

class CProvider_Memory : public CFileProvider {
  public:
    CProvider_Memory(const uint8_t* data, uint64_t size, const std::string& filename)
        : m_data(data), m_size(size), m_filename(filename) {}

    virtual binistream* open(std::string filename) const override {
        (void)filename;
        // Return a memory stream wrapping our data
        return new binisstream(const_cast<uint8_t*>(m_data), static_cast<unsigned long>(m_size));
    }

    virtual void close(binistream* stream) const override {
        delete stream;
    }

  private:
    const uint8_t* m_data;
    uint64_t m_size;
    std::string m_filename;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Plugin instance data

struct AdplugData {
    CEmuopl* opl;
    CPlayer* player;
    int16_t* temp_buffer;
    uint32_t temp_buffer_size;
    uint32_t sample_rate;
    uint8_t vu_left;
    uint8_t vu_right;
    double samples_per_tick;
    double sample_accumulator;
    uint8_t* file_data;
    uint64_t file_size;
    uint32_t current_subsong;
    bool scope_enabled;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Helper: case-insensitive string comparison

static int strcasecmp_local(const char* a, const char* b) {
    while (*a && *b) {
        char ca = (*a >= 'A' && *a <= 'Z') ? (*a + 32) : *a;
        char cb = (*b >= 'A' && *b <= 'Z') ? (*b + 32) : *b;
        if (ca != cb) {
            return ca - cb;
        }
        a++;
        b++;
    }
    char ca = (*a >= 'A' && *a <= 'Z') ? (*a + 32) : *a;
    char cb = (*b >= 'A' && *b <= 'Z') ? (*b + 32) : *b;
    return ca - cb;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Helper: get file extension from filename

static const char* get_extension(const char* filename) {
    if (filename == nullptr) {
        return nullptr;
    }
    const char* dot = strrchr(filename, '.');
    if (dot == nullptr || dot == filename) {
        return nullptr;
    }
    return dot + 1;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Helper: check if extension matches any in a null-terminated list

static bool extension_matches(const char* ext, const char* const* list) {
    if (ext == nullptr) {
        return false;
    }
    for (int i = 0; list[i] != nullptr; i++) {
        if (strcasecmp_local(ext, list[i]) == 0) {
            return true;
        }
    }
    return false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// RVPlaybackPlugin implementation

static const char* adplug_supported_extensions(void) {
    // Full list of extensions supported by AdPlug 2.4
    return "a2m,adl,adt,adtrack,agd,amd,bam,bmf,cff,cmf,d00,dfm,dmo,dro,dtm,"
           "got,herad,hsc,hsp,hybrid,hyp,imf,jbm,ksm,laa,lds,m,mad,mdi,"
           "mkj,msc,mtk,mtr,mus,pis,plx,psi,rad,rat,raw,rix,rol,s3m,sa2,sat,sci,"
           "sng,sop,u6m,vgm,xad,xms,xsm";
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static RVProbeResult adplug_probe_can_play(uint8_t* data, uint64_t data_size, const char* filename,
                                           uint64_t total_size) {
    (void)total_size;

    if (data == nullptr || data_size < 4) {
        return RVProbeResult_Unsupported;
    }

    // CMF: "CTMF" at offset 0
    if (data_size >= 4 && data[0] == 'C' && data[1] == 'T' && data[2] == 'M' && data[3] == 'F') {
        return RVProbeResult_Supported;
    }

    // DRO v1/v2: "DBRAWOPL" at offset 0
    if (data_size >= 8 && memcmp(data, "DBRAWOPL", 8) == 0) {
        return RVProbeResult_Supported;
    }

    // D00: "JCH\x26" or "EdLib" at offset 0
    if (data_size >= 4 && data[0] == 'J' && data[1] == 'C' && data[2] == 'H' && data[3] == 0x26) {
        return RVProbeResult_Supported;
    }
    if (data_size >= 6 && memcmp(data, "\x00\x00\x00\x00\x00\x00", 6) != 0 && (data[0] == 0x00 || data[0] == 0x01)) {
        // D00 files can start with 0x00 or 0x01
    }

    // A2M: ADLIB TRACKER magic
    if (data_size >= 18 && memcmp(data, "_A2module_", 10) == 0) {
        return RVProbeResult_Supported;
    }

    // BMF: "BMF1.2" magic
    if (data_size >= 6 && memcmp(data, "BMF1.2", 6) == 0) {
        return RVProbeResult_Supported;
    }

    // CFF: "CFFFILE" magic
    if (data_size >= 16 && memcmp(data, "<CONSTRUCTUS>", 13) == 0) {
        return RVProbeResult_Supported;
    }

    // SOP: "sopepos" magic
    if (data_size >= 7 && memcmp(data, "sopepos", 7) == 0) {
        return RVProbeResult_Supported;
    }

    // RAD: "RAD by REALiTY!!" magic
    if (data_size >= 16 && memcmp(data, "RAD by REALiTY!!", 16) == 0) {
        return RVProbeResult_Supported;
    }

    // HSC: Check for HSC file patterns (first byte is tempo, usually 1-10)
    // Note: HSC has no magic, rely on extension

    // Many AdPlug formats don't have magic bytes - check extension
    const char* ext = get_extension(filename);
    if (ext != nullptr) {
        // Extensions with good magic byte detection in AdPlug
        static const char* const magic_exts[] = { "cmf", "dro", "d00", "a2m", "bmf", "rad", "sop", nullptr };
        if (extension_matches(ext, magic_exts)) {
            return RVProbeResult_Unsure;
        }

        // Other AdPlug extensions without reliable magic bytes
        static const char* const other_exts[]
            = { "adl", "adt", "adtrack", "agd", "amd", "bam", "cff", "dfm", "dmo", "dtm", "got",  "herad",
                "hsc", "hsp", "hybrid",  "hyp", "imf", "jbm", "ksm", "laa", "lds", "m",   "mad",  "mdi",
                "mkj", "msc", "mtk",     "mtr", "mus", "pis", "plx", "psi", "rat", "raw", "rix",  "rol",
                "s3m", "sa2", "sat",     "sci", "sng", "u6m", "vgm", "xad", "xms", "xsm", nullptr };
        if (extension_matches(ext, other_exts)) {
            return RVProbeResult_Unsure;
        }
    }

    return RVProbeResult_Unsupported;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void* adplug_create(const RVService* service_api) {
    AdplugData* data = static_cast<AdplugData*>(malloc(sizeof(AdplugData)));
    if (data == nullptr) {
        return nullptr;
    }
    memset(data, 0, sizeof(AdplugData));

    g_io_api = RVService_get_io(service_api, RV_IO_API_VERSION);

    // Create OPL emulator: 48kHz, 16-bit, stereo
    data->opl = new CEmuopl(SAMPLE_RATE, true, true);
    data->sample_rate = SAMPLE_RATE;

    // Allocate temp buffer for rendering (enough for ~100ms of audio)
    data->temp_buffer_size = (SAMPLE_RATE / 10) * 2; // Stereo samples
    data->temp_buffer = static_cast<int16_t*>(malloc(data->temp_buffer_size * sizeof(int16_t)));
    if (data->temp_buffer == nullptr) {
        delete data->opl;
        free(data);
        return nullptr;
    }

    return data;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static int adplug_destroy(void* user_data) {
    AdplugData* data = static_cast<AdplugData*>(user_data);
    if (data == nullptr) {
        return 0;
    }

    if (data->player != nullptr) {
        delete data->player;
        data->player = nullptr;
    }

    if (data->opl != nullptr) {
        delete data->opl;
        data->opl = nullptr;
    }

    if (data->temp_buffer != nullptr) {
        free(data->temp_buffer);
        data->temp_buffer = nullptr;
    }

    if (data->file_data != nullptr) {
        RVIo_free_url_to_memory(g_io_api, data->file_data);
        data->file_data = nullptr;
    }

    free(data);
    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static int adplug_open(void* user_data, const char* url, uint32_t subsong, const RVService* service_api) {
    (void)service_api;

    AdplugData* data = static_cast<AdplugData*>(user_data);

    // Clean up any previously open file
    if (data->player != nullptr) {
        delete data->player;
        data->player = nullptr;
    }

    if (data->file_data != nullptr) {
        RVIo_free_url_to_memory(g_io_api, data->file_data);
        data->file_data = nullptr;
    }

    data->vu_left = 0;
    data->vu_right = 0;
    data->sample_accumulator = 0.0;
    data->current_subsong = subsong;

    // Load file into memory
    RVIoReadUrlResult read_res = RVIo_read_url_to_memory(g_io_api, url);
    if (read_res.data == nullptr) {
        rv_error("adplug: Failed to load file: %s", url);
        return -1;
    }

    data->file_data = read_res.data;
    data->file_size = read_res.data_size;

    // Create memory provider
    CProvider_Memory provider(data->file_data, data->file_size, url);

    // Reset OPL emulator
    data->opl->init();

    // Use AdPlug factory to create appropriate player
    data->player = CAdPlug::factory(url, data->opl, CAdPlug::players, provider);
    if (data->player == nullptr) {
        rv_error("adplug: Failed to create player for: %s", url);
        RVIo_free_url_to_memory(g_io_api, data->file_data);
        data->file_data = nullptr;
        return -1;
    }

    // Rewind to requested subsong
    if (subsong < data->player->getsubsongs()) {
        data->player->rewind(static_cast<int>(subsong));
    } else {
        data->player->rewind(0);
    }

    // Calculate samples per tick based on refresh rate
    float refresh = data->player->getrefresh();
    if (refresh <= 0.0f) {
        refresh = 70.0f; // Default to 70 Hz if not specified
    }
    data->samples_per_tick = static_cast<double>(data->sample_rate) / static_cast<double>(refresh);

    rv_info("adplug: Loaded %s (type: %s, refresh: %.2f Hz)", url, data->player->gettype().c_str(), refresh);

    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void adplug_close(void* user_data) {
    AdplugData* data = static_cast<AdplugData*>(user_data);

    if (data->player != nullptr) {
        delete data->player;
        data->player = nullptr;
    }

    if (data->file_data != nullptr) {
        RVIo_free_url_to_memory(g_io_api, data->file_data);
        data->file_data = nullptr;
    }

    data->vu_left = 0;
    data->vu_right = 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static RVReadInfo adplug_read_data(void* user_data, RVReadData dest) {
    AdplugData* data = static_cast<AdplugData*>(user_data);
    RVAudioFormat format = { RVAudioStreamFormat_S16, 2, SAMPLE_RATE };
    RVReadInfo info = { format, 0, RVReadStatus_Ok };

    if (data->player == nullptr || data->opl == nullptr) {
        info.status = RVReadStatus_Error;
        return info;
    }

    uint32_t max_frames = dest.channels_output_max_bytes_size / (sizeof(int16_t) * 2);

    // Limit to our temp buffer size (OPL renders into temp_buffer)
    if (max_frames > data->temp_buffer_size / 2) {
        max_frames = data->temp_buffer_size / 2;
    }

    uint32_t frames_generated = 0;
    bool finished = false;
    int16_t peak_left = 0;
    int16_t peak_right = 0;

    // Render audio using tick-based loop into temp_buffer
    while (frames_generated < max_frames && !finished) {
        // Calculate samples until next tick
        uint32_t samples_until_tick = static_cast<uint32_t>(data->samples_per_tick - data->sample_accumulator);
        if (samples_until_tick == 0) {
            samples_until_tick = 1;
        }

        // Don't exceed remaining space
        uint32_t samples_to_generate = samples_until_tick;
        if (frames_generated + samples_to_generate > max_frames) {
            samples_to_generate = max_frames - frames_generated;
        }

        // Generate audio from OPL emulator
        data->opl->update(data->temp_buffer + (frames_generated * 2), static_cast<int>(samples_to_generate));

        frames_generated += samples_to_generate;
        data->sample_accumulator += samples_to_generate;

        // Check if we've reached tick boundary
        if (data->sample_accumulator >= data->samples_per_tick) {
            // Call player update - returns false when song is done
            finished = !data->player->update();
            data->sample_accumulator -= data->samples_per_tick;

            // Update refresh rate (some formats change it during playback)
            float refresh = data->player->getrefresh();
            if (refresh > 0.0f) {
                data->samples_per_tick = static_cast<double>(data->sample_rate) / static_cast<double>(refresh);
            }
        }
    }

    // Track VU meters on the S16 data
    for (uint32_t i = 0; i < frames_generated * 2; i += 2) {
        int16_t abs_left = (data->temp_buffer[i] < 0) ? static_cast<int16_t>(-data->temp_buffer[i]) : data->temp_buffer[i];
        int16_t abs_right = (data->temp_buffer[i + 1] < 0) ? static_cast<int16_t>(-data->temp_buffer[i + 1]) : data->temp_buffer[i + 1];
        if (abs_left > peak_left) {
            peak_left = abs_left;
        }
        if (abs_right > peak_right) {
            peak_right = abs_right;
        }
    }

    // Convert peak to 0-255 VU value (shift right by 7 to get top 8 bits of 15-bit value)
    data->vu_left = static_cast<uint8_t>((peak_left >> 7) & 0xFF);
    data->vu_right = static_cast<uint8_t>((peak_right >> 7) & 0xFF);

    // Copy S16 from temp_buffer to output
    memcpy(dest.channels_output, data->temp_buffer, frames_generated * 2 * sizeof(int16_t));

    info.frame_count = frames_generated;

    if (finished) {
        info.status = RVReadStatus_Finished;
    }

    return info;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static int64_t adplug_seek(void* user_data, int64_t ms) {
    AdplugData* data = static_cast<AdplugData*>(user_data);

    if (data->player == nullptr || ms < 0) {
        return -1;
    }

    // AdPlug's seek() function handles seeking
    data->player->seek(static_cast<unsigned long>(ms));

    // Reset accumulator
    data->sample_accumulator = 0.0;

    return ms;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static int adplug_metadata(const char* url, const RVService* service_api) {
    const RVIo* io_api = RVService_get_io(service_api, RV_IO_API_VERSION);
    const RVMetadata* metadata_api = RVService_get_metadata(service_api, RV_METADATA_API_VERSION);

    if (io_api == nullptr || metadata_api == nullptr) {
        return -1;
    }

    // Load file
    RVIoReadUrlResult read_res = RVIo_read_url_to_memory(io_api, url);
    if (read_res.data == nullptr) {
        rv_error("adplug: Failed to load file for metadata: %s", url);
        return -1;
    }

    // Create temporary OPL and provider
    CEmuopl opl(SAMPLE_RATE, true, true);
    CProvider_Memory provider(read_res.data, read_res.data_size, url);

    // Create player
    CPlayer* player = CAdPlug::factory(url, &opl, CAdPlug::players, provider);
    if (player == nullptr) {
        RVIo_free_url_to_memory(io_api, read_res.data);
        return -1;
    }

    // Create metadata entry
    RVMetadataId index = RVMetadata_create_url(metadata_api, url);

    // Extract metadata
    std::string title = player->gettitle();
    if (!title.empty()) {
        RVMetadata_set_tag(metadata_api, index, RV_METADATA_TITLE_TAG, title.c_str());
    }

    std::string author = player->getauthor();
    if (!author.empty()) {
        RVMetadata_set_tag(metadata_api, index, RV_METADATA_ARTIST_TAG, author.c_str());
    }

    std::string desc = player->getdesc();
    if (!desc.empty()) {
        RVMetadata_set_tag(metadata_api, index, RV_METADATA_MESSAGE_TAG, desc.c_str());
    }

    std::string type = player->gettype();
    if (!type.empty()) {
        RVMetadata_set_tag(metadata_api, index, RV_METADATA_SONGTYPE_TAG, type.c_str());
    }

    // Get song length (in milliseconds, convert to seconds)
    unsigned long length_ms = player->songlength(-1);
    if (length_ms > 0) {
        double length_sec = static_cast<double>(length_ms) / 1000.0;
        RVMetadata_set_tag_f64(metadata_api, index, RV_METADATA_LENGTH_TAG, length_sec);
    }

    // Handle subsongs
    unsigned int num_subsongs = player->getsubsongs();
    if (num_subsongs > 1) {
        for (unsigned int i = 0; i < num_subsongs; i++) {
            player->rewind(static_cast<int>(i));
            unsigned long subsong_len = player->songlength(static_cast<int>(i));
            float len_sec = static_cast<float>(subsong_len) / 1000.0f;
            RVMetadata_add_subsong(metadata_api, index, i, "", len_sec);
        }
    }

    // Extract instrument names if available
    unsigned int num_instruments = player->getinstruments();
    for (unsigned int i = 0; i < num_instruments && i < 32; i++) {
        std::string inst_name = player->getinstrument(i);
        if (!inst_name.empty()) {
            RVMetadata_add_instrument(metadata_api, index, inst_name.c_str());
        }
    }

    delete player;
    RVIo_free_url_to_memory(io_api, read_res.data);

    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void adplug_event(void* user_data, uint8_t* event_data, uint64_t len) {
    AdplugData* data = static_cast<AdplugData*>(user_data);

    if (len < 8 || data == nullptr) {
        return;
    }

    // Report VU meters based on recent peak amplitude
    event_data[0] = data->vu_left;
    event_data[1] = data->vu_right;
    event_data[2] = 0;
    event_data[3] = 0;

    // Report pattern/row info if available (some tracker-style formats)
    if (data->player != nullptr) {
        event_data[6] = static_cast<uint8_t>(data->player->getrow() & 0xFF);
        event_data[7] = static_cast<uint8_t>(data->player->getpattern() & 0xFF);
    } else {
        event_data[6] = 0;
        event_data[7] = 0;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void adplug_static_init(const RVService* service_api) {
    g_rv_log = RVService_get_log(service_api, RV_LOG_API_VERSION);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint32_t adplug_get_scope_data(void* user_data, int channel, float* buffer, uint32_t num_samples) {
    AdplugData* data = static_cast<AdplugData*>(user_data);
    if (data == nullptr || data->opl == nullptr || buffer == nullptr) {
        return 0;
    }

    if (!data->scope_enabled) {
        FMOPL_EnableScopeCapture(1);
        data->scope_enabled = true;
    }

    // Map plugin channel to chip_index (0 or 1) and channel within chip (0-8)
    int chip_index = channel / FMOPL_SCOPE_NUM_CHANNELS;
    int ch = channel % FMOPL_SCOPE_NUM_CHANNELS;

    return FMOPL_GetScopeData(chip_index, ch, buffer, num_samples);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint32_t adplug_get_scope_channel_names(void* user_data, const char** names, uint32_t max_channels) {
    (void)user_data;
    static char s_name_bufs[18][16];
    // Single OPL chip: 9 channels
    uint32_t count = FMOPL_SCOPE_NUM_CHANNELS;
    if (count > max_channels)
        count = max_channels;
    for (uint32_t i = 0; i < count; i++) {
        snprintf(s_name_bufs[i], sizeof(s_name_bufs[i]), "OPL %u", i + 1);
        names[i] = s_name_bufs[i];
    }
    return count;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static RVPlaybackPlugin g_adplug_plugin = {
    RV_PLAYBACK_PLUGIN_API_VERSION,
    "adplug",
    "0.1.0",
    "AdPlug 2.4",
    adplug_probe_can_play,
    adplug_supported_extensions,
    adplug_create,
    adplug_destroy,
    adplug_event,
    adplug_open,
    adplug_close,
    adplug_read_data,
    adplug_seek,
    adplug_metadata,
    adplug_static_init,
    nullptr, // settings_updated

    // Tracker visualization API - not supported (OPL emulation)
    nullptr, // get_tracker_info
    nullptr, // get_pattern_cell
    nullptr, // get_pattern_num_rows

    // Scope visualization API
    adplug_get_scope_data,
    nullptr, // static_destroy
    adplug_get_scope_channel_names,
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

extern "C" RV_EXPORT RVPlaybackPlugin* rv_playback_plugin(void) {
    return &g_adplug_plugin;
}
