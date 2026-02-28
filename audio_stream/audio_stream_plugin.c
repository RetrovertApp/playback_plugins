///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Audio Stream Playback Plugin
//
// Implements RVPlaybackPlugin interface for standard streaming audio formats:
// MP3, FLAC, WAV, and OGG Vorbis.
// Uses lightweight single-header libraries (dr_libs, stb_vorbis).
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

#include "decoder.h"
#include "metadata.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Module-local globals for API access

RV_PLUGIN_USE_IO_API();
RV_PLUGIN_USE_METADATA_API();
RV_PLUGIN_USE_LOG_API();

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Plugin instance data

#define AS_SCOPE_BUFFER_SIZE 1024
#define AS_SCOPE_BUFFER_MASK (AS_SCOPE_BUFFER_SIZE - 1)
#define AS_SCOPE_MAX_CHANNELS 2

typedef struct AudioStreamData {
    AudioStreamDecoder* decoder;
    const uint8_t* file_data;
    uint64_t file_size;
    bool scope_enabled;
    float scope_buffer[AS_SCOPE_MAX_CHANNELS][AS_SCOPE_BUFFER_SIZE];
    uint32_t scope_write_pos[AS_SCOPE_MAX_CHANNELS];
} AudioStreamData;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Helper: case-insensitive string comparison for extension matching

static int strcasecmp_local(const char* a, const char* b) {
    while (*a && *b) {
        int diff = tolower((unsigned char)*a) - tolower((unsigned char)*b);
        if (diff != 0) {
            return diff;
        }
        a++;
        b++;
    }
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
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
// Format detection by magic bytes

AudioStreamFormat audio_stream_detect_format(const uint8_t* data, uint64_t size, const char* filename) {
    if (data == nullptr || size < 4) {
        return AudioStreamFormat_Unknown;
    }

    // Check FLAC magic: "fLaC"
    if (size >= 4 && data[0] == 'f' && data[1] == 'L' && data[2] == 'a' && data[3] == 'C') {
        return AudioStreamFormat_FLAC;
    }

    // Check OGG magic: "OggS"
    if (size >= 4 && data[0] == 'O' && data[1] == 'g' && data[2] == 'g' && data[3] == 'S') {
        return AudioStreamFormat_Vorbis;
    }

    // Check WAV magic: "RIFF....WAVE"
    if (size >= 12 && data[0] == 'R' && data[1] == 'I' && data[2] == 'F' && data[3] == 'F' && data[8] == 'W'
        && data[9] == 'A' && data[10] == 'V' && data[11] == 'E') {
        return AudioStreamFormat_WAV;
    }

    // Check AIFF magic: "FORM....AIFF"
    if (size >= 12 && data[0] == 'F' && data[1] == 'O' && data[2] == 'R' && data[3] == 'M' && data[8] == 'A'
        && data[9] == 'I' && data[10] == 'F' && data[11] == 'F') {
        return AudioStreamFormat_WAV; // dr_wav handles AIFF
    }

    // Check MP3: ID3 tag or frame sync
    // ID3v2 tag at start
    if (size >= 3 && data[0] == 'I' && data[1] == 'D' && data[2] == '3') {
        return AudioStreamFormat_MP3;
    }

    // MP3 frame sync (0xFF followed by 0xE* or 0xF*)
    // Frame sync patterns: 0xFF 0xFB, 0xFF 0xFA, 0xFF 0xF3, 0xFF 0xF2, 0xFF 0xE*
    if (size >= 2 && data[0] == 0xFF && (data[1] & 0xE0) == 0xE0) {
        return AudioStreamFormat_MP3;
    }

    // Fallback to extension-based detection
    const char* ext = get_extension(filename);
    if (ext != nullptr) {
        if (strcasecmp_local(ext, "mp3") == 0) {
            return AudioStreamFormat_MP3;
        }
        if (strcasecmp_local(ext, "flac") == 0) {
            return AudioStreamFormat_FLAC;
        }
        if (strcasecmp_local(ext, "wav") == 0) {
            return AudioStreamFormat_WAV;
        }
        if (strcasecmp_local(ext, "ogg") == 0 || strcasecmp_local(ext, "oga") == 0) {
            return AudioStreamFormat_Vorbis;
        }
        if (strcasecmp_local(ext, "aiff") == 0 || strcasecmp_local(ext, "aif") == 0) {
            return AudioStreamFormat_WAV;
        }
    }

    return AudioStreamFormat_Unknown;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// RVPlaybackPlugin implementation

static const char* audio_stream_supported_extensions(void) {
    return "mp3,flac,wav,ogg,oga,aiff,aif";
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void* audio_stream_create(const RVService* service_api) {
    AudioStreamData* data = (AudioStreamData*)malloc(sizeof(AudioStreamData));
    if (data == nullptr) {
        return nullptr;
    }
    memset(data, 0, sizeof(AudioStreamData));

    return data;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static int audio_stream_destroy(void* user_data) {
    AudioStreamData* data = (AudioStreamData*)user_data;
    if (data == nullptr) {
        return 0;
    }

    if (data->decoder != nullptr) {
        data->decoder->close(data->decoder->decoder_data);
        free(data->decoder);
    }

    if (data->file_data != nullptr) {
        rv_io_free_url_to_memory((void*)data->file_data);
    }

    free(data);
    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static RVProbeResult audio_stream_probe_can_play(uint8_t* data, uint64_t data_size, const char* filename,
                                                 uint64_t total_size) {
    (void)total_size;

    AudioStreamFormat format = audio_stream_detect_format(data, data_size, filename);
    if (format != AudioStreamFormat_Unknown) {
        return RVProbeResult_Supported;
    }
    return RVProbeResult_Unsupported;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static int audio_stream_open(void* user_data, const char* url, uint32_t subsong, const RVService* service_api) {
    (void)subsong;
    (void)service_api;

    AudioStreamData* data = (AudioStreamData*)user_data;

    // Close any previously open decoder
    if (data->decoder != nullptr) {
        data->decoder->close(data->decoder->decoder_data);
        free(data->decoder);
        data->decoder = nullptr;
    }

    if (data->file_data != nullptr) {
        rv_io_free_url_to_memory((void*)data->file_data);
        data->file_data = nullptr;
    }

    // Load file into memory
    RVIoReadUrlResult read_res = rv_io_read_url_to_memory(url);
    if (read_res.data == nullptr) {
        rv_error("Failed to load %s to memory", url);
        return -1;
    }

    data->file_data = (const uint8_t*)read_res.data;
    data->file_size = read_res.data_size;

    // Detect format and create appropriate decoder
    AudioStreamFormat format = audio_stream_detect_format(data->file_data, data->file_size, url);

    switch (format) {
        case AudioStreamFormat_MP3:
            data->decoder = decoder_mp3_open(data->file_data, data->file_size);
            break;
        case AudioStreamFormat_FLAC:
            data->decoder = decoder_flac_open(data->file_data, data->file_size);
            break;
        case AudioStreamFormat_WAV:
            data->decoder = decoder_wav_open(data->file_data, data->file_size);
            break;
        case AudioStreamFormat_Vorbis:
            data->decoder = decoder_vorbis_open(data->file_data, data->file_size);
            break;
        default:
            rv_error("Unknown audio format for %s", url);
            rv_io_free_url_to_memory((void*)data->file_data);
            data->file_data = nullptr;
            return -1;
    }

    if (data->decoder == nullptr) {
        rv_error("Failed to create decoder for %s", url);
        rv_io_free_url_to_memory((void*)data->file_data);
        data->file_data = nullptr;
        return -1;
    }

    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void audio_stream_close(void* user_data) {
    AudioStreamData* data = (AudioStreamData*)user_data;

    if (data->decoder != nullptr) {
        data->decoder->close(data->decoder->decoder_data);
        free(data->decoder);
        data->decoder = nullptr;
    }

    if (data->file_data != nullptr) {
        rv_io_free_url_to_memory((void*)data->file_data);
        data->file_data = nullptr;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static RVReadInfo audio_stream_read_data(void* user_data, RVReadData dest) {
    AudioStreamData* data = (AudioStreamData*)user_data;

    if (data->decoder == nullptr) {
        return (RVReadInfo) { { RVAudioStreamFormat_F32, 2, 48000 }, 0, RVReadStatus_Error, 0 };
    }

    AudioStreamDecoder* decoder = data->decoder;
    float* output = (float*)dest.channels_output;

    // Calculate max frames based on output buffer size and decoder channels
    uint32_t max_frames = dest.channels_output_max_bytes_size / (sizeof(float) * decoder->channels);

    // Read frames from decoder
    uint64_t frames_read = decoder->read_frames(decoder->decoder_data, output, max_frames);

    // Capture decoded samples into scope ring buffers
    if (data->scope_enabled && frames_read > 0) {
        uint32_t channels = decoder->channels;
        if (channels > AS_SCOPE_MAX_CHANNELS) {
            channels = AS_SCOPE_MAX_CHANNELS;
        }
        for (uint32_t i = 0; i < (uint32_t)frames_read; i++) {
            for (uint32_t ch = 0; ch < channels; ch++) {
                data->scope_buffer[ch][data->scope_write_pos[ch] & AS_SCOPE_BUFFER_MASK]
                    = output[i * decoder->channels + ch];
                data->scope_write_pos[ch]++;
            }
        }
    }

    // Determine status
    RVReadStatus status = RVReadStatus_Ok;
    if (frames_read == 0) {
        status = RVReadStatus_Finished;
    }

    RVAudioFormat format = { RVAudioStreamFormat_F32, (uint8_t)decoder->channels, decoder->sample_rate };

    return (RVReadInfo) { format, (uint32_t)frames_read, status};
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static int64_t audio_stream_seek(void* user_data, int64_t ms) {
    AudioStreamData* data = (AudioStreamData*)user_data;

    if (data->decoder == nullptr || ms < 0) {
        return -1;
    }

    AudioStreamDecoder* decoder = data->decoder;

    // Convert milliseconds to frame number
    uint64_t target_frame = (uint64_t)((ms * (int64_t)decoder->sample_rate) / 1000);

    // Clamp to valid range
    if (target_frame > decoder->total_frames) {
        target_frame = decoder->total_frames;
    }

    if (decoder->seek_to_frame(decoder->decoder_data, target_frame)) {
        // Return the actual position in ms
        return (int64_t)((target_frame * 1000) / decoder->sample_rate);
    }

    return -1;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Helper: extract filename from URL for metadata fallback

static const char* get_filename_from_url(const char* url) {
    if (url == nullptr) {
        return nullptr;
    }
    const char* last_slash = strrchr(url, '/');
    if (last_slash != nullptr) {
        return last_slash + 1;
    }
    const char* last_backslash = strrchr(url, '\\');
    if (last_backslash != nullptr) {
        return last_backslash + 1;
    }
    return url;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static int audio_stream_metadata(const char* url, const RVService* service_api) {
    (void)service_api;

    // Load file to get duration
    RVIoReadUrlResult read_res = rv_io_read_url_to_memory(url);
    if (read_res.data == nullptr) {
        rv_error("Failed to load %s for metadata", url);
        return -1;
    }

    const uint8_t* file_data = (const uint8_t*)read_res.data;
    uint64_t file_size = read_res.data_size;

    AudioStreamFormat format = audio_stream_detect_format(file_data, file_size, url);
    AudioStreamDecoder* decoder = nullptr;

    switch (format) {
        case AudioStreamFormat_MP3:
            decoder = decoder_mp3_open(file_data, file_size);
            break;
        case AudioStreamFormat_FLAC:
            decoder = decoder_flac_open(file_data, file_size);
            break;
        case AudioStreamFormat_WAV:
            decoder = decoder_wav_open(file_data, file_size);
            break;
        case AudioStreamFormat_Vorbis:
            decoder = decoder_vorbis_open(file_data, file_size);
            break;
        default:
            rv_io_free_url_to_memory(read_res.data);
            return -1;
    }

    if (decoder == nullptr) {
        rv_io_free_url_to_memory(read_res.data);
        return -1;
    }

    // Calculate length in seconds
    float length = 0.0f;
    if (decoder->sample_rate > 0) {
        length = (float)decoder->total_frames / (float)decoder->sample_rate;
    }

    // Get format name
    const char* format_name = "Unknown";
    switch (format) {
        case AudioStreamFormat_MP3:
            format_name = "MP3";
            break;
        case AudioStreamFormat_FLAC:
            format_name = "FLAC";
            break;
        case AudioStreamFormat_WAV:
            format_name = "WAV";
            break;
        case AudioStreamFormat_Vorbis:
            format_name = "OGG Vorbis";
            break;
        default:
            break;
    }

    // Extract format-specific metadata
    AudioMetadata audio_meta;
    bool has_tags = false;

    switch (format) {
        case AudioStreamFormat_MP3:
            has_tags = metadata_extract_id3(file_data, file_size, &audio_meta);
            break;
        case AudioStreamFormat_FLAC:
            has_tags = metadata_extract_flac_comments(file_data, file_size, &audio_meta);
            break;
        case AudioStreamFormat_Vorbis:
            has_tags = metadata_extract_vorbis_comments(file_data, file_size, &audio_meta);
            break;
        default:
            break;
    }

    // Set metadata
    RVMetadataId index = rv_metadata_create_url(url);

    // Use extracted title or fall back to filename
    if (has_tags && audio_meta.title[0] != '\0') {
        rv_metadata_set_tag(index, RV_METADATA_TITLE_TAG, audio_meta.title);
    } else {
        const char* filename = get_filename_from_url(url);
        if (filename != nullptr) {
            rv_metadata_set_tag(index, RV_METADATA_TITLE_TAG, filename);
        }
    }

    // Set artist if available
    if (has_tags && audio_meta.artist[0] != '\0') {
        rv_metadata_set_tag(index, RV_METADATA_ARTIST_TAG, audio_meta.artist);
    }

    // Set album if available
    if (has_tags && audio_meta.album[0] != '\0') {
        rv_metadata_set_tag(index, RV_METADATA_ALBUM_TAG, audio_meta.album);
    }

    // Set date if available
    if (has_tags && audio_meta.date[0] != '\0') {
        rv_metadata_set_tag(index, RV_METADATA_DATE_TAG, audio_meta.date);
    }

    // Set genre if available
    if (has_tags && audio_meta.genre[0] != '\0') {
        rv_metadata_set_tag(index, RV_METADATA_GENRE_TAG, audio_meta.genre);
    }

    rv_metadata_set_tag(index, RV_METADATA_SONGTYPE_TAG, format_name);
    rv_metadata_set_tag_f64(index, RV_METADATA_LENGTH_TAG, (double)length);

    // Clean up
    decoder->close(decoder->decoder_data);
    free(decoder);
    rv_io_free_url_to_memory(read_res.data);

    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void audio_stream_event(void* user_data, uint8_t* data, uint64_t len) {
    (void)user_data;
    (void)data;
    (void)len;
    // No VU meters or other events for streaming formats
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void audio_stream_static_init(const RVService* service_api) {
    rv_init_log_api(service_api);
    rv_init_io_api(service_api);
    rv_init_metadata_api(service_api);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint32_t audio_stream_get_scope_data(void* user_data, int channel, float* buffer, uint32_t num_samples) {
    AudioStreamData* data = (AudioStreamData*)user_data;
    if (data == nullptr || data->decoder == nullptr || buffer == nullptr) {
        return 0;
    }

    if (channel < 0 || channel >= AS_SCOPE_MAX_CHANNELS || channel >= (int)data->decoder->channels) {
        return 0;
    }

    if (!data->scope_enabled) {
        memset(data->scope_buffer, 0, sizeof(data->scope_buffer));
        memset(data->scope_write_pos, 0, sizeof(data->scope_write_pos));
        data->scope_enabled = true;
    }

    if (num_samples > AS_SCOPE_BUFFER_SIZE) {
        num_samples = AS_SCOPE_BUFFER_SIZE;
    }

    uint32_t wp = data->scope_write_pos[channel];
    uint32_t read_pos = (wp - num_samples) & AS_SCOPE_BUFFER_MASK;
    for (uint32_t i = 0; i < num_samples; i++) {
        buffer[i] = data->scope_buffer[channel][read_pos];
        read_pos = (read_pos + 1) & AS_SCOPE_BUFFER_MASK;
    }

    return num_samples;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint32_t audio_stream_get_scope_channel_names(void* user_data, const char** names, uint32_t max_channels) {
    AudioStreamData* data = (AudioStreamData*)user_data;
    if (data == NULL || data->decoder == NULL)
        return 0;

    uint32_t channels = data->decoder->channels;
    if (channels > AS_SCOPE_MAX_CHANNELS)
        channels = AS_SCOPE_MAX_CHANNELS;
    if (channels > max_channels)
        channels = max_channels;

    if (channels == 1) {
        names[0] = "Mono";
    } else {
        names[0] = "Left";
        if (channels > 1)
            names[1] = "Right";
    }
    return channels;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static RVPlaybackPlugin g_audio_stream_plugin = {
    RV_PLAYBACK_PLUGIN_API_VERSION,
    "audio_stream",
    "0.1.0",
    "dr_libs + stb_vorbis",
    audio_stream_probe_can_play,
    audio_stream_supported_extensions,
    audio_stream_create,
    audio_stream_destroy,
    audio_stream_event,
    audio_stream_open,
    audio_stream_close,
    audio_stream_read_data,
    audio_stream_seek,
    audio_stream_metadata,
    audio_stream_static_init,
    NULL, // settings_updated

    // Tracker visualization API - not supported
    NULL, // get_tracker_info
    NULL, // get_pattern_cell
    NULL, // get_pattern_num_rows
    audio_stream_get_scope_data,
    NULL, // static_destroy
    audio_stream_get_scope_channel_names,
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RV_EXPORT RVPlaybackPlugin* rv_playback_plugin(void) {
    return &g_audio_stream_plugin;
}
