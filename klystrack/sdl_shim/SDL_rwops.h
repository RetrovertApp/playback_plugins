// SDL_rwops shim for klystron
// klystron defines its own RWops struct (when USESDL_RWOPS is not defined)
// but the VER_READ macro in macros.h uses SDL_RWread directly.
// We provide macros that dispatch to the struct's function pointers.
#ifndef SDL_RWOPS_SHIM_H
#define SDL_RWOPS_SHIM_H

// These macros call through the RWops struct's function pointers
// The ctx parameter is a pointer to klystron's own RWops struct
#ifndef SDL_RWread
#define SDL_RWread(ctx, ptr, size, num) ((ctx)->read((ctx), (ptr), (size), (num)))
#endif
#ifndef SDL_RWclose
#define SDL_RWclose(ctx) ((ctx)->close((ctx)))
#endif
#ifndef SDL_RWtell
#define SDL_RWtell(ctx) (0)
#endif
#ifndef SDL_RWseek
#define SDL_RWseek(ctx, offset, whence) (0)
#endif

#endif
