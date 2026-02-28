///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Audio Stream Decoder Interface
//
// Common interface for audio format decoders (MP3, FLAC, WAV, OGG Vorbis).
// Each decoder wraps a single-header library and provides a uniform API.
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Supported audio formats

typedef enum AudioStreamFormat {
    AudioStreamFormat_Unknown = 0,
    AudioStreamFormat_MP3,
    AudioStreamFormat_FLAC,
    AudioStreamFormat_WAV,
    AudioStreamFormat_Vorbis,
} AudioStreamFormat;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Forward declaration

typedef struct AudioStreamDecoder AudioStreamDecoder;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Function pointer types for decoder operations

typedef uint64_t (*AudioDecoderReadFunc)(void* decoder_data, float* output, uint64_t frames_to_read);
typedef bool (*AudioDecoderSeekFunc)(void* decoder_data, uint64_t frame);
typedef void (*AudioDecoderCloseFunc)(void* decoder_data);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Decoder structure

struct AudioStreamDecoder {
    AudioStreamFormat format;
    void* decoder_data;
    uint32_t sample_rate;
    uint32_t channels;
    uint64_t total_frames;

    // Function pointers for format-specific operations
    AudioDecoderReadFunc read_frames;
    AudioDecoderSeekFunc seek_to_frame;
    AudioDecoderCloseFunc close;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Format detection
//
// Detects format by examining magic bytes at the start of the file.
// Falls back to extension-based detection if magic bytes are inconclusive.

AudioStreamFormat audio_stream_detect_format(const uint8_t* data, uint64_t size, const char* filename);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Decoder creation functions
//
// Each function takes the complete file data in memory and returns a decoder.
// Returns nullptr on failure (invalid format, corrupted data, etc.)

AudioStreamDecoder* decoder_mp3_open(const uint8_t* data, uint64_t size);
AudioStreamDecoder* decoder_flac_open(const uint8_t* data, uint64_t size);
AudioStreamDecoder* decoder_wav_open(const uint8_t* data, uint64_t size);
AudioStreamDecoder* decoder_vorbis_open(const uint8_t* data, uint64_t size);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#ifdef __cplusplus
}
#endif
