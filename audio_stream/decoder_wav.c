///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// WAV/AIFF Decoder
//
// Wrapper around dr_wav for decoding WAV and AIFF audio files.
// Uses memory-based decoding for simplicity.
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// C11 nullptr compatibility
#ifndef nullptr
#define nullptr ((void*)0)
#endif

#define DR_WAV_IMPLEMENTATION
#include "external/dr_wav.h"

#include "decoder.h"

#include <stdlib.h>
#include <string.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct WavDecoderData {
    drwav wav;
    uint64_t current_frame;
} WavDecoderData;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint64_t wav_read_frames(void* decoder_data, float* output, uint64_t frames_to_read) {
    WavDecoderData* data = (WavDecoderData*)decoder_data;
    uint64_t frames_read = drwav_read_pcm_frames_f32(&data->wav, frames_to_read, output);
    data->current_frame += frames_read;
    return frames_read;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool wav_seek_to_frame(void* decoder_data, uint64_t frame) {
    WavDecoderData* data = (WavDecoderData*)decoder_data;
    if (drwav_seek_to_pcm_frame(&data->wav, frame)) {
        data->current_frame = frame;
        return true;
    }
    return false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void wav_close(void* decoder_data) {
    WavDecoderData* data = (WavDecoderData*)decoder_data;
    drwav_uninit(&data->wav);
    free(data);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

AudioStreamDecoder* decoder_wav_open(const uint8_t* data, uint64_t size) {
    if (data == nullptr || size == 0) {
        return nullptr;
    }

    WavDecoderData* wav_data = (WavDecoderData*)malloc(sizeof(WavDecoderData));
    if (wav_data == nullptr) {
        return nullptr;
    }
    memset(wav_data, 0, sizeof(WavDecoderData));

    if (!drwav_init_memory(&wav_data->wav, data, size, nullptr)) {
        free(wav_data);
        return nullptr;
    }

    AudioStreamDecoder* decoder = (AudioStreamDecoder*)malloc(sizeof(AudioStreamDecoder));
    if (decoder == nullptr) {
        drwav_uninit(&wav_data->wav);
        free(wav_data);
        return nullptr;
    }

    decoder->format = AudioStreamFormat_WAV;
    decoder->decoder_data = wav_data;
    decoder->sample_rate = wav_data->wav.sampleRate;
    decoder->channels = wav_data->wav.channels;
    decoder->total_frames = wav_data->wav.totalPCMFrameCount;
    decoder->read_frames = wav_read_frames;
    decoder->seek_to_frame = wav_seek_to_frame;
    decoder->close = wav_close;

    return decoder;
}
