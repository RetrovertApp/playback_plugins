///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// xSF QSF Wrapper - Capcom QSound emulation via highly_quixotic
//
// QSF (0x41) exe data uses a section-based format:
//   3-byte section name ("KEY", "Z80", "SMP") + 4-byte offset + 4-byte size + data
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// C11 nullptr compatibility
#ifndef nullptr
#define nullptr ((void*)0)
#endif

#include "xsf_qsf.h"

#include "xsf_common.h"

#include <retrovert/log.h>

#include "qsound.h"

#include <stdlib.h>
#include <string.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct XsfQsfState {
    uint8_t* qsound_state;
    // Accumulated section data
    uint8_t key_data[11];
    int has_key;
    uint8_t* z80_rom;
    size_t z80_size;
    uint8_t* sample_rom;
    size_t sample_size;
} XsfQsfState;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void* xsf_qsf_create(void) {
    XsfQsfState* state = (XsfQsfState*)calloc(1, sizeof(XsfQsfState));
    return state;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Parse section-based exe data: each section = 3-byte name + 4-byte offset + 4-byte size + data

int xsf_qsf_load(void* context, const uint8_t* exe, size_t exe_size, const uint8_t* reserved, size_t reserved_size) {
    (void)reserved;
    (void)reserved_size;
    XsfQsfState* state = (XsfQsfState*)context;

    if (exe == nullptr || exe_size < 11) {
        return 0;
    }

    size_t pos = 0;
    while (pos + 11 <= exe_size) {
        char name[4] = { (char)exe[pos], (char)exe[pos + 1], (char)exe[pos + 2], '\0' };
        uint32_t offset = xsf_get_le32(exe + pos + 3);
        uint32_t size = xsf_get_le32(exe + pos + 7);
        pos += 11;

        if (pos + size > exe_size) {
            break;
        }

        if (strcmp(name, "KEY") == 0) {
            if (size <= 11) {
                memcpy(state->key_data, exe + pos, size);
                state->has_key = 1;
            }
        } else if (strcmp(name, "Z80") == 0) {
            size_t needed = (size_t)offset + size;
            if (state->z80_rom == nullptr) {
                state->z80_rom = (uint8_t*)calloc(1, needed);
                if (state->z80_rom == nullptr) {
                    return -1;
                }
                state->z80_size = needed;
            } else if (state->z80_size < needed) {
                uint8_t* new_buf = (uint8_t*)realloc(state->z80_rom, needed);
                if (new_buf == nullptr) {
                    return -1;
                }
                memset(new_buf + state->z80_size, 0, needed - state->z80_size);
                state->z80_rom = new_buf;
                state->z80_size = needed;
            }
            memcpy(state->z80_rom + offset, exe + pos, size);
        } else if (strcmp(name, "SMP") == 0) {
            size_t needed = (size_t)offset + size;
            if (state->sample_rom == nullptr) {
                state->sample_rom = (uint8_t*)calloc(1, needed);
                if (state->sample_rom == nullptr) {
                    return -1;
                }
                state->sample_size = needed;
            } else if (state->sample_size < needed) {
                uint8_t* new_buf = (uint8_t*)realloc(state->sample_rom, needed);
                if (new_buf == nullptr) {
                    return -1;
                }
                memset(new_buf + state->sample_size, 0, needed - state->sample_size);
                state->sample_rom = new_buf;
                state->sample_size = needed;
            }
            memcpy(state->sample_rom + offset, exe + pos, size);
        }

        pos += size;
    }

    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int xsf_qsf_start(void* state_ptr, int psf_version) {
    (void)psf_version;
    qsound_init();
    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int xsf_qsf_post_load(void* state_ptr) {
    XsfQsfState* state = (XsfQsfState*)state_ptr;

    free(state->qsound_state);
    state->qsound_state = (uint8_t*)malloc(qsound_get_state_size());
    if (state->qsound_state == nullptr) {
        return -1;
    }

    qsound_clear_state(state->qsound_state);

    // Set Kabuki encryption key if present
    if (state->has_key && state->key_data[0]) {
        uint32_t swap_key1 = xsf_get_le32(state->key_data);
        uint32_t swap_key2 = xsf_get_le32(state->key_data + 4);
        uint16_t addr_key = (uint16_t)(state->key_data[8] | ((uint16_t)state->key_data[9] << 8));
        uint8_t xor_key = state->key_data[10];
        qsound_set_kabuki_key(state->qsound_state, swap_key1, swap_key2, addr_key, xor_key);
    } else {
        qsound_set_kabuki_key(state->qsound_state, 0, 0, 0, 0);
    }

    if (state->z80_rom != nullptr) {
        qsound_set_z80_rom(state->qsound_state, state->z80_rom, (uint32_t)state->z80_size);
    }

    if (state->sample_rom != nullptr) {
        qsound_set_sample_rom(state->qsound_state, state->sample_rom, (uint32_t)state->sample_size);
    }

    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int xsf_qsf_render(void* state_ptr, int16_t* buffer, int frames) {
    XsfQsfState* state = (XsfQsfState*)state_ptr;
    if (state->qsound_state == nullptr) {
        return 0;
    }

    unsigned int frames_u = (unsigned int)frames;
    qsound_execute(state->qsound_state, 0x7fffffff, buffer, &frames_u);
    return (int)frames_u;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int xsf_qsf_sample_rate(void* state) {
    (void)state;
    return 24038;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int xsf_qsf_seek_reset(void* state_ptr) {
    XsfQsfState* state = (XsfQsfState*)state_ptr;

    free(state->qsound_state);
    state->qsound_state = nullptr;

    free(state->z80_rom);
    free(state->sample_rom);
    state->z80_rom = nullptr;
    state->sample_rom = nullptr;
    state->z80_size = 0;
    state->sample_size = 0;
    state->has_key = 0;
    memset(state->key_data, 0, sizeof(state->key_data));

    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void xsf_qsf_destroy(void* state_ptr) {
    XsfQsfState* state = (XsfQsfState*)state_ptr;
    if (state == nullptr) {
        return;
    }

    free(state->qsound_state);
    free(state->z80_rom);
    free(state->sample_rom);
    free(state);
}
