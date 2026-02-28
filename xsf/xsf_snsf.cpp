///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// xSF SNSF Wrapper - Super Nintendo emulation via snsf9x
//
// SNSF (0x23) exe data format:
//   Bytes 0-3: ROM offset (LE)
//   Bytes 4-7: ROM data size (LE)
//   Bytes 8+:  ROM data
//
// Reserved section can contain SRAM data (type 0 blocks).
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "xsf_snsf.h"

#include <retrovert/log.h>

#include "SNESSystem.h"
#include "xsf_common.h"

#include <stdlib.h>
#include <string.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static const int SNSF_SAMPLE_RATE = 48000;
static const size_t SNSF_SRAM_MAX = 0x20000; // 128KB

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Audio output callback - accumulates S16 stereo samples from CPULoop
//
// The SNES sound system flushes audio in chunks. When the output buffer fills
// mid-chunk, excess samples are saved in an overflow buffer and prepended to
// the next render call. Without this, the emulator advances past discarded
// samples, causing audio to play too fast.

static const int SNSF_OVERFLOW_SIZE = 1024;

struct SnsfSoundOut : SNESSoundOut {
    int16_t* buffer;
    int buffer_frames;
    int frames_written;

    // Overflow buffer for excess samples that don't fit in the current render
    int16_t overflow[SNSF_OVERFLOW_SIZE * 2]; // stereo S16
    int overflow_frames;

    SnsfSoundOut() : buffer(nullptr), buffer_frames(0), frames_written(0), overflow_frames(0) {}

    void write(const void* samples, unsigned long bytes) override {
        int frames = (int)(bytes / 4);
        const int16_t* src = (const int16_t*)samples;

        // Copy as many frames as fit into the output buffer
        int remaining = buffer_frames - frames_written;
        int to_copy = frames < remaining ? frames : remaining;

        if (to_copy > 0 && buffer != nullptr) {
            memcpy(buffer + frames_written * 2, src, (size_t)to_copy * 4);
            frames_written += to_copy;
        }

        // Save excess frames in overflow buffer for the next render call
        int excess = frames - to_copy;
        if (excess > 0) {
            int capacity = SNSF_OVERFLOW_SIZE - overflow_frames;
            if (excess > capacity) {
                excess = capacity;
            }
            memcpy(overflow + overflow_frames * 2, src + to_copy * 2, (size_t)excess * 4);
            overflow_frames += excess;
        }
    }
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct XsfSnsfState {
    SNESSystem* system;
    SnsfSoundOut sound_out;
    // Accumulated ROM/SRAM data from _lib chain
    uint8_t* rom;
    size_t rom_size;
    uint8_t* sram;
    size_t sram_size;
    int initialized;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

extern "C" void* xsf_snsf_create(void) {
    XsfSnsfState* state = (XsfSnsfState*)calloc(1, sizeof(XsfSnsfState));
    return state;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Load callback - accumulates ROM data from exe section, SRAM from reserved section

extern "C" int xsf_snsf_load(void* context, const uint8_t* exe, size_t exe_size, const uint8_t* reserved,
                             size_t reserved_size) {
    XsfSnsfState* state = (XsfSnsfState*)context;

    // Process exe section (ROM data)
    if (exe != nullptr && exe_size >= 8) {
        uint32_t offset = xsf_get_le32(exe);
        uint32_t size = xsf_get_le32(exe + 4);
        const uint8_t* data = exe + 8;
        size_t data_len = exe_size - 8;

        if (size > data_len) {
            size = (uint32_t)data_len;
        }

        size_t needed = (size_t)offset + size;
        if (state->rom == nullptr) {
            state->rom = (uint8_t*)calloc(1, needed);
            if (state->rom == nullptr) {
                return -1;
            }
            state->rom_size = needed;
        } else if (state->rom_size < needed) {
            uint8_t* new_buf = (uint8_t*)realloc(state->rom, needed);
            if (new_buf == nullptr) {
                return -1;
            }
            memset(new_buf + state->rom_size, 0, needed - state->rom_size);
            state->rom = new_buf;
            state->rom_size = needed;
        }

        memcpy(state->rom + offset, data, size);
    }

    // Process reserved section (SRAM blocks, type 0)
    if (reserved != nullptr && reserved_size >= 8) {
        size_t pos = 0;
        while (pos + 8 <= reserved_size) {
            uint32_t type = xsf_get_le32(reserved + pos);
            uint32_t block_size = xsf_get_le32(reserved + pos + 4);
            pos += 8;

            if (pos + block_size > reserved_size) {
                break;
            }

            if (type == 0 && block_size >= 4) {
                // SRAM block: 4 bytes offset + data
                uint32_t sram_offset = xsf_get_le32(reserved + pos);
                const uint8_t* sram_data = reserved + pos + 4;
                size_t sram_len = block_size - 4;

                size_t needed = (size_t)sram_offset + sram_len;
                if (needed > SNSF_SRAM_MAX) {
                    needed = SNSF_SRAM_MAX;
                    if (sram_offset >= SNSF_SRAM_MAX) {
                        pos += block_size;
                        continue;
                    }
                    sram_len = SNSF_SRAM_MAX - sram_offset;
                }

                if (state->sram == nullptr) {
                    state->sram = (uint8_t*)calloc(1, needed);
                    if (state->sram == nullptr) {
                        return -1;
                    }
                    state->sram_size = needed;
                } else if (state->sram_size < needed) {
                    uint8_t* new_buf = (uint8_t*)realloc(state->sram, needed);
                    if (new_buf == nullptr) {
                        return -1;
                    }
                    memset(new_buf + state->sram_size, 0, needed - state->sram_size);
                    state->sram = new_buf;
                    state->sram_size = needed;
                }

                memcpy(state->sram + sram_offset, sram_data, sram_len);
            }

            pos += block_size;
        }
    }

    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

extern "C" int xsf_snsf_start(void* state_ptr, int psf_version) {
    (void)psf_version;
    // Nothing to do here - SNES init happens in post_load
    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

extern "C" int xsf_snsf_post_load(void* state_ptr) {
    XsfSnsfState* state = (XsfSnsfState*)state_ptr;

    if (state->initialized) {
        if (state->system != nullptr) {
            state->system->Term();
            delete state->system;
            state->system = nullptr;
        }
        state->initialized = 0;
    }

    if (state->rom == nullptr || state->rom_size == 0) {
        rv_error("SNSF: no ROM data loaded");
        return -1;
    }

    state->system = new SNESSystem();
    if (state->system == nullptr) {
        return -1;
    }

    if (!state->system->Load((const uint8*)state->rom, (uint32)state->rom_size,
                             state->sram != nullptr ? (const uint8*)state->sram : nullptr, (uint32)state->sram_size)) {
        rv_error("SNSF: Load failed");
        delete state->system;
        state->system = nullptr;
        return -1;
    }

    // Reset overflow buffer for fresh playback
    state->sound_out.overflow_frames = 0;

    state->system->soundSampleRate = SNSF_SAMPLE_RATE;
    state->system->soundEnableFlag = 0xff;
    state->system->SoundInit(&state->sound_out);
    state->system->SoundReset();
    state->system->Init();
    state->system->Reset();

    state->initialized = 1;
    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

extern "C" int xsf_snsf_render(void* state_ptr, int16_t* buffer, int frames) {
    XsfSnsfState* state = (XsfSnsfState*)state_ptr;
    if (!state->initialized || state->system == nullptr) {
        return 0;
    }

    state->sound_out.buffer = buffer;
    state->sound_out.buffer_frames = frames;
    state->sound_out.frames_written = 0;

    // Drain overflow from previous render first
    if (state->sound_out.overflow_frames > 0) {
        int to_copy = state->sound_out.overflow_frames;
        if (to_copy > frames) {
            to_copy = frames;
        }
        memcpy(buffer, state->sound_out.overflow, (size_t)to_copy * 4);
        state->sound_out.frames_written = to_copy;

        // Shift remaining overflow forward
        int remaining = state->sound_out.overflow_frames - to_copy;
        if (remaining > 0) {
            memmove(state->sound_out.overflow, state->sound_out.overflow + to_copy * 2, (size_t)remaining * 4);
        }
        state->sound_out.overflow_frames = remaining;
    }

    // Each CPULoop() call produces one frame of audio
    while (state->sound_out.frames_written < frames) {
        state->system->CPULoop();
    }

    return state->sound_out.frames_written;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

extern "C" int xsf_snsf_sample_rate(void* state) {
    (void)state;
    return SNSF_SAMPLE_RATE;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

extern "C" int xsf_snsf_seek_reset(void* state_ptr) {
    XsfSnsfState* state = (XsfSnsfState*)state_ptr;

    if (state->initialized && state->system != nullptr) {
        state->system->Term();
        delete state->system;
        state->system = nullptr;
        state->initialized = 0;
    }

    free(state->rom);
    free(state->sram);
    state->rom = nullptr;
    state->sram = nullptr;
    state->rom_size = 0;
    state->sram_size = 0;

    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

extern "C" void xsf_snsf_destroy(void* state_ptr) {
    XsfSnsfState* state = (XsfSnsfState*)state_ptr;
    if (state == nullptr) {
        return;
    }

    if (state->initialized && state->system != nullptr) {
        state->system->Term();
        delete state->system;
    }

    free(state->rom);
    free(state->sram);
    free(state);
}
