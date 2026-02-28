// Minimal SDL_endian shim for klystron
#ifndef SDL_ENDIAN_SHIM_H
#define SDL_ENDIAN_SHIM_H

#include <stdint.h>

// Assume little-endian (x86, ARM little-endian)
#define SDL_SwapLE16(x) (x)
#define SDL_SwapLE32(x) (x)

#define SDL_LIL_ENDIAN 1234
#define SDL_BIG_ENDIAN 4321
#define SDL_BYTEORDER SDL_LIL_ENDIAN

#endif
