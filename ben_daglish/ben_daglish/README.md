# Ben Daglish C Player

A C library for playing Ben Daglish (.bd) modules, an Amiga music format. The player renders 4-channel Amiga-style audio to stereo float PCM.

## Origin

This player is a C port of the Ben Daglish player from the [NostalgicPlayer](https://github.com/neumatho/NostalgicPlayer) project by Thomas Neumann, which is written in C#. The port was done with the help of AI coding agents.

## API

```c
#include "ben_daglish.h"

// Load a module from raw file data and prepare for playback at the given sample rate.
// Returns NULL on failure.
BdModule* bd_create(const uint8_t* data, size_t size, float sample_rate);

// Free all resources associated with a module.
void bd_destroy(BdModule* module);

// Query the number of subsongs in the module.
int bd_subsong_count(const BdModule* module);

// Select a subsong by index (0-based). Resets playback state.
bool bd_select_subsong(BdModule* module, int subsong);

// Query the number of channels (always 4).
int bd_channel_count(const BdModule* module);

// Set a bitmask of which channels to include in the mix (bit 0 = ch0, etc.).
void bd_set_channel_mask(BdModule* module, uint32_t mask);

// Render interleaved stereo float samples into the provided buffer.
// Returns the number of frames written.
size_t bd_render(BdModule* module, float* interleaved_stereo, size_t frames);

// Check whether the current subsong has finished playing.
bool bd_has_ended(const BdModule* module);
```

## Usage Example

```c
#include "ben_daglish.h"
#include <stdio.h>
#include <stdlib.h>

int main(void) {
    // Load the .bd file into memory
    FILE* f = fopen("song.bd", "rb");
    fseek(f, 0, SEEK_END);
    size_t size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t* data = malloc(size);
    fread(data, 1, size, f);
    fclose(f);

    // Create the player at 44100 Hz
    BdModule* mod = bd_create(data, size, 44100.0f);
    free(data);
    if (!mod) {
        fprintf(stderr, "Failed to load module\n");
        return 1;
    }

    // Render audio in chunks
    float buffer[1024 * 2]; // stereo interleaved
    while (!bd_has_ended(mod)) {
        bd_render(mod, buffer, 1024);
        // ... feed buffer to audio output ...
    }

    bd_destroy(mod);
    return 0;
}
```

## License

MIT
