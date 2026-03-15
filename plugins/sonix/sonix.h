// SPDX-License-Identifier: MIT

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SonixSong SonixSong;

typedef enum SonixFormat {
    SONIX_FORMAT_UNKNOWN = 0,
    SONIX_FORMAT_SNX,
    SONIX_FORMAT_SMUS,
    SONIX_FORMAT_TINY,
} SonixFormat;

typedef struct SonixSongMetadata {
    uint8_t num_channels;
    SonixFormat format;
    bool has_form_header;
    bool valid;
    uint32_t num_ins_chunks;
    uint32_t num_real_samples;
    uint32_t num_track_chunks;
} SonixSongMetadata;

// I/O callback: load file at path into malloc'd buffer. Caller frees *out_data.
typedef bool (*SonixReadFileFn)(const char* path, uint8_t** out_data, uint32_t* out_size, void* user_data);

// Directory listing callback: call visitor(filename, ctx) for each file in dir.
typedef int (*SonixListDirFn)(const char* dir_path, void (*visitor)(const char* filename, void* ctx), void* ctx,
                              void* user_data);

typedef struct SonixIoCallbacks {
    SonixReadFileFn read_file;
    SonixListDirFn list_dir;
    void* user_data;
} SonixIoCallbacks;

SonixSong* sonix_song_create(const uint8_t* data, uint32_t size, const SonixIoCallbacks* io);
void sonix_song_destroy(SonixSong* song);

void sonix_song_set_sample_rate(SonixSong* song, uint32_t rate);
void sonix_song_set_solo_channel(SonixSong* song, int32_t channel);
void sonix_song_set_stereo_mix(SonixSong* song, float mix);
void sonix_song_start(SonixSong* song);
int sonix_song_decode(SonixSong* song, float* buffer, int num_frames);
bool sonix_song_is_finished(const SonixSong* song);

const SonixSongMetadata* sonix_song_get_metadata(const SonixSong* song);
const char* sonix_format_name(SonixFormat format);
const char* sonix_song_get_error(const SonixSong* song);
const char* sonix_song_get_instrument_name(const SonixSong* song, uint8_t instrument_index);

// Load all sidecar instruments. Returns count loaded.
int sonix_song_load_instruments(SonixSong* song, const char* song_file_path);

#ifdef __cplusplus
}
#endif
