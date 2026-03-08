# SidMon 2.0 C Player

A C library for playing SidMon 2.0 (.sid2) modules, an Amiga music format created by Brian Postma. The player renders 4-channel Amiga-style audio to stereo float PCM.

SidMon 2.0 is notable for its instrument-based approach with envelope tables (controlling volume, arpeggio, and vibrato per tick) and a sample negation feature that modifies waveform data in real-time during playback.

## Origin

This player is a C port of the SidMon 2.0 player from the [NostalgicPlayer](https://github.com/neumatho/NostalgicPlayer) project by Thomas Neumann, which is written in C#. The port was done with the help of AI coding agents.

Validated against 106 modules from the [Modland](https://ftp.modland.com/pub/modules/SidMon%202/) archive: 104 pass with RMS error below 0.05, 1 file is rejected by the C# reference (corrupt module), and 1 has slightly higher error due to cumulative sample negation drift over a 150-second song.

## API

```c
#include "sidmon2.h"

// Load a module from raw file data and prepare for playback at the given sample rate.
// Returns NULL on failure (not a valid SidMon 2.0 module).
Sd2Module* sd2_create(const uint8_t* data, size_t size, float sample_rate);

// Free all resources associated with a module.
void sd2_destroy(Sd2Module* module);

// Query the number of subsongs in the module (always 1 for SidMon 2.0).
int sd2_subsong_count(const Sd2Module* module);

// Select a subsong by index (0-based). Resets playback state.
bool sd2_select_subsong(Sd2Module* module, int subsong);

// Query the number of channels (always 4).
int sd2_channel_count(const Sd2Module* module);

// Set a bitmask of which channels to include in the mix (bit 0 = ch0, etc.).
void sd2_set_channel_mask(Sd2Module* module, uint32_t mask);

// Render interleaved stereo float samples into the provided buffer.
// Returns the number of frames written.
size_t sd2_render(Sd2Module* module, float* interleaved_stereo, size_t frames);

// Check whether the module has reached its natural end point at least once.
// Playback continues looping after this returns true.
bool sd2_has_ended(const Sd2Module* module);
```

## Usage Example

```c
#include "sidmon2.h"
#include <stdio.h>
#include <stdlib.h>

int main(void) {
    // Load the .sid2 file into memory
    FILE* f = fopen("song.sid2", "rb");
    fseek(f, 0, SEEK_END);
    size_t size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t* data = malloc(size);
    fread(data, 1, size, f);
    fclose(f);

    // Create the player at 44100 Hz
    Sd2Module* mod = sd2_create(data, size, 44100.0f);
    free(data);
    if (!mod) {
        fprintf(stderr, "Failed to load module\n");
        return 1;
    }

    // Render audio in chunks
    float buffer[1024 * 2]; // stereo interleaved
    while (!sd2_has_ended(mod)) {
        sd2_render(mod, buffer, 1024);
        // ... feed buffer to audio output ...
    }

    sd2_destroy(mod);
    return 0;
}
```

## Format Details

- 4 Amiga hardware channels, hard-panned (channels 0,3 left; channels 1,2 right)
- Instrument-based: each instrument references a sample and an envelope table
- Envelope tables control volume, arpeggio, and vibrato per tick
- Sample negation: a unique feature that flips sample bytes in real-time, creating evolving timbres
- Effects: portamento (slide up/down/to note), vibrato, arpeggio (via envelopes)
- Songs are structured as a position list referencing track patterns

## Example Songs

Example songs can be found at https://ftp.modland.com/pub/modules/SidMon%202/

## License

MIT
