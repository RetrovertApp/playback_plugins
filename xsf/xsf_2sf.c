///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// xSF 2SF Wrapper - Nintendo DS emulation via vio2sf
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// C11 nullptr compatibility
#ifndef nullptr
#define nullptr ((void*)0)
#endif

#include "xsf_2sf.h"

#include "xsf_common.h"

#include <retrovert/log.h>

#include "state.h"

#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#ifdef _WIN32
#define strcasecmp _stricmp
#else
#include <strings.h>
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct Xsf2sfState {
    NDS_state nds;
    // Loader buffers (accumulated from _lib chain)
    uint8_t* rom;
    uint8_t* save;
    size_t rom_size;
    size_t save_size;
    // Configuration from PSF tags
    int initial_frames;
    int sync_type;
    int arm9_clockdown_level;
    int arm7_clockdown_level;
    int initialized;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint32_t next_power_of_2(uint32_t v) {
    if (v == 0) {
        return 1;
    }
    v--;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    v++;
    return v;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Map ROM or save state data at the given offset into the accumulation buffer

static int twosf_load_map(Xsf2sfState* state, int issave, const uint8_t* data, unsigned data_len) {
    if (data_len < 8) {
        return -1;
    }

    uint32_t offset = xsf_get_le32(data);
    uint32_t size = xsf_get_le32(data + 4);
    data += 8;
    data_len -= 8;

    uint8_t** buf;
    size_t* buf_size;

    if (issave) {
        buf = &state->save;
        buf_size = &state->save_size;
    } else {
        buf = &state->rom;
        buf_size = &state->rom_size;
        // ROM size must be power of 2
        if (size < data_len) {
            size = data_len;
        }
        size = next_power_of_2(size);
    }

    if (*buf_size == 0) {
        size_t alloc_size = issave ? (size_t)(offset + data_len) : (size_t)size;
        *buf = (uint8_t*)calloc(1, alloc_size);
        if (*buf == nullptr) {
            return -1;
        }
        *buf_size = alloc_size;
    } else if (*buf_size < (size_t)(offset + data_len)) {
        size_t new_size = (size_t)(offset + data_len);
        uint8_t* new_buf = (uint8_t*)realloc(*buf, new_size);
        if (new_buf == nullptr) {
            return -1;
        }
        // Zero newly allocated region
        memset(new_buf + *buf_size, 0, new_size - *buf_size);
        *buf = new_buf;
        *buf_size = new_size;
    }

    memcpy(*buf + offset, data, data_len);
    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Decompress zlib data and map as ROM or save state

static int twosf_load_mapz(Xsf2sfState* state, int issave, const uint8_t* zdata, unsigned zsize) {
    z_stream z;
    memset(&z, 0, sizeof(z));
    if (inflateInit(&z) != Z_OK) {
        return -1;
    }

    z.next_in = (Bytef*)zdata;
    z.avail_in = zsize;

    unsigned buf_size = zsize * 4;
    if (buf_size < 256) {
        buf_size = 256;
    }
    uint8_t* buf = (uint8_t*)malloc(buf_size);
    if (buf == nullptr) {
        inflateEnd(&z);
        return -1;
    }

    z.next_out = buf;
    z.avail_out = buf_size;

    for (;;) {
        int ret = inflate(&z, Z_NO_FLUSH);
        if (ret == Z_STREAM_END) {
            break;
        }
        if (ret != Z_OK) {
            free(buf);
            inflateEnd(&z);
            return -1;
        }
        if (z.avail_out == 0) {
            unsigned new_size = buf_size * 2;
            uint8_t* new_buf = (uint8_t*)realloc(buf, new_size);
            if (new_buf == nullptr) {
                free(buf);
                inflateEnd(&z);
                return -1;
            }
            z.next_out = new_buf + buf_size;
            z.avail_out = buf_size;
            buf = new_buf;
            buf_size = new_size;
        }
    }

    unsigned total = (unsigned)z.total_out;
    inflateEnd(&z);

    int result = twosf_load_map(state, issave, buf, total);
    free(buf);
    return result;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void* xsf_2sf_create(void) {
    Xsf2sfState* state = (Xsf2sfState*)calloc(1, sizeof(Xsf2sfState));
    if (state == nullptr) {
        return nullptr;
    }
    state->initial_frames = -1;
    return state;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int xsf_2sf_load(void* context, const uint8_t* exe, size_t exe_size, const uint8_t* reserved, size_t reserved_size) {
    Xsf2sfState* state = (Xsf2sfState*)context;

    // Process exe section (ROM data)
    if (exe != nullptr && exe_size >= 8) {
        if (twosf_load_map(state, 0, exe, (unsigned)exe_size) != 0) {
            return -1;
        }
    }

    // Process reserved section (SAVE state chunks)
    if (reserved != nullptr && reserved_size >= 16) {
        size_t pos = 0;
        while (pos + 12 <= reserved_size) {
            uint32_t id = xsf_get_le32(reserved + pos);
            uint32_t save_size = xsf_get_le32(reserved + pos + 4);
            // uint32_t save_crc = xsf_get_le32(reserved + pos + 8); // CRC not validated
            if (pos + 12 + save_size > reserved_size) {
                break;
            }
            if (id == 0x45564153) { // "SAVE" in little-endian
                if (twosf_load_mapz(state, 1, reserved + pos + 12, save_size) != 0) {
                    return -1;
                }
            }
            pos += 12 + save_size;
        }
    }

    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int xsf_2sf_start(void* state_ptr, int psf_version) {
    (void)psf_version;
    // Nothing to do here - NDS init happens in post_load after ROM data is accumulated
    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int xsf_2sf_post_load(void* state_ptr) {
    Xsf2sfState* state = (Xsf2sfState*)state_ptr;

    if (state->initialized) {
        state_deinit(&state->nds);
        state->initialized = 0;
    }

    if (state_init(&state->nds) != 0) {
        rv_error("2SF: state_init failed");
        return -1;
    }
    state->initialized = 1;

    // Configure NDS emulator from PSF tags
    state->nds.dwInterpolation = 0;
    state->nds.dwChannelMute = 0;
    state->nds.initial_frames = state->initial_frames;
    state->nds.sync_type = state->sync_type;
    state->nds.arm9_clockdown_level = state->arm9_clockdown_level;
    state->nds.arm7_clockdown_level = state->arm7_clockdown_level;

    // Load ROM
    if (state->rom != nullptr) {
        state_setrom(&state->nds, state->rom, (u32)state->rom_size, 1);
    }

    // Load save state
    if (state->save != nullptr) {
        state_loadstate(&state->nds, state->save, (u32)state->save_size);
    }

    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int xsf_2sf_render(void* state_ptr, int16_t* buffer, int frames) {
    Xsf2sfState* state = (Xsf2sfState*)state_ptr;
    if (!state->initialized) {
        return 0;
    }
    state_render(&state->nds, buffer, (unsigned)frames);
    return frames;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int xsf_2sf_sample_rate(void* state) {
    (void)state;
    return 44100;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int xsf_2sf_seek_reset(void* state_ptr) {
    Xsf2sfState* state = (Xsf2sfState*)state_ptr;

    if (state->initialized) {
        state_deinit(&state->nds);
        state->initialized = 0;
    }

    // Free accumulated data so psf_load can re-accumulate
    free(state->rom);
    free(state->save);
    state->rom = nullptr;
    state->save = nullptr;
    state->rom_size = 0;
    state->save_size = 0;

    // Reset config to defaults
    state->initial_frames = -1;
    state->sync_type = 0;
    state->arm9_clockdown_level = 0;
    state->arm7_clockdown_level = 0;

    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void xsf_2sf_destroy(void* state_ptr) {
    Xsf2sfState* state = (Xsf2sfState*)state_ptr;
    if (state == nullptr) {
        return;
    }

    if (state->initialized) {
        state_deinit(&state->nds);
    }

    free(state->rom);
    free(state->save);
    free(state);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int xsf_2sf_info(void* state_ptr, const char* name, const char* value) {
    Xsf2sfState* state = (Xsf2sfState*)state_ptr;

    if (strcasecmp(name, "initial_frames") == 0) {
        state->initial_frames = atoi(value);
    } else if (strcasecmp(name, "sync_type") == 0) {
        state->sync_type = atoi(value);
    } else if (strcasecmp(name, "clockdown") == 0) {
        int v = atoi(value);
        state->arm9_clockdown_level = v;
        state->arm7_clockdown_level = v;
    } else if (strcasecmp(name, "arm9_clockdown_level") == 0) {
        state->arm9_clockdown_level = atoi(value);
    } else if (strcasecmp(name, "arm7_clockdown_level") == 0) {
        state->arm7_clockdown_level = atoi(value);
    }

    return 0;
}
