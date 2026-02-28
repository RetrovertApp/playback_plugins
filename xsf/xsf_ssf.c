///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// xSF SSF/DSF Wrapper - Sega Saturn/Dreamcast emulation via highly_theoretical
//
// SSF (0x11) = Sega Saturn, sega version 1
// DSF (0x12) = Sega Dreamcast, sega version 2
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// C11 nullptr compatibility
#ifndef nullptr
#define nullptr ((void*)0)
#endif

#include "xsf_ssf.h"

#include "xsf_common.h"

#include <retrovert/log.h>

#include "sega.h"

#include <stdlib.h>
#include <string.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct XsfSsfState {
    uint8_t* sega_state;
    uint8_t sega_version; // 1 = Saturn (SSF), 2 = Dreamcast (DSF)
    // Loader: accumulated program data (includes 4-byte address header)
    uint8_t* program;
    size_t program_size;
} XsfSsfState;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void* xsf_ssf_create(void) {
    XsfSsfState* state = (XsfSsfState*)calloc(1, sizeof(XsfSsfState));
    return state;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Load callback - accumulates program data from exe section.
// First 4 bytes of exe = start address (little-endian, masked to address space).
// Remaining bytes = program data placed at that address.

int xsf_ssf_load(void* context, const uint8_t* exe, size_t exe_size, const uint8_t* reserved, size_t reserved_size) {
    (void)reserved;
    (void)reserved_size;
    XsfSsfState* state = (XsfSsfState*)context;

    if (exe == nullptr || exe_size <= 4) {
        return 0;
    }

    uint32_t start = xsf_get_le32(exe) & 0x7fffff;
    size_t data_len = exe_size - 4;
    size_t needed = (size_t)start + data_len;

    if (state->program == nullptr) {
        state->program = (uint8_t*)calloc(1, needed);
        if (state->program == nullptr) {
            return -1;
        }
        state->program_size = needed;
    } else if (state->program_size < needed) {
        uint8_t* new_buf = (uint8_t*)realloc(state->program, needed);
        if (new_buf == nullptr) {
            return -1;
        }
        memset(new_buf + state->program_size, 0, needed - state->program_size);
        state->program = new_buf;
        state->program_size = needed;
    }

    memcpy(state->program + start, exe + 4, data_len);
    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int xsf_ssf_start(void* state_ptr, int psf_version) {
    XsfSsfState* state = (XsfSsfState*)state_ptr;

    // PSF version 0x11 = Saturn (sega ver 1), 0x12 = Dreamcast (sega ver 2)
    state->sega_version = (uint8_t)(psf_version - 0x10);

    sega_init();
    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int xsf_ssf_post_load(void* state_ptr) {
    XsfSsfState* state = (XsfSsfState*)state_ptr;

    // Free previous state if re-loading
    free(state->sega_state);

    state->sega_state = (uint8_t*)malloc(sega_get_state_size(state->sega_version));
    if (state->sega_state == nullptr) {
        return -1;
    }

    sega_clear_state(state->sega_state, state->sega_version);
    sega_enable_dry(state->sega_state, 1);
    sega_enable_dsp(state->sega_state, 1);
    sega_enable_dsp_dynarec(state->sega_state, 0);

    if (state->program != nullptr && state->program_size > 0) {
        sega_upload_program(state->sega_state, state->program, (uint32_t)state->program_size);
    }

    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int xsf_ssf_render(void* state_ptr, int16_t* buffer, int frames) {
    XsfSsfState* state = (XsfSsfState*)state_ptr;
    if (state->sega_state == nullptr) {
        return 0;
    }

    unsigned int frames_u = (unsigned int)frames;
    sega_execute(state->sega_state, 0x7fffffff, buffer, &frames_u);
    return (int)frames_u;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int xsf_ssf_sample_rate(void* state) {
    (void)state;
    return 44100;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int xsf_ssf_seek_reset(void* state_ptr) {
    XsfSsfState* state = (XsfSsfState*)state_ptr;

    free(state->program);
    state->program = nullptr;
    state->program_size = 0;

    free(state->sega_state);
    state->sega_state = nullptr;

    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void xsf_ssf_destroy(void* state_ptr) {
    XsfSsfState* state = (XsfSsfState*)state_ptr;
    if (state == nullptr) {
        return;
    }

    free(state->sega_state);
    free(state->program);
    free(state);
}
