///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Nuade Playback Plugin - visualization
//
// Maps nuade's display API (snapshots, tracker cells, scope, VU) onto the RVPlaybackPlugin viz callbacks.
//
// nuade does not lock: nuade_render writes the state these calls read. The host serializes them with read_data.
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// C11 nullptr compatibility
#ifndef nullptr
#define nullptr ((void*)0)
#endif

#include "nuade_instance.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// The host rejects a captured window wider than its row budget (64 rows by default) instead of truncating it, and a
// grid_ticks player can report a thousand rows, so the window follows the playhead.
#define NUPLUG_ROW_WINDOW 64

static bool viz_ready(const NuplugInstance* inst) {
    return inst != nullptr && inst->song != nullptr;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Where the display is: the newest snapshot at or before the frame rendered so far.
static bool current_position(NuplugInstance* inst, NuadePosition* out) {
    NuadeSnapshotResult snap = nuade_snapshot(inst->song, nuade_position(inst->song), nullptr, 0);
    if (!snap.valid) {
        return false;
    }
    *out = snap.snapshot.position;
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// The pattern layout (channels, columns) is the same for every pattern of a song, so read it once, from the current
// position or, before the first tick, from the start of the song.
static bool load_tracker(NuplugInstance* inst) {
    if (inst->tracker_cached) {
        return true;
    }

    const NuadePlayer* player = nuade_player(inst->song);
    if (player->tracker_info == nullptr && player->grid_ticks == nullptr) {
        return false;
    }

    NuadePosition at;
    if (!current_position(inst, &at)) {
        memset(&at, 0, sizeof(at));
        at.mode = NuadeScroll_Rows;
    }

    NuadeTrackerInfoResult res = nuade_tracker_info(inst->song, &at);
    if (!res.valid) {
        return false;
    }

    inst->tracker = res.info;
    inst->tracker_cached = true;
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// The voices nuade meters. A player that mixes its tracks in software (EMS v6) shows more display voices than the
// mixer has, but scope and VU only exist for the mixer's voices; the host needs one level per scope channel.
static uint32_t meter_count(NuplugInstance* inst) {
    const NuadeSongInfo* info = nuade_info(inst->song);
    return info->display_voices < info->voice_count ? info->display_voices : info->voice_count;
}

bool nuplug_viz_info(void* user_data, RVVizInfo* out) {
    NuplugInstance* inst = (NuplugInstance*)user_data;
    if (!viz_ready(inst) || out == nullptr) {
        return false;
    }

    out->caps = RVVizCaps_Scope | RVVizCaps_Vu;
    out->scroll_mode = RVScrollMode_Synchronized;
    out->scope_channel_count = meter_count(inst);
    out->pattern_channel_count = 0;
    out->column_count = 0;

    if (load_tracker(inst)) {
        out->caps |= RVVizCaps_PatternCells;
        out->pattern_channel_count = inst->tracker.channels;
        out->column_count = inst->tracker.column_count;
    }
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// The characters every cell of the column takes. A note is "C-5", or "C--1" for a format whose lowest notes sit below
// C-0, so its column is as wide as the widest spelling the format can produce.
static uint8_t column_text_width(const NuadeColumnDesc* col) {
    if (col->kind != NuadeColumn_Note) {
        return col->digits;
    }
    return col->octave_shift < 0 ? 4 : 3;
}

uint32_t nuplug_viz_columns(void* user_data, RVColumnDesc* out, uint32_t cap) {
    NuplugInstance* inst = (NuplugInstance*)user_data;
    if (!viz_ready(inst) || !load_tracker(inst)) {
        return 0;
    }

    uint32_t n = inst->tracker.column_count < cap ? inst->tracker.column_count : cap;
    for (uint32_t i = 0; i < n; i++) {
        const NuadeColumnDesc* col = &inst->tracker.columns[i];
        memset(out[i].label, 0, sizeof(out[i].label));
        strncpy((char*)out[i].label, col->name, sizeof(out[i].label) - 1);
        out[i].char_width = column_text_width(col);
        out[i].kind = col->kind == NuadeColumn_Note ? RVColumnKind_Note : RVColumnKind_Custom;
    }
    return n;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint32_t fill_channels(uint32_t count, RVChannelDesc* out, uint32_t cap) {
    if (count > cap) {
        count = cap;
    }
    for (uint32_t i = 0; i < count; i++) {
        memset(out[i].name, 0, sizeof(out[i].name));
        snprintf((char*)out[i].name, sizeof(out[i].name), "Ch %u", i + 1);
        out[i].scope_width = 1;
    }
    return count;
}

uint32_t nuplug_viz_pattern_channels(void* user_data, RVChannelDesc* out, uint32_t cap) {
    NuplugInstance* inst = (NuplugInstance*)user_data;
    if (!viz_ready(inst) || !load_tracker(inst)) {
        return 0;
    }
    return fill_channels(inst->tracker.channels, out, cap);
}

uint32_t nuplug_viz_scope_channels(void* user_data, RVChannelDesc* out, uint32_t cap) {
    NuplugInstance* inst = (NuplugInstance*)user_data;
    if (!viz_ready(inst)) {
        return 0;
    }
    return fill_channels(meter_count(inst), out, cap);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool nuplug_viz_position(void* user_data, RVTrackerPosition* out) {
    NuplugInstance* inst = (NuplugInstance*)user_data;
    NuadePosition at;
    if (!viz_ready(inst) || out == nullptr || !current_position(inst, &at)) {
        return false;
    }

    NuadeTrackerInfoResult res = nuade_tracker_info(inst->song, &at);

    out->order = at.order;
    out->pattern = at.pattern;
    out->row = at.row;

    uint32_t rows = res.valid ? res.info.rows : 0;
    uint32_t lo = 0;
    uint32_t hi = rows;
    if (rows > NUPLUG_ROW_WINDOW) {
        lo = at.row > NUPLUG_ROW_WINDOW / 2 ? at.row - NUPLUG_ROW_WINDOW / 2 : 0;
        if (lo + NUPLUG_ROW_WINDOW > rows) {
            lo = rows - NUPLUG_ROW_WINDOW;
        }
        hi = lo + NUPLUG_ROW_WINDOW;
    }
    out->window_lo = lo;
    out->window_hi = hi;
    return true;
}

uint32_t nuplug_viz_channel_rows(void* user_data, uint32_t* out, uint32_t cap) {
    (void)user_data;
    (void)out;
    (void)cap;
    return 0; // Synchronized: window comes from the position
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool reserve_cells(NuplugInstance* inst, uint32_t count) {
    if (count <= inst->cell_scratch_cap) {
        return true;
    }
    NuadePatternCell* grown = (NuadePatternCell*)realloc(inst->cell_scratch, count * sizeof(NuadePatternCell));
    if (grown == nullptr) {
        return false;
    }
    inst->cell_scratch = grown;
    inst->cell_scratch_cap = count;
    return true;
}

// The host shows a cell as the text it is given and sizes its columns from that text, so every cell takes the width the
// column declares and an empty one is dots. `raw` only drives its coloring, with 0 for an empty cell.
static void render_cell(const NuadeColumnDesc* col, int32_t value, RVPatternCell* cell) {
    memset(cell->text, 0, sizeof(cell->text));
    cell->raw = value >= 0 ? (uint32_t)value + 1 : 0;

    char* txt = (char*)cell->text;
    const uint8_t width = column_text_width(col);
    if (value == NUADE_CELL_EMPTY) {
        memset(txt, '.', width < sizeof(cell->text) ? width : sizeof(cell->text) - 1);
        return;
    }

    switch (col->kind) {
        case NuadeColumn_Note: {
            char name[8];
            nuade_note_name(name, value, col->octave_shift);
            snprintf(txt, sizeof(cell->text), "%-*s", width, name);
            break;
        }
        case NuadeColumn_Dec:
            snprintf(txt, sizeof(cell->text), "%0*d", col->digits, value);
            break;
        case NuadeColumn_Hex:
            snprintf(txt, sizeof(cell->text), "%0*X", col->digits, (unsigned)value);
            break;
    }
}

uint32_t nuplug_viz_cells(void* user_data, int32_t channel, uint32_t row_lo, uint32_t row_hi, RVPatternCell* out,
                         uint32_t cap) {
    NuplugInstance* inst = (NuplugInstance*)user_data;
    NuadePosition at;
    if (!viz_ready(inst) || out == nullptr || row_hi <= row_lo || !load_tracker(inst)
        || !current_position(inst, &at)) {
        return 0;
    }

    const NuadeTrackerInfo* info = &inst->tracker;
    uint32_t rows = row_hi - row_lo;
    if (!reserve_cells(inst, rows)) {
        return 0;
    }

    uint32_t ch_start = channel < 0 ? 0 : (uint32_t)channel;
    uint32_t ch_end = channel < 0 ? info->channels : ch_start + 1;
    if (ch_start >= info->channels) {
        return 0;
    }

    // Cells come out row by row, each row channel by channel; nuade hands them over one channel at a time.
    uint32_t written = 0;
    for (uint32_t ch = ch_start; ch < ch_end; ch++) {
        uint32_t got = nuade_tracker_cells(inst->song, &at, ch, (int32_t)row_lo, (int32_t)row_hi, inst->cell_scratch,
                                           inst->cell_scratch_cap);
        for (uint32_t r = 0; r < got; r++) {
            for (uint32_t c = 0; c < info->column_count; c++) {
                uint32_t slot = (r * (ch_end - ch_start) + (ch - ch_start)) * info->column_count + c;
                if (slot >= cap) {
                    return written;
                }
                render_cell(&info->columns[c], inst->cell_scratch[r].value[c], &out[slot]);
                if (slot + 1 > written) {
                    written = slot + 1;
                }
            }
        }
    }
    return written;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// nuade starts keeping scope and meter history on the first call that asks for it.
void nuplug_viz_scope_enable(void* user_data, bool on) {
    NuplugInstance* inst = (NuplugInstance*)user_data;
    if (!on || !viz_ready(inst)) {
        return;
    }
    float unused;
    nuade_scope(inst->song, 0, &unused, 1);
}

uint32_t nuplug_viz_scope_samples(void* user_data, int32_t channel, float* out, uint32_t cap) {
    NuplugInstance* inst = (NuplugInstance*)user_data;
    if (!viz_ready(inst) || out == nullptr || channel < 0) {
        return 0;
    }
    return nuade_scope(inst->song, (uint32_t)channel, out, cap);
}

uint32_t nuplug_viz_vu_levels(void* user_data, float* out, uint32_t cap) {
    NuplugInstance* inst = (NuplugInstance*)user_data;
    if (!viz_ready(inst) || out == nullptr) {
        return 0;
    }
    return nuade_vu(inst->song, out, cap);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void nuplug_viz_reset(NuplugInstance* inst) {
    inst->tracker_cached = false;
    memset(&inst->tracker, 0, sizeof(inst->tracker));
    free(inst->cell_scratch);
    inst->cell_scratch = nullptr;
    inst->cell_scratch_cap = 0;
}
