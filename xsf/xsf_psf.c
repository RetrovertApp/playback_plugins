///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// xSF PSF/PSF2 Wrapper - PlayStation 1/2 emulation via highly_experimental
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// C11 nullptr compatibility
#ifndef nullptr
#define nullptr ((void*)0)
#endif

#include "xsf_psf.h"

#include <retrovert/log.h>

#include "bios.h"
#include "iop.h"
#include "psf2fs.h"
#include "psflib.h"
#include "psx.h"
#include "r3000.h"
#include "spu.h"

#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define strncasecmp _strnicmp
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Tracks whether psx_init() completed successfully. Checked by xsf_psf_start() to
// prevent calling psx_clear_state() when the library isn't initialized (which crashes
// via psx_hang). The BIOS pointer alone isn't sufficient: bios_set_image() succeeds
// even when psx_init() subsequently fails.

static int s_psx_lib_initialized = 0;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct XsfPsfState {
    uint8_t* psx_state;
    void* psf2fs;
    int psf_version;
    uint32_t refresh;
    int first_load;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int xsf_psf_init_bios(const uint8_t* bios_data, uint32_t bios_size) {
    // Verify the "Highly Experimental" banner at offset 0x80 before calling psx_init().
    // psx_init() -> getenvhex() -> bios_getenv() crashes via psx_hang() if this banner
    // is missing, and the app's SIGSEGV handler swallows the crash.
    if (bios_size < 0x100) {
        rv_error("PSF: BIOS too small (%u bytes)", bios_size);
        return -1;
    }

    const char* banner = "Highly Experimental";
    size_t banner_len = strlen(banner);
    if (memcmp(bios_data + 0x80, banner, banner_len) != 0) {
        rv_error("PSF: BIOS missing 'Highly Experimental' banner at offset 0x80");
        return -1;
    }

    bios_set_image((uint8*)(uintptr_t)bios_data, (uint32)bios_size);

    sint32 r = psx_init();
    if (r != 0) {
        rv_error("PSF: psx_init() failed with return value %d", (int)r);
        return -1;
    }

    s_psx_lib_initialized = 1;
    rv_info("PSF: highly_experimental library initialized successfully");
    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void* xsf_psf_create(void) {
    XsfPsfState* state = (XsfPsfState*)calloc(1, sizeof(XsfPsfState));
    if (state == nullptr) {
        return nullptr;
    }
    state->first_load = 1;
    return state;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// PSF1 load callback

int xsf_psf_load(void* context, const uint8_t* exe, size_t exe_size, const uint8_t* reserved, size_t reserved_size) {
    (void)reserved;
    (void)reserved_size;

    XsfPsfState* state = (XsfPsfState*)context;

    if (exe_size < 0x800) {
        return -1;
    }

    // PSX executable header
    uint32_t addr = exe[0x18] | ((uint32_t)exe[0x19] << 8) | ((uint32_t)exe[0x1a] << 16) | ((uint32_t)exe[0x1b] << 24);
    uint32_t size = (uint32_t)(exe_size - 0x800);

    addr &= 0x1fffff;
    if (addr < 0x10000 || size > 0x1f0000 || addr + size > 0x200000) {
        return -1;
    }

    void* iop = psx_get_iop_state(state->psx_state);
    iop_upload_to_ram(iop, addr, exe + 0x800, size);

    // Detect refresh rate from region string
    if (state->refresh == 0) {
        if (exe_size > 118 && strncasecmp((const char*)exe + 113, "Japan", 5) == 0) {
            state->refresh = 60;
        } else if (exe_size > 119 && strncasecmp((const char*)exe + 113, "Europe", 6) == 0) {
            state->refresh = 50;
        } else if (exe_size > 126 && strncasecmp((const char*)exe + 113, "North America", 13) == 0) {
            state->refresh = 60;
        }
    }

    if (state->first_load) {
        void* r3000 = iop_get_r3000_state(iop);
        uint32_t pc
            = exe[0x10] | ((uint32_t)exe[0x11] << 8) | ((uint32_t)exe[0x12] << 16) | ((uint32_t)exe[0x13] << 24);
        uint32_t sp
            = exe[0x30] | ((uint32_t)exe[0x31] << 8) | ((uint32_t)exe[0x32] << 16) | ((uint32_t)exe[0x33] << 24);
        r3000_setreg(r3000, R3000_REG_PC, pc);
        r3000_setreg(r3000, R3000_REG_GEN + 29, sp);
        state->first_load = 0;
    }

    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// PSF2 load callback - delegates to psf2fs

int xsf_psf2_load(void* context, const uint8_t* exe, size_t exe_size, const uint8_t* reserved, size_t reserved_size) {
    XsfPsfState* state = (XsfPsfState*)context;
    if (state->psf2fs == nullptr) {
        return -1;
    }
    return psf2fs_load_callback(state->psf2fs, exe, exe_size, reserved, reserved_size);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int xsf_psf_start(void* state_ptr, int psf_version) {
    XsfPsfState* state = (XsfPsfState*)state_ptr;
    state->psf_version = psf_version;

    // BIOS and psx_init() are handled once in xsf_plugin_static_init().
    // Check the init flag - not just the BIOS pointer - because bios_set_image()
    // succeeds even when psx_init() subsequently fails. Calling psx_clear_state()
    // without a successful psx_init() crashes via psx_hang("library not initialized").
    if (!s_psx_lib_initialized) {
        rv_error("PSF: highly_experimental library not initialized - cannot play PSF files");
        return -1;
    }

    // Allocate PSX state
    state->psx_state = (uint8_t*)malloc(psx_get_state_size((uint8_t)psf_version));
    if (state->psx_state == nullptr) {
        return -1;
    }
    psx_clear_state(state->psx_state, (uint8_t)psf_version);

    // PSF2 needs a virtual filesystem
    if (psf_version == 2) {
        state->psf2fs = psf2fs_create();
        if (state->psf2fs == nullptr) {
            free(state->psx_state);
            state->psx_state = nullptr;
            return -1;
        }
    }

    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static int psf2_virtual_readfile(void* context, const char* path, int offset, char* buffer, int length) {
    return psf2fs_virtual_readfile(context, path, offset, buffer, length);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int xsf_psf_render(void* state_ptr, int16_t* buffer, int frames) {
    XsfPsfState* state = (XsfPsfState*)state_ptr;
    if (state == nullptr || state->psx_state == nullptr) {
        return 0;
    }

    // Set refresh rate if detected
    if (state->refresh > 0) {
        psx_set_refresh(state->psx_state, state->refresh);
    }

    // For PSF2, set up virtual file system reader
    if (state->psf_version == 2 && state->psf2fs != nullptr) {
        psx_set_readfile(state->psx_state, psf2_virtual_readfile, state->psf2fs);
    }

    unsigned int frames_u = (unsigned int)frames;
    psx_execute(state->psx_state, 0x7fffffff, buffer, &frames_u, 0);
    return (int)frames_u;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int xsf_psf_sample_rate(void* state_ptr) {
    XsfPsfState* state = (XsfPsfState*)state_ptr;
    if (state == nullptr) {
        return 44100;
    }
    // PSF1 = 44100, PSF2 = 48000
    return state->psf_version == 2 ? 48000 : 44100;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int xsf_psf_seek_reset(void* state_ptr) {
    XsfPsfState* state = (XsfPsfState*)state_ptr;
    if (state == nullptr || state->psx_state == nullptr) {
        return -1;
    }

    psx_clear_state(state->psx_state, (uint8_t)state->psf_version);
    state->first_load = 1;
    state->refresh = 0;

    if (state->psf_version == 2 && state->psf2fs != nullptr) {
        psf2fs_delete(state->psf2fs);
        state->psf2fs = psf2fs_create();
    }

    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void xsf_psf_destroy(void* state_ptr) {
    XsfPsfState* state = (XsfPsfState*)state_ptr;
    if (state == nullptr) {
        return;
    }

    if (state->psf2fs != nullptr) {
        psf2fs_delete(state->psf2fs);
    }

    free(state->psx_state);
    free(state);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void* xsf_psf_get_psf2fs(void* state_ptr) {
    XsfPsfState* state = (XsfPsfState*)state_ptr;
    if (state == nullptr) {
        return nullptr;
    }
    return state->psf2fs;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Scope capture helper: get SPU state from the PSX emulator state chain

static void* xsf_psf_get_spu_state(XsfPsfState* state) {
    if (state == nullptr || state->psx_state == nullptr) {
        return nullptr;
    }
    void* iop = psx_get_iop_state(state->psx_state);
    if (iop == nullptr) {
        return nullptr;
    }
    return iop_get_spu_state(iop);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void xsf_psf_enable_scope_capture(void* state_ptr, int enable) {
    XsfPsfState* state = (XsfPsfState*)state_ptr;
    void* spu = xsf_psf_get_spu_state(state);
    if (spu != nullptr) {
        spu_enable_scope_capture(spu, enable);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

uint32_t xsf_psf_get_scope_data(void* state_ptr, int channel, float* buffer, uint32_t num_samples) {
    XsfPsfState* state = (XsfPsfState*)state_ptr;
    void* spu = xsf_psf_get_spu_state(state);
    if (spu == nullptr) {
        return 0;
    }
    return spu_get_scope_data(spu, channel, buffer, num_samples);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int xsf_psf_get_scope_channel_count(void* state_ptr) {
    XsfPsfState* state = (XsfPsfState*)state_ptr;
    void* spu = xsf_psf_get_spu_state(state);
    if (spu == nullptr) {
        return 0;
    }
    return spu_get_scope_channel_count(spu);
}
