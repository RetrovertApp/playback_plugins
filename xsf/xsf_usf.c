///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// xSF USF Wrapper - Nintendo 64 emulation via lazyusf2
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// C11 nullptr compatibility
#ifndef nullptr
#define nullptr ((void*)0)
#endif

#include "xsf_usf.h"

#include <retrovert/log.h>

#include "usf/usf.h"

#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define strcasecmp _stricmp
#else
#include <strings.h>
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct XsfUsfState {
    void* emu_state;
    int enable_compare;
    int enable_fifo_full;
} XsfUsfState;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void* xsf_usf_create(void) {
    XsfUsfState* state = (XsfUsfState*)calloc(1, sizeof(XsfUsfState));
    return state;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// USF load callback - exe must be empty, all data in reserved section

int xsf_usf_load(void* context, const uint8_t* exe, size_t exe_size, const uint8_t* reserved, size_t reserved_size) {
    XsfUsfState* state = (XsfUsfState*)context;

    if (exe != nullptr && exe_size > 0) {
        return -1;
    }

    if (reserved != nullptr && reserved_size > 0) {
        return usf_upload_section(state->emu_state, reserved, reserved_size);
    }

    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int xsf_usf_start(void* state_ptr, int psf_version) {
    (void)psf_version;
    XsfUsfState* state = (XsfUsfState*)state_ptr;

    state->emu_state = malloc(usf_get_state_size());
    if (state->emu_state == nullptr) {
        return -1;
    }

    usf_clear(state->emu_state);
    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int xsf_usf_render(void* state_ptr, int16_t* buffer, int frames) {
    XsfUsfState* state = (XsfUsfState*)state_ptr;
    if (state->emu_state == nullptr) {
        return 0;
    }

    usf_set_hle_audio(state->emu_state, 1);
    usf_set_compare(state->emu_state, state->enable_compare);
    usf_set_fifo_full(state->emu_state, state->enable_fifo_full);

    const char* err = usf_render_resampled(state->emu_state, buffer, (size_t)frames, 48000);
    if (err != nullptr) {
        rv_error("USF: render error: %s", err);
        return 0;
    }

    return frames;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int xsf_usf_sample_rate(void* state) {
    (void)state;
    return 48000;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int xsf_usf_seek_reset(void* state_ptr) {
    XsfUsfState* state = (XsfUsfState*)state_ptr;
    if (state->emu_state == nullptr) {
        return -1;
    }

    usf_shutdown(state->emu_state);
    usf_clear(state->emu_state);
    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void xsf_usf_destroy(void* state_ptr) {
    XsfUsfState* state = (XsfUsfState*)state_ptr;
    if (state == nullptr) {
        return;
    }

    if (state->emu_state != nullptr) {
        usf_shutdown(state->emu_state);
        free(state->emu_state);
    }

    free(state);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int xsf_usf_info(void* state_ptr, const char* name, const char* value) {
    XsfUsfState* state = (XsfUsfState*)state_ptr;

    if (strcasecmp(name, "_enablecompare") == 0) {
        state->enable_compare = atoi(value);
    } else if (strcasecmp(name, "_enablefifofull") == 0) {
        state->enable_fifo_full = atoi(value);
    }

    return 0;
}
