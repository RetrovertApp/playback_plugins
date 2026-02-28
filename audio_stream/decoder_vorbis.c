///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// OGG Vorbis Decoder
//
// Wrapper around stb_vorbis for decoding OGG Vorbis audio files.
// Uses memory-based decoding for simplicity.
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// C11 nullptr compatibility
#ifndef nullptr
#define nullptr ((void*)0)
#endif

// stb_vorbis configuration - no pushdata API, use only pulldata
#define STB_VORBIS_NO_PUSHDATA_API
#include "external/stb_vorbis.c"

#include "decoder.h"

#include <stdlib.h>
#include <string.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct VorbisDecoderData {
    stb_vorbis* vorbis;
    stb_vorbis_info info;
    uint64_t current_frame;
    uint64_t total_frames;
} VorbisDecoderData;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint64_t vorbis_read_frames(void* decoder_data, float* output, uint64_t frames_to_read) {
    VorbisDecoderData* data = (VorbisDecoderData*)decoder_data;

    // stb_vorbis_get_samples_float_interleaved expects number of samples (frames * channels)
    int channels = data->info.channels;
    int samples_to_read = (int)(frames_to_read * (uint64_t)channels);

    int samples_read = stb_vorbis_get_samples_float_interleaved(data->vorbis, channels, output, samples_to_read);

    uint64_t frames_read = (uint64_t)samples_read;
    data->current_frame += frames_read;
    return frames_read;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool vorbis_seek_to_frame(void* decoder_data, uint64_t frame) {
    VorbisDecoderData* data = (VorbisDecoderData*)decoder_data;
    if (stb_vorbis_seek(data->vorbis, (unsigned int)frame)) {
        data->current_frame = frame;
        return true;
    }
    return false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void vorbis_close(void* decoder_data) {
    VorbisDecoderData* data = (VorbisDecoderData*)decoder_data;
    if (data->vorbis != nullptr) {
        stb_vorbis_close(data->vorbis);
    }
    free(data);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

AudioStreamDecoder* decoder_vorbis_open(const uint8_t* data, uint64_t size) {
    if (data == nullptr || size == 0) {
        return nullptr;
    }

    int error = 0;
    stb_vorbis* vorbis = stb_vorbis_open_memory(data, (int)size, &error, nullptr);
    if (vorbis == nullptr) {
        return nullptr;
    }

    VorbisDecoderData* vorbis_data = (VorbisDecoderData*)malloc(sizeof(VorbisDecoderData));
    if (vorbis_data == nullptr) {
        stb_vorbis_close(vorbis);
        return nullptr;
    }
    memset(vorbis_data, 0, sizeof(VorbisDecoderData));

    vorbis_data->vorbis = vorbis;
    vorbis_data->info = stb_vorbis_get_info(vorbis);
    vorbis_data->total_frames = (uint64_t)stb_vorbis_stream_length_in_samples(vorbis);

    AudioStreamDecoder* decoder = (AudioStreamDecoder*)malloc(sizeof(AudioStreamDecoder));
    if (decoder == nullptr) {
        stb_vorbis_close(vorbis);
        free(vorbis_data);
        return nullptr;
    }

    decoder->format = AudioStreamFormat_Vorbis;
    decoder->decoder_data = vorbis_data;
    decoder->sample_rate = vorbis_data->info.sample_rate;
    decoder->channels = (uint32_t)vorbis_data->info.channels;
    decoder->total_frames = vorbis_data->total_frames;
    decoder->read_frames = vorbis_read_frames;
    decoder->seek_to_frame = vorbis_seek_to_frame;
    decoder->close = vorbis_close;

    return decoder;
}
