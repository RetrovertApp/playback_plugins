///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// FLAC Decoder
//
// Wrapper around dr_flac for decoding FLAC audio files.
// Uses memory-based decoding for simplicity.
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// C11 nullptr compatibility
#ifndef nullptr
#define nullptr ((void*)0)
#endif

#define DR_FLAC_IMPLEMENTATION
#include "external/dr_flac.h"

#include "decoder.h"

#include <stdlib.h>
#include <string.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct FlacDecoderData {
    drflac* flac;
    uint64_t current_frame;
} FlacDecoderData;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint64_t flac_read_frames(void* decoder_data, float* output, uint64_t frames_to_read) {
    FlacDecoderData* data = (FlacDecoderData*)decoder_data;
    uint64_t frames_read = drflac_read_pcm_frames_f32(data->flac, frames_to_read, output);
    data->current_frame += frames_read;
    return frames_read;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool flac_seek_to_frame(void* decoder_data, uint64_t frame) {
    FlacDecoderData* data = (FlacDecoderData*)decoder_data;
    if (drflac_seek_to_pcm_frame(data->flac, frame)) {
        data->current_frame = frame;
        return true;
    }
    return false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void flac_close(void* decoder_data) {
    FlacDecoderData* data = (FlacDecoderData*)decoder_data;
    if (data->flac != nullptr) {
        drflac_close(data->flac);
    }
    free(data);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

AudioStreamDecoder* decoder_flac_open(const uint8_t* data, uint64_t size) {
    if (data == nullptr || size == 0) {
        return nullptr;
    }

    drflac* flac = drflac_open_memory(data, size, nullptr);
    if (flac == nullptr) {
        return nullptr;
    }

    FlacDecoderData* flac_data = (FlacDecoderData*)malloc(sizeof(FlacDecoderData));
    if (flac_data == nullptr) {
        drflac_close(flac);
        return nullptr;
    }
    memset(flac_data, 0, sizeof(FlacDecoderData));
    flac_data->flac = flac;

    AudioStreamDecoder* decoder = (AudioStreamDecoder*)malloc(sizeof(AudioStreamDecoder));
    if (decoder == nullptr) {
        drflac_close(flac);
        free(flac_data);
        return nullptr;
    }

    decoder->format = AudioStreamFormat_FLAC;
    decoder->decoder_data = flac_data;
    decoder->sample_rate = flac->sampleRate;
    decoder->channels = flac->channels;
    decoder->total_frames = flac->totalPCMFrameCount;
    decoder->read_frames = flac_read_frames;
    decoder->seek_to_frame = flac_seek_to_frame;
    decoder->close = flac_close;

    return decoder;
}
