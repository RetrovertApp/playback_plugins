// Comprehensive SDL shim for klystron (no actual SDL dependency)
#ifndef SDL_SHIM_H
#define SDL_SHIM_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// Basic SDL types
typedef uint8_t Uint8;
typedef int8_t Sint8;
typedef uint16_t Uint16;
typedef int16_t Sint16;
typedef uint32_t Uint32;
typedef int32_t Sint32;
typedef uint64_t Uint64;
typedef int64_t Sint64;

// SDL_mutex stub (klystron references it in cydtypes.h)
typedef struct SDL_mutex {
    int dummy;
} SDL_mutex;

static inline SDL_mutex* SDL_CreateMutex(void) {
    return NULL;
}
static inline void SDL_DestroyMutex(SDL_mutex* m) {
    (void)m;
}
static inline int SDL_LockMutex(SDL_mutex* m) {
    (void)m;
    return 0;
}
static inline int SDL_UnlockMutex(SDL_mutex* m) {
    (void)m;
    return 0;
}

// SDL_Delay and SDL_GetTicks stubs
static inline void SDL_Delay(Uint32 ms) {
    (void)ms;
}
static inline Uint32 SDL_GetTicks(void) {
    return 0;
}

// SDL audio types (used by cyd_register which we don't call, but code references them)
#define AUDIO_S16SYS 0x8010

typedef void (*SDL_AudioCallback)(void* userdata, Uint8* stream, int len);

typedef struct SDL_AudioSpec {
    int freq;
    Uint16 format;
    Uint8 channels;
    Uint8 silence;
    Uint16 samples;
    Uint32 size;
    SDL_AudioCallback callback;
    void* userdata;
} SDL_AudioSpec;

static inline int SDL_OpenAudio(SDL_AudioSpec* desired, SDL_AudioSpec* obtained) {
    (void)desired;
    (void)obtained;
    return -1; // Stub: always fail (we use unregistered player)
}
static inline void SDL_CloseAudio(void) {}
static inline void SDL_PauseAudio(int pause_on) {
    (void)pause_on;
}

// SDL surface stubs (macros.h references SDL_MUSTLOCK/SDL_LockSurface)
#define SDL_MUSTLOCK(s) (0)
#define SDL_LockSurface(s) (0)
#define SDL_UnlockSurface(s)

// Include our RWops and endian shims
#include "SDL_endian.h"
#include "SDL_rwops.h"

#endif
