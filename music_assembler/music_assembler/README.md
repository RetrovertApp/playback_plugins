# Music Assembler C Player

A C library for playing Music Assembler (MA) modules, an Amiga music format. The player renders 4-channel Amiga-style audio to stereo float PCM.

## Origin

This player is a C port of the Music Assembler player from the [NostalgicPlayer](https://github.com/neumatho/NostalgicPlayer) project by Thomas Neumann, which is written in C#. The port was done with the help of AI coding agents.

## API

```c
#include "music_assembler.h"

// Load a module from raw file data and prepare for playback at the given sample rate.
// Returns NULL on failure.
MaModule* ma_create(const uint8_t* data, size_t size, float sample_rate);

// Free all resources associated with a module.
void ma_destroy(MaModule* module);

// Query the number of subsongs in the module.
int ma_subsong_count(const MaModule* module);

// Select a subsong by index (0-based). Resets playback state.
bool ma_select_subsong(MaModule* module, int subsong);

// Render interleaved stereo float samples into the provided buffer.
// Returns the number of frames written.
size_t ma_render(MaModule* module, float* interleaved_stereo, size_t frames);

// Check whether the current subsong has finished playing.
bool ma_has_ended(const MaModule* module);
```

## Usage Example

```c
#include "music_assembler.h"
#include <stdio.h>
#include <stdlib.h>

int main(void) {
    // Load the .ma file into memory
    FILE* f = fopen("song.ma", "rb");
    fseek(f, 0, SEEK_END);
    size_t size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t* data = malloc(size);
    fread(data, 1, size, f);
    fclose(f);

    // Create the player at 44100 Hz
    MaModule* mod = ma_create(data, size, 44100.0f);
    free(data);
    if (!mod) {
        fprintf(stderr, "Failed to load module\n");
        return 1;
    }

    // Render audio in chunks
    float buffer[1024 * 2]; // stereo interleaved
    while (!ma_has_ended(mod)) {
        ma_render(mod, buffer, 1024);
        // ... feed buffer to audio output ...
    }

    ma_destroy(mod);
    return 0;
}
```

## License

MIT
