///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Per-instance state shared by the playback half (nuade_plugin.c) and the visualization half
// (nuade_viz_adapter.c). The host's user_data handle is a NuplugInstance*.
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <nuade/nuade.h>
#include <retrovert/playback.h>

#include <stdbool.h>
#include <stdint.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define NUADE_PLUGIN_FREQ 48000

typedef struct NuplugInstance {
    NuadeSong* song;
    bool ended; // the song reported its end; the next read finishes the stream

    // Visualization cache, filled by nuplug_viz_info and dropped by nuplug_viz_reset.
    bool tracker_cached;
    NuadeTrackerInfo tracker;
    NuadePatternCell* cell_scratch; // one channel's cells for the rows being asked for
    uint32_t cell_scratch_cap;
} NuplugInstance;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Visualization callbacks of RVPlaybackPlugin. user_data is a NuplugInstance*.

bool nuplug_viz_info(void* user_data, RVVizInfo* out);
uint32_t nuplug_viz_columns(void* user_data, RVColumnDesc* out, uint32_t cap);
uint32_t nuplug_viz_pattern_channels(void* user_data, RVChannelDesc* out, uint32_t cap);
uint32_t nuplug_viz_scope_channels(void* user_data, RVChannelDesc* out, uint32_t cap);
bool nuplug_viz_position(void* user_data, RVTrackerPosition* out);
uint32_t nuplug_viz_channel_rows(void* user_data, uint32_t* out, uint32_t cap);
uint32_t nuplug_viz_cells(void* user_data, int32_t channel, uint32_t row_lo, uint32_t row_hi, RVPatternCell* out,
                          uint32_t cap);
void nuplug_viz_scope_enable(void* user_data, bool on);
uint32_t nuplug_viz_scope_samples(void* user_data, int32_t channel, float* out, uint32_t cap);
uint32_t nuplug_viz_vu_levels(void* user_data, float* out, uint32_t cap);

// Drop what the visualization cached for the current song. Call when the song closes.
void nuplug_viz_reset(NuplugInstance* inst);
