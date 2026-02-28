///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// xSF GSF Wrapper - Game Boy Advance emulation via viogsf
//
// GSF (0x22) exe data format:
//   Bytes 0-3:  Entry point (LE) - 0x02000000 = multiboot, 0x08000000 = ROM
//   Bytes 4-7:  ROM offset (LE, masked with 0x1FFFFFF)
//   Bytes 8-11: ROM data size (LE)
//   Bytes 12+:  ROM data
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "xsf_gsf.h"

#include <retrovert/log.h>

#include "GBA.h"
#include "Sound.h"
#include "xsf_common.h"

#include <new>
#include <stdlib.h>
#include <string.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static const int GSF_SAMPLE_RATE = 48000;
static const int GSF_TICKS_PER_RENDER = 250000;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Audio output callback - accumulates S16 stereo samples from CPULoop
//
// The GBA sound system flushes audio in chunks (~735 frames at 44100 Hz).
// When the output buffer fills mid-chunk, excess samples are saved in an
// overflow buffer and prepended to the next render call. Without this,
// the emulator advances past discarded samples, causing audio to play too fast.

static const int GSF_OVERFLOW_SIZE = 1024; // Enough for one sound flush chunk

struct GsfSoundOut : GBASoundOut {
    int16_t* buffer;
    int buffer_frames;  // max frames
    int frames_written; // frames written so far

    // Overflow buffer for excess samples that don't fit in the current render
    int16_t overflow[GSF_OVERFLOW_SIZE * 2]; // stereo S16
    int overflow_frames;

    GsfSoundOut() : buffer(nullptr), buffer_frames(0), frames_written(0), overflow_frames(0) {}

    void write(const void* samples, unsigned long bytes) override {
        int frames = (int)(bytes / 4); // 4 bytes per stereo frame (2 * S16)
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
            int capacity = GSF_OVERFLOW_SIZE - overflow_frames;
            if (excess > capacity) {
                excess = capacity;
            }
            memcpy(overflow + overflow_frames * 2, src + to_copy * 2, (size_t)excess * 4);
            overflow_frames += excess;
        }
    }
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct XsfGsfState {
    GBASystem system;
    GsfSoundOut sound_out;
    // Accumulated ROM data from _lib chain
    uint8_t* rom;
    size_t rom_size;
    uint32_t entry_point;
    int initialized;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

extern "C" void* xsf_gsf_create(void) {
    // Must use new (not calloc) because XsfGsfState contains C++ objects
    // (GBASystem with Blip_Synth members, GsfSoundOut with vtable) whose
    // constructors must run to initialize internal state correctly.
    return new (std::nothrow) XsfGsfState();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Load callback - accumulates ROM data from exe section

extern "C" int xsf_gsf_load(void* context, const uint8_t* exe, size_t exe_size, const uint8_t* reserved,
                            size_t reserved_size) {
    (void)reserved;
    (void)reserved_size;
    XsfGsfState* state = (XsfGsfState*)context;

    if (exe == nullptr || exe_size < 12) {
        return 0;
    }

    uint32_t entry = xsf_get_le32(exe);
    uint32_t offset = xsf_get_le32(exe + 4) & 0x1FFFFFF;
    uint32_t size = xsf_get_le32(exe + 8);
    const uint8_t* data = exe + 12;
    size_t data_len = exe_size - 12;

    if (size > data_len) {
        size = (uint32_t)data_len;
    }

    // Store entry point from first file in chain
    if (state->entry_point == 0) {
        state->entry_point = entry;
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
    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

extern "C" int xsf_gsf_start(void* state_ptr, int psf_version) {
    (void)psf_version;
    // Nothing to do here - GBA init happens in post_load after ROM data is accumulated
    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

extern "C" int xsf_gsf_post_load(void* state_ptr) {
    XsfGsfState* state = (XsfGsfState*)state_ptr;

    if (state->initialized) {
        CPUCleanUp(&state->system);
        state->initialized = 0;
    }

    if (state->rom == nullptr || state->rom_size == 0) {
        rv_error("GSF: no ROM data loaded");
        return -1;
    }

    // Reset overflow buffer for fresh playback
    state->sound_out.overflow_frames = 0;

    // Reconstruct GBASystem in place using placement new. memset would destroy
    // C++ object invariants (Blip_Synth::width must be 16, not 0).
    state->system.~GBASystem();
    new (&state->system) GBASystem();

    // Multiboot if entry point is in EWRAM (0x02xxxxxx)
    state->system.cpuIsMultiBoot = ((state->entry_point >> 24) == 0x02);
    state->system.soundSampleRate = GSF_SAMPLE_RATE;
    state->system.soundInterpolation = false;
    state->system.soundDeclicking = false;

    if (CPULoadRom(&state->system, state->rom, (u32)state->rom_size) == 0) {
        rv_error("GSF: CPULoadRom failed");
        return -1;
    }

    soundInit(&state->system, &state->sound_out);
    soundReset(&state->system);

    CPUInit(&state->system);
    CPUReset(&state->system);

    state->initialized = 1;
    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

extern "C" int xsf_gsf_render(void* state_ptr, int16_t* buffer, int frames) {
    XsfGsfState* state = (XsfGsfState*)state_ptr;
    if (!state->initialized) {
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

    // Keep running CPU until we have enough frames
    while (state->sound_out.frames_written < frames) {
        CPULoop(&state->system, GSF_TICKS_PER_RENDER);
    }

    return state->sound_out.frames_written;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

extern "C" int xsf_gsf_sample_rate(void* state) {
    (void)state;
    return GSF_SAMPLE_RATE;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

extern "C" int xsf_gsf_seek_reset(void* state_ptr) {
    XsfGsfState* state = (XsfGsfState*)state_ptr;

    if (state->initialized) {
        CPUCleanUp(&state->system);
        state->initialized = 0;
    }

    free(state->rom);
    state->rom = nullptr;
    state->rom_size = 0;
    state->entry_point = 0;

    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

extern "C" void xsf_gsf_destroy(void* state_ptr) {
    XsfGsfState* state = (XsfGsfState*)state_ptr;
    if (state == nullptr) {
        return;
    }

    if (state->initialized) {
        CPUCleanUp(&state->system);
    }

    free(state->rom);
    delete state;
}
