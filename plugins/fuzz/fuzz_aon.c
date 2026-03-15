// SPDX-License-Identifier: MIT
// libFuzzer harness for Art of Noise (AoN) parser and decoder.
//
// Exercises the full load -> start -> decode -> destroy path.
// Build: cmake -B build_fuzz -G Ninja -DENABLE_FUZZ=ON && cmake --build build_fuzz
// Run:   ./build_fuzz/fuzz/fuzz_aon fuzz/corpus/art_of_noise -max_len=1048576

#include "aon_player.h"
#include <stddef.h>
#include <stdint.h>

// Cap decode iterations to prevent timeout on pathological inputs
#define FUZZ_MAX_DECODE_FRAMES 4096
#define FUZZ_MAX_DECODE_CALLS 16

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    // Reject absurdly large inputs early (4 MiB)
    if (size > 4 * 1024 * 1024) {
        return 0;
    }

    AonSong* song = aon_song_create(data, (uint32_t)size);
    if (!song) {
        return 0;
    }

    // Exercise metadata queries
    (void)aon_song_get_metadata(song);

    // Exercise pattern data queries
    AonPatternCell cell;
    aon_song_get_pattern_cell(song, 0, 0, 0, &cell);

    uint8_t pattern_num;
    aon_song_get_position_pattern(song, 0, &pattern_num);

    uint8_t arp[AON_ARPEGGIO_LENGTH];
    aon_song_get_arpeggio(song, 0, arp);

    // Exercise playback path
    aon_song_set_sample_rate(song, 44100);
    aon_song_start(song);

    float buffer[FUZZ_MAX_DECODE_FRAMES * 2]; // stereo
    int calls = 0;

    while (!aon_song_is_finished(song) && calls < FUZZ_MAX_DECODE_CALLS) {
        aon_song_decode(song, buffer, FUZZ_MAX_DECODE_FRAMES);
        calls++;
    }

    aon_song_destroy(song);
    return 0;
}
