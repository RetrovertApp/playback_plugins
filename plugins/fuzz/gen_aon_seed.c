// Generates a minimal valid AON4 seed corpus file for fuzzing.
// Build:  cc -o gen_aon_seed gen_aon_seed.c && ./gen_aon_seed
// Output: corpus/art_of_noise/minimal.aon

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void write_be32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

int main(void) {
    // AON4 header: 4 bytes magic + 42 bytes author = 46 bytes
    uint8_t buf[4096];
    memset(buf, 0, sizeof(buf));
    int pos = 0;

    // Magic
    memcpy(buf + pos, "AON4", 4);
    pos += 4;

    // Author (42 bytes, zero-filled)
    pos += 42;

    // INFO chunk: version=1, num_positions=1, restart_position=0
    memcpy(buf + pos, "INFO", 4);
    pos += 4;
    write_be32(buf + pos, 3);
    pos += 4;
    buf[pos++] = 1; // version
    buf[pos++] = 1; // num_positions
    buf[pos++] = 0; // restart_position

    // ARPG chunk: 16 arpeggios * 4 bytes = 64 bytes
    memcpy(buf + pos, "ARPG", 4);
    pos += 4;
    write_be32(buf + pos, 64);
    pos += 4;
    memset(buf + pos, 0, 64);
    pos += 64;

    // PLST chunk: 1 entry pointing to pattern 0
    memcpy(buf + pos, "PLST", 4);
    pos += 4;
    write_be32(buf + pos, 1);
    pos += 4;
    buf[pos++] = 0; // position 0 -> pattern 0

    // PATT chunk: 1 pattern * 64 rows * 4 channels * 4 bytes = 1024 bytes
    uint32_t patt_size = 4 * 4 * 64; // 4 channels, 64 rows, 4 bytes per cell
    memcpy(buf + pos, "PATT", 4);
    pos += 4;
    write_be32(buf + pos, patt_size);
    pos += 4;
    // Leave pattern data as zeros (empty pattern)
    memset(buf + pos, 0, patt_size);
    // Put a speed command in first cell: effect=F (15), arg=6
    buf[pos + 2] = 15;  // effect = set speed
    buf[pos + 3] = 6;   // speed = 6
    pos += (int)patt_size;

    // INST chunk: 1 instrument * 32 bytes
    memcpy(buf + pos, "INST", 4);
    pos += 4;
    write_be32(buf + pos, 32);
    pos += 4;
    memset(buf + pos, 0, 32);
    buf[pos + 0] = 0;  // type = sample
    buf[pos + 1] = 64; // volume
    // start_offset=0, length=64 words, loop_start=0, loop_length=0
    write_be32(buf + pos + 8, 64); // length in words
    pos += 32;

    // WLEN chunk: 1 waveform, length = 128 bytes (64 words)
    memcpy(buf + pos, "WLEN", 4);
    pos += 4;
    write_be32(buf + pos, 4);
    pos += 4;
    write_be32(buf + pos, 128); // waveform length in bytes
    pos += 4;

    // WAVE chunk: 128 bytes of sample data (simple sine-ish)
    memcpy(buf + pos, "WAVE", 4);
    pos += 4;
    write_be32(buf + pos, 128);
    pos += 4;
    for (int i = 0; i < 128; i++) {
        // Simple triangle wave
        int val = (i < 64) ? (i * 2 - 64) : (192 - i * 2);
        buf[pos + i] = (uint8_t)(val & 0xff);
    }
    pos += 128;

    FILE* f = fopen("corpus/art_of_noise/minimal.aon", "wb");
    if (!f) {
        fprintf(stderr, "Failed to create output file. Run from fuzz/ directory.\n");
        return 1;
    }
    fwrite(buf, 1, (size_t)pos, f);
    fclose(f);

    printf("Generated minimal.aon (%d bytes)\n", pos);
    return 0;
}
