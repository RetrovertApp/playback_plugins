///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// MP3 Decoder
//
// Wrapper around dr_mp3 for decoding MP3 audio files.
// Uses memory-based decoding for simplicity.
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// C11 nullptr compatibility
#ifndef nullptr
#define nullptr ((void*)0)
#endif

#define DR_MP3_IMPLEMENTATION
#include "external/dr_mp3.h"

#include "decoder.h"

#include <stdlib.h>
#include <string.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct Mp3DecoderData {
    drmp3 mp3;
    uint64_t current_frame;
} Mp3DecoderData;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint64_t mp3_read_frames(void* decoder_data, float* output, uint64_t frames_to_read) {
    Mp3DecoderData* data = (Mp3DecoderData*)decoder_data;
    uint64_t frames_read = drmp3_read_pcm_frames_f32(&data->mp3, frames_to_read, output);
    data->current_frame += frames_read;
    return frames_read;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool mp3_seek_to_frame(void* decoder_data, uint64_t frame) {
    Mp3DecoderData* data = (Mp3DecoderData*)decoder_data;
    if (drmp3_seek_to_pcm_frame(&data->mp3, frame)) {
        data->current_frame = frame;
        return true;
    }
    return false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void mp3_close(void* decoder_data) {
    Mp3DecoderData* data = (Mp3DecoderData*)decoder_data;
    drmp3_uninit(&data->mp3);
    free(data);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

AudioStreamDecoder* decoder_mp3_open(const uint8_t* data, uint64_t size) {
    if (data == nullptr || size == 0) {
        return nullptr;
    }

    Mp3DecoderData* mp3_data = (Mp3DecoderData*)malloc(sizeof(Mp3DecoderData));
    if (mp3_data == nullptr) {
        return nullptr;
    }
    memset(mp3_data, 0, sizeof(Mp3DecoderData));

    if (!drmp3_init_memory(&mp3_data->mp3, data, size, nullptr)) {
        free(mp3_data);
        return nullptr;
    }

    // Get total frame count (dr_mp3 needs to scan the file for this)
    uint64_t total_frames = drmp3_get_pcm_frame_count(&mp3_data->mp3);

    AudioStreamDecoder* decoder = (AudioStreamDecoder*)malloc(sizeof(AudioStreamDecoder));
    if (decoder == nullptr) {
        drmp3_uninit(&mp3_data->mp3);
        free(mp3_data);
        return nullptr;
    }

    decoder->format = AudioStreamFormat_MP3;
    decoder->decoder_data = mp3_data;
    decoder->sample_rate = mp3_data->mp3.sampleRate;
    decoder->channels = mp3_data->mp3.channels;
    decoder->total_frames = total_frames;
    decoder->read_frames = mp3_read_frames;
    decoder->seek_to_frame = mp3_seek_to_frame;
    decoder->close = mp3_close;

    return decoder;
}
