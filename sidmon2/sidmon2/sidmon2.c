// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Daniel Collin
// Ported from NostalgicPlayer C# implementation by Thomas Neumann (MIT)
#include "sidmon2.h"

#include <stdlib.h>
#include <string.h>

#ifndef __cplusplus
#ifndef nullptr
#define nullptr ((void*)0)
#endif
#endif

#define SAMPLE_FRAC_BITS 11

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Tables
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static const uint16_t s_periods[] = {
       0,
                      5760, 5424, 5120, 4832, 4560, 4304, 4064, 3840, 3616,
    3424, 3232, 3048, 2880, 2712, 2560, 2416, 2280, 2152, 2032, 1920, 1808,
    1712, 1616, 1524, 1440, 1356, 1280, 1208, 1140, 1076, 1016,  960,  904,
     856,  808,  762,  720,  678,  640,  604,  570,  538,  508,  480,  453,
     428,  404,  381,  360,  339,  320,  302,  285,  269,  254,  240,  226,
     214,  202,  190,  180,  170,  160,  151,  143,  135,  127,  120,  113,
     107,  101,   95
};

#define PERIODS_COUNT (sizeof(s_periods) / sizeof(s_periods[0]))

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Types
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef enum EnvelopeState {
    EnvelopeState_Done = 0,
    EnvelopeState_Attack,
    EnvelopeState_Decay,
    EnvelopeState_Sustain,
    EnvelopeState_Release
} EnvelopeState;

typedef struct Instrument {
    uint8_t waveform_list_number;
    uint8_t waveform_list_length;
    uint8_t waveform_list_speed;
    uint8_t waveform_list_delay;
    uint8_t arpeggio_number;
    uint8_t arpeggio_length;
    uint8_t arpeggio_speed;
    uint8_t arpeggio_delay;
    uint8_t vibrato_number;
    uint8_t vibrato_length;
    uint8_t vibrato_speed;
    uint8_t vibrato_delay;
    int8_t pitch_bend_speed;
    uint8_t pitch_bend_delay;
    uint8_t attack_max;
    uint8_t attack_speed;
    uint8_t decay_min;
    uint8_t decay_speed;
    uint8_t sustain_time;
    uint8_t release_min;
    uint8_t release_speed;
} Instrument;

typedef struct Sample {
    uint32_t length;
    uint32_t loop_start;
    uint32_t loop_length;
    int8_t* sample_data;
} Sample;

typedef struct SampleNegateInfo {
    uint32_t start_offset;
    uint32_t end_offset;
    uint16_t loop_index;
    uint16_t status;
    int16_t speed;
    int32_t position;
    uint16_t index;
    int16_t do_negation;
} SampleNegateInfo;

typedef struct Sequence {
    uint8_t track_number;
    int8_t note_transpose;
    int8_t instrument_transpose;
} Sequence;

typedef struct Track {
    uint8_t* track_data;
    uint32_t length;
} Track;

typedef struct VoiceInfo {
    Sequence* sequence_list;
    int8_t* sample_data;
    uint32_t sample_length;
    uint16_t sample_period;
    int16_t sample_volume;
    EnvelopeState envelope_state;
    uint16_t sustain_counter;
    Instrument* instrument;
    int8_t* loop_sample;
    uint32_t loop_offset;
    uint32_t loop_length;
    uint16_t original_note;

    uint16_t wave_list_delay;
    int16_t wave_list_offset;
    uint16_t arpeggio_delay;
    int16_t arpeggio_offset;
    uint16_t vibrato_delay;
    int16_t vibrato_offset;

    uint16_t current_note;
    uint16_t current_instrument;
    uint16_t current_effect;
    uint16_t current_effect_arg;

    uint16_t pitch_bend_counter;
    int16_t instrument_transpose;
    int16_t pitch_bend_value;
    uint16_t note_slide_note;
    int16_t note_slide_speed;
    int32_t track_position;
    uint8_t* current_track;
    uint32_t current_track_length;
    uint16_t empty_notes_counter;
    int16_t note_transpose;
    uint16_t current_sample;
} VoiceInfo;

typedef struct VoiceRender {
    const int8_t* sample_data;
    uint32_t sample_length;
    const int8_t* loop_data;
    uint32_t loop_offset;
    uint32_t loop_length;
    uint32_t sample_pos;    // fixed-point position (SAMPLE_FRAC_BITS)
    uint32_t sample_inc;    // fixed-point increment
    uint16_t volume;
    uint16_t period;
    bool active;
    bool has_pending_sample;
    const int8_t* pending_sample_data;
    uint32_t pending_sample_length;
    bool has_pending_loop;
    const int8_t* pending_loop_data;
    uint32_t pending_loop_offset;
    uint32_t pending_loop_length;
} VoiceRender;

typedef struct Sd2Module {
    // Module data
    uint8_t** waveform_info;    // [list_count][16]
    int32_t waveform_count;
    int8_t** arpeggios;         // [arp_count][16]
    int32_t arpeggio_count;
    int8_t** vibratoes;         // [vib_count][16]
    int32_t vibrato_count;
    Sequence* sequences;        // [4 * num_positions]
    int32_t num_positions;      // number of positions per channel
    Track* tracks;
    int32_t track_count;
    Instrument* instruments;
    int32_t instrument_count;
    Sample* samples;
    int32_t sample_count;
    SampleNegateInfo* sample_negate_info;

    // Playback state
    int8_t current_position;
    uint8_t current_row;
    uint8_t pattern_length;
    uint8_t current_rast;
    uint8_t current_rast2;
    uint8_t speed;
    uint8_t start_speed;
    uint8_t number_of_positions;  // max position index (numberOfPositions in C#, after decrement)

    VoiceInfo voices[4];
    VoiceRender voice_render[4];

    float sample_rate;
    float frames_per_tick;
    float frames_until_tick;
    uint32_t channel_mask;
    bool ended;
} Sd2Module;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Byte-reading helpers
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint8_t read_u8(const uint8_t* data, size_t* pos) {
    return data[(*pos)++];
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static int8_t read_i8(const uint8_t* data, size_t* pos) {
    return (int8_t)data[(*pos)++];
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint16_t read_be16(const uint8_t* data, size_t* pos) {
    uint16_t val = (uint16_t)((data[*pos] << 8) | data[*pos + 1]);
    *pos += 2;
    return val;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static int16_t read_be16_signed(const uint8_t* data, size_t* pos) {
    return (int16_t)read_be16(data, pos);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint32_t read_be32(const uint8_t* data, size_t* pos) {
    uint32_t val = ((uint32_t)data[*pos] << 24) | ((uint32_t)data[*pos + 1] << 16)
                   | ((uint32_t)data[*pos + 2] << 8) | (uint32_t)data[*pos + 3];
    *pos += 4;
    return val;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static int32_t read_be32_signed(const uint8_t* data, size_t* pos) {
    return (int32_t)read_be32(data, pos);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Module parsing
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool sd2_identify(const uint8_t* data, size_t size) {
    if (size < 86) {
        return false;
    }
    // Check "SIDMON II - THE MIDI VERSION" at offset 58
    return memcmp(data + 58, "SIDMON II - THE MIDI VERSION", 28) == 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool load_single_list_u8(const uint8_t* data, size_t size, uint32_t offset, uint32_t length,
                                uint8_t*** out_list, int32_t* out_count) {
    if ((length % 16) != 0 || offset + length > size) {
        return false;
    }

    uint32_t count = length / 16;
    uint8_t** list = (uint8_t**)calloc(count, sizeof(uint8_t*));
    uint8_t* block = (uint8_t*)malloc(length);
    if (list == nullptr || block == nullptr) {
        free(list);
        free(block);
        return false;
    }

    memcpy(block, data + offset, length);
    for (uint32_t i = 0; i < count; i++) {
        list[i] = block + i * 16;
    }

    *out_list = list;
    *out_count = (int32_t)count;
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool load_single_list_i8(const uint8_t* data, size_t size, uint32_t offset, uint32_t length,
                                int8_t*** out_list, int32_t* out_count) {
    if ((length % 16) != 0 || offset + length > size) {
        return false;
    }

    uint32_t count = length / 16;
    int8_t** list = (int8_t**)calloc(count, sizeof(int8_t*));
    int8_t* block = (int8_t*)malloc(length);
    if (list == nullptr || block == nullptr) {
        free(list);
        free(block);
        return false;
    }

    memcpy(block, data + offset, length);
    for (uint32_t i = 0; i < count; i++) {
        list[i] = block + i * 16;
    }

    *out_list = list;
    *out_count = (int32_t)count;
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool load_sequences(const uint8_t* data, size_t size, uint32_t pos_offset, uint32_t pos_length,
                           uint32_t note_offset, uint32_t note_length, uint32_t inst_offset, uint32_t inst_length,
                           uint8_t num_positions, Sequence** out_sequences) {
    if (pos_length != note_length || note_length != inst_length) {
        return false;
    }
    if (pos_length != (uint32_t)(num_positions * 4)) {
        return false;
    }
    if (pos_offset + pos_length > size || note_offset + note_length > size || inst_offset + inst_length > size) {
        return false;
    }

    // 4 channels * num_positions
    Sequence* seqs = (Sequence*)calloc(4 * num_positions, sizeof(Sequence));
    if (seqs == nullptr) {
        return false;
    }

    for (int ch = 0; ch < 4; ch++) {
        for (int j = 0; j < num_positions; j++) {
            int idx = ch * num_positions + j;
            int src_idx = ch * num_positions + j;
            seqs[idx].track_number = data[pos_offset + src_idx];
            seqs[idx].note_transpose = (int8_t)data[note_offset + src_idx];
            seqs[idx].instrument_transpose = (int8_t)data[inst_offset + src_idx];
        }
    }

    *out_sequences = seqs;
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool load_tracks(const uint8_t* data, size_t size, uint32_t table_offset, uint32_t table_length,
                        uint32_t tracks_offset, uint32_t tracks_length, Track** out_tracks, int32_t* out_count,
                        uint32_t* out_sample_offset) {
    uint32_t num_tracks = table_length / 2;
    if (table_offset + table_length > size || tracks_offset + tracks_length > size) {
        return false;
    }

    // Read track offset table
    uint16_t* track_offsets = (uint16_t*)calloc(num_tracks, sizeof(uint16_t));
    if (track_offsets == nullptr) {
        return false;
    }

    size_t tpos = table_offset;
    for (uint32_t i = 0; i < num_tracks; i++) {
        track_offsets[i] = read_be16(data, &tpos);
    }

    Track* trks = (Track*)calloc(num_tracks, sizeof(Track));
    if (trks == nullptr) {
        free(track_offsets);
        return false;
    }

    for (uint32_t i = 0; i < num_tracks; i++) {
        uint32_t track_len = (i == num_tracks - 1 ? tracks_length : track_offsets[i + 1]) - track_offsets[i];
        uint32_t src = tracks_offset + track_offsets[i];
        if (src + track_len > size) {
            // Truncate to available data
            track_len = (src < size) ? (uint32_t)(size - src) : 0;
        }

        trks[i].track_data = (uint8_t*)malloc(track_len > 0 ? track_len : 1);
        if (trks[i].track_data == nullptr) {
            for (uint32_t j = 0; j < i; j++) {
                free(trks[j].track_data);
            }
            free(trks);
            free(track_offsets);
            return false;
        }
        if (track_len > 0) {
            memcpy(trks[i].track_data, data + src, track_len);
        }
        trks[i].length = track_len;
    }

    // Sample data starts right after tracks block.
    // Note: C# has "sampleOffset % 1 != 0" which is always false (no-op).
    uint32_t sample_off = tracks_offset + tracks_length;

    *out_tracks = trks;
    *out_count = (int32_t)num_tracks;
    *out_sample_offset = sample_off;

    free(track_offsets);
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool load_instruments(const uint8_t* data, size_t size, uint32_t offset, uint32_t length,
                             Instrument** out_instruments, int32_t* out_count) {
    if ((length % 32) != 0) {
        return false;
    }
    if (offset + length > size) {
        return false;
    }

    uint32_t count = length / 32;
    Instrument* insts = (Instrument*)calloc(count, sizeof(Instrument));
    if (insts == nullptr) {
        return false;
    }

    size_t pos = offset;
    for (uint32_t i = 0; i < count; i++) {
        insts[i].waveform_list_number = read_u8(data, &pos);
        insts[i].waveform_list_length = read_u8(data, &pos);
        insts[i].waveform_list_speed = read_u8(data, &pos);
        insts[i].waveform_list_delay = read_u8(data, &pos);
        insts[i].arpeggio_number = read_u8(data, &pos);
        insts[i].arpeggio_length = read_u8(data, &pos);
        insts[i].arpeggio_speed = read_u8(data, &pos);
        insts[i].arpeggio_delay = read_u8(data, &pos);
        insts[i].vibrato_number = read_u8(data, &pos);
        insts[i].vibrato_length = read_u8(data, &pos);
        insts[i].vibrato_speed = read_u8(data, &pos);
        insts[i].vibrato_delay = read_u8(data, &pos);
        insts[i].pitch_bend_speed = read_i8(data, &pos);
        insts[i].pitch_bend_delay = read_u8(data, &pos);
        pos += 2; // skip 2 bytes
        insts[i].attack_max = read_u8(data, &pos);
        insts[i].attack_speed = read_u8(data, &pos);
        insts[i].decay_min = read_u8(data, &pos);
        insts[i].decay_speed = read_u8(data, &pos);
        insts[i].sustain_time = read_u8(data, &pos);
        insts[i].release_min = read_u8(data, &pos);
        insts[i].release_speed = read_u8(data, &pos);
        pos += 9; // skip 9 bytes
    }

    *out_instruments = insts;
    *out_count = (int32_t)count;
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool load_samples(const uint8_t* data, size_t size, uint32_t sample_offset, uint16_t num_samples,
                         uint32_t info_offset, uint32_t info_length, Sample** out_samples,
                         SampleNegateInfo** out_negate) {
    if (info_offset + info_length > size) {
        return false;
    }

    Sample* samps = (Sample*)calloc(num_samples, sizeof(Sample));
    SampleNegateInfo* negates = (SampleNegateInfo*)calloc(num_samples, sizeof(SampleNegateInfo));
    if (samps == nullptr || negates == nullptr) {
        free(samps);
        free(negates);
        return false;
    }

    // Read sample info (each entry is 64 bytes: 4 ptr + 6 sample + 22 negate + 4 skip + 32 name = 68 bytes? )
    // Actually: 4 (ptr) + 2+2+2 (len/loopstart/looplen) + 2+2+2+2+2+4+2+2 (negate=18) + 4 (skip) + 32 (name) = 64
    size_t pos = info_offset;
    for (int i = 0; i < num_samples; i++) {
        if (pos + 64 > size) {
            free(samps);
            free(negates);
            return false;
        }

        pos += 4; // skip sample data pointer

        samps[i].length = (uint32_t)read_be16(data, &pos) * 2;
        samps[i].loop_start = (uint32_t)read_be16(data, &pos) * 2;
        samps[i].loop_length = (uint32_t)read_be16(data, &pos) * 2;

        if (samps[i].loop_start > samps[i].length) {
            samps[i].loop_start = 0;
            samps[i].loop_length = 0;
        } else if (samps[i].loop_start + samps[i].loop_length > samps[i].length) {
            samps[i].loop_length = samps[i].length - samps[i].loop_start;
        }

        // Negate info
        negates[i].start_offset = (uint32_t)read_be16(data, &pos) * 2;
        negates[i].end_offset = (uint32_t)read_be16(data, &pos) * 2;
        negates[i].loop_index = read_be16(data, &pos);
        negates[i].status = read_be16(data, &pos);
        negates[i].speed = read_be16_signed(data, &pos);
        negates[i].position = read_be32_signed(data, &pos);
        negates[i].index = read_be16(data, &pos);
        negates[i].do_negation = read_be16_signed(data, &pos);

        pos += 4;  // skip 4 bytes
        pos += 32; // skip name
    }

    // Read sample data
    size_t sdata_pos = sample_offset;
    for (int i = 0; i < num_samples; i++) {
        if (samps[i].length == 0) {
            samps[i].sample_data = (int8_t*)calloc(4, 1);
            continue;
        }
        if (sdata_pos + samps[i].length > size) {
            // Truncate or fail
            uint32_t avail = (sdata_pos < size) ? (uint32_t)(size - sdata_pos) : 0;
            samps[i].sample_data = (int8_t*)calloc(samps[i].length, 1);
            if (samps[i].sample_data != nullptr && avail > 0) {
                memcpy(samps[i].sample_data, data + sdata_pos, avail);
            }
            sdata_pos += samps[i].length;
            continue;
        }
        samps[i].sample_data = (int8_t*)malloc(samps[i].length);
        if (samps[i].sample_data == nullptr) {
            // cleanup
            for (int j = 0; j < i; j++) {
                free(samps[j].sample_data);
            }
            free(samps);
            free(negates);
            return false;
        }
        memcpy(samps[i].sample_data, data + sdata_pos, samps[i].length);
        sdata_pos += samps[i].length;
    }

    *out_samples = samps;
    *out_negate = negates;
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool sd2_parse(Sd2Module* mod, const uint8_t* data, size_t size) {
    if (!sd2_identify(data, size)) {
        return false;
    }

    size_t pos = 2; // skip MIDI mode
    uint8_t num_pos = read_u8(data, &pos);
    num_pos++;
    mod->start_speed = read_u8(data, &pos);

    uint16_t num_samples = read_be16(data, &pos);
    num_samples /= 64;

    // Block offsets: start after header
    // offset 6: 10 x uint32 block lengths
    // Block data starts at offset 58 + id_length + song_length_length

    uint32_t offset = 58;

    // Read id_length and song_length_length
    uint32_t id_length = read_be32(data, &pos);     // at pos 6
    uint32_t song_len_length = read_be32(data, &pos); // at pos 10

    offset += id_length;
    offset += song_len_length;

    // Read 10 block lengths
    uint32_t block_lengths[10];
    for (int i = 0; i < 10; i++) {
        block_lengths[i] = read_be32(data, &pos);
    }

    // Compute block offsets
    uint32_t block_offsets[10];
    uint32_t cur_offset = offset;
    for (int i = 0; i < 10; i++) {
        block_offsets[i] = cur_offset;
        cur_offset += block_lengths[i];
    }

    // Block indices (from C# LoadSongData order):
    // 0: positionTable, 1: noteTranspose, 2: instrumentTranspose
    // 3: instruments, 4: waveformList, 5: arpeggios
    // 6: vibratoes, 7: sampleInfo, 8: trackTable, 9: tracks

    // Load lists
    if (!load_single_list_u8(data, size, block_offsets[4], block_lengths[4],
                             &mod->waveform_info, &mod->waveform_count)) {
        return false;
    }

    if (!load_single_list_i8(data, size, block_offsets[5], block_lengths[5],
                             &mod->arpeggios, &mod->arpeggio_count)) {
        return false;
    }

    if (!load_single_list_i8(data, size, block_offsets[6], block_lengths[6],
                             &mod->vibratoes, &mod->vibrato_count)) {
        return false;
    }

    // Load sequences
    mod->num_positions = num_pos;
    if (!load_sequences(data, size, block_offsets[0], block_lengths[0],
                        block_offsets[1], block_lengths[1],
                        block_offsets[2], block_lengths[2],
                        num_pos, &mod->sequences)) {
        return false;
    }

    // Load tracks
    uint32_t sample_data_offset = 0;
    if (!load_tracks(data, size, block_offsets[8], block_lengths[8],
                     block_offsets[9], block_lengths[9],
                     &mod->tracks, &mod->track_count, &sample_data_offset)) {
        return false;
    }

    // Load instruments
    if (!load_instruments(data, size, block_offsets[3], block_lengths[3],
                          &mod->instruments, &mod->instrument_count)) {
        return false;
    }

    // Load samples
    mod->sample_count = num_samples;
    if (!load_samples(data, size, sample_data_offset, num_samples,
                      block_offsets[7], block_lengths[7],
                      &mod->samples, &mod->sample_negate_info)) {
        return false;
    }

    // numberOfPositions-- (C# does this after loading)
    mod->number_of_positions = (uint8_t)(num_pos - 1);

    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Voice rendering helpers
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void update_period(VoiceRender* vr, uint16_t period, float sample_rate) {
    vr->period = period;
    if (period > 0) {
        // NP mixer: frequency = 3546895 / period (integer), then increment = (freq << FracBits) / mixerRate
        uint32_t frequency = 3546895 / period;
        vr->sample_inc = (uint32_t)(((uint64_t)frequency << SAMPLE_FRAC_BITS) / (uint32_t)sample_rate);
    } else {
        vr->sample_inc = 0;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void voice_render_play_sample(VoiceRender* vr, const int8_t* sample_data, uint32_t offset,
                                     uint32_t length) {
    // Matches C# PlaySample: sets sampleInfo immediately, clears newSampleInfo and loop
    vr->sample_data = sample_data;
    vr->sample_length = offset + length;
    vr->sample_pos = offset << SAMPLE_FRAC_BITS;
    vr->active = true;
    vr->has_pending_sample = false;
    vr->has_pending_loop = false;
    // Clear loop (C#: sampleInfo.Loop = null)
    vr->loop_data = nullptr;
    vr->loop_offset = 0;
    vr->loop_length = 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void voice_render_set_loop(VoiceRender* vr, const int8_t* loop_data, uint32_t loop_offset,
                                  uint32_t loop_length) {
    // C# SetLoop: operates on newSampleInfo ?? sampleInfo
    // If there's a pending sample, set loop on it; otherwise set directly on current
    if (vr->has_pending_sample) {
        vr->has_pending_loop = true;
        vr->pending_loop_data = loop_data;
        vr->pending_loop_offset = loop_offset;
        vr->pending_loop_length = loop_length;
    } else {
        vr->loop_data = loop_data;
        vr->loop_offset = loop_offset;
        vr->loop_length = loop_length;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void voice_render_set_sample(VoiceRender* vr, const int8_t* sample_data, uint32_t offset,
                                    uint32_t length) {
    vr->has_pending_sample = true;
    vr->pending_sample_data = sample_data;
    vr->pending_sample_length = offset + length;
    vr->pending_loop_data = sample_data;
    vr->pending_loop_offset = offset;
    vr->pending_loop_length = length;
    vr->has_pending_loop = true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void voice_render_mute(VoiceRender* vr) {
    vr->active = false;
    vr->volume = 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Player state init
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void find_note(Sd2Module* mod, VoiceInfo* vi) {
    Sequence* seq = &mod->sequences[0]; // base for this channel
    // Find correct sequence for this channel
    int ch_idx = (int)(vi - &mod->voices[0]);
    seq = &mod->sequences[ch_idx * mod->num_positions + mod->current_position];

    if (seq->track_number < mod->track_count) {
        vi->current_track = mod->tracks[seq->track_number].track_data;
        vi->current_track_length = mod->tracks[seq->track_number].length;
    } else {
        vi->current_track = nullptr;
        vi->current_track_length = 0;
    }
    vi->track_position = 0;
    vi->note_transpose = seq->note_transpose;
    vi->instrument_transpose = seq->instrument_transpose;
    vi->empty_notes_counter = 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void init_sound(Sd2Module* mod) {
    mod->current_position = 0;
    mod->current_row = 0;
    mod->pattern_length = 64;
    mod->current_rast = 0;
    mod->current_rast2 = 0;
    mod->speed = mod->start_speed;

    for (int i = 0; i < 4; i++) {
        VoiceInfo* vi = &mod->voices[i];
        memset(vi, 0, sizeof(VoiceInfo));

        vi->sequence_list = &mod->sequences[i * mod->num_positions];

        Instrument* inst = &mod->instruments[0];
        vi->instrument = inst;

        uint8_t sample_number = mod->waveform_info[inst->waveform_list_number][0];
        vi->current_sample = sample_number;

        if (sample_number < mod->sample_count) {
            Sample* smp = &mod->samples[sample_number];
            vi->sample_data = smp->sample_data;
            vi->sample_length = smp->length;
            vi->loop_sample = smp->sample_data;
            vi->loop_offset = smp->loop_start;
            vi->loop_length = smp->loop_length;
        }

        vi->envelope_state = EnvelopeState_Attack;
        vi->sample_period = 0;

        // Init render state
        VoiceRender* vr = &mod->voice_render[i];
        memset(vr, 0, sizeof(VoiceRender));

        find_note(mod, vi);
    }

    // frames_per_tick: NostalgicPlayer default is 50Hz (PAL VBI)
    mod->frames_per_tick = mod->sample_rate / 50.0f;
    mod->frames_until_tick = 0.0f;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Note and voice processing
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void get_note2(Sd2Module* mod, VoiceInfo* vi) {
    uint8_t* track_data = vi->current_track;

    vi->current_note = 0;
    vi->current_instrument = 0;
    vi->current_effect = 0;
    vi->current_effect_arg = 0;

    if (vi->empty_notes_counter == 0) {
        int8_t track_value;
        if (track_data == nullptr || vi->track_position >= (int32_t)vi->current_track_length) {
            track_value = -1;
        } else {
            track_value = (int8_t)track_data[vi->track_position++];
        }

        if (track_value == 0) {
            if (vi->track_position + 1 < (int32_t)vi->current_track_length) {
                vi->current_effect = track_data[vi->track_position++];
                vi->current_effect_arg = track_data[vi->track_position++];
            }
            return;
        }

        if (track_value > 0) {
            if (track_value >= 0x70) {
                vi->current_effect = (uint8_t)track_value;
                if (vi->track_position < (int32_t)vi->current_track_length) {
                    vi->current_effect_arg = track_data[vi->track_position++];
                }
                return;
            }

            vi->current_note = (uint16_t)track_value;

            if (vi->track_position < (int32_t)vi->current_track_length) {
                track_value = (int8_t)track_data[vi->track_position++];
                if (track_value >= 0) {
                    if (track_value >= 0x70) {
                        vi->current_effect = (uint8_t)track_value;
                        if (vi->track_position < (int32_t)vi->current_track_length) {
                            vi->current_effect_arg = track_data[vi->track_position++];
                        }
                        return;
                    }

                    vi->current_instrument = (uint16_t)track_value;

                    if (vi->track_position < (int32_t)vi->current_track_length) {
                        track_value = (int8_t)track_data[vi->track_position++];
                        if (track_value >= 0) {
                            vi->current_effect = (uint8_t)track_value;
                            if (vi->track_position < (int32_t)vi->current_track_length) {
                                vi->current_effect_arg = track_data[vi->track_position++];
                            }
                            return;
                        }
                    } else {
                        return;
                    }
                }
            } else {
                return;
            }
        }

        vi->empty_notes_counter = (uint16_t)(~(int)track_value);
    } else {
        vi->empty_notes_counter--;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void get_note(Sd2Module* mod, VoiceInfo* vi) {
    get_note2(mod, vi);

    if (vi->current_note != 0) {
        vi->current_note = (uint16_t)(vi->current_note + vi->note_transpose);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void play_voice(Sd2Module* mod, VoiceInfo* vi, VoiceRender* vr) {
    vi->pitch_bend_value = 0;

    if (vi->current_note != 0) {
        vi->sample_volume = 0;
        vi->wave_list_delay = 0;
        vi->wave_list_offset = 0;
        vi->arpeggio_delay = 0;
        vi->arpeggio_offset = 0;
        vi->vibrato_delay = 0;
        vi->vibrato_offset = 0;
        vi->pitch_bend_counter = 0;
        vi->note_slide_speed = 0;

        vi->envelope_state = EnvelopeState_Attack;
        vi->sustain_counter = 0;

        uint16_t instrument = vi->current_instrument;
        if (instrument != 0) {
            instrument = (uint16_t)(instrument - 1 + vi->instrument_transpose);
            if (instrument < (uint16_t)mod->instrument_count) {
                vi->instrument = &mod->instruments[instrument];

                if (vi->instrument->waveform_list_number < mod->waveform_count) {
                    uint8_t* waveform_list = mod->waveform_info[vi->instrument->waveform_list_number];
                    vi->current_sample = waveform_list[0];

                    if (vi->current_sample < (uint16_t)mod->sample_count) {
                        Sample* smp = &mod->samples[vi->current_sample];
                        vi->sample_data = smp->sample_data;
                        vi->sample_length = smp->length;
                        vi->loop_sample = smp->sample_data;
                        vi->loop_offset = smp->loop_start;
                        vi->loop_length = smp->loop_length;
                    }
                }
            }
        }

        if (vi->instrument->arpeggio_number < mod->arpeggio_count) {
            int8_t* arpeggio = mod->arpeggios[vi->instrument->arpeggio_number];
            int note = vi->current_note + arpeggio[0];
            if (note >= 0 && note < (int)PERIODS_COUNT) {
                vi->original_note = (uint16_t)note;
                vi->sample_period = s_periods[note];

                if (vi->sample_length != 0) {
                    voice_render_play_sample(vr, vi->sample_data, 0, vi->sample_length);

                    if (vi->loop_length > 2) {
                        voice_render_set_loop(vr, vi->loop_sample, vi->loop_offset, vi->loop_length);
                    }
                } else {
                    voice_render_mute(vr);
                }
            } else {
                voice_render_mute(vr);
            }
        } else {
            voice_render_mute(vr);
        }

        update_period(vr, vi->sample_period, mod->sample_rate);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Effect processing
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void do_adsr_curve(VoiceInfo* vi, VoiceRender* vr) {
    Instrument* inst = vi->instrument;
    (void)vr;

    switch (vi->envelope_state) {
        case EnvelopeState_Attack:
            vi->sample_volume += inst->attack_speed;
            if (vi->sample_volume >= inst->attack_max) {
                vi->sample_volume = inst->attack_max;
                vi->envelope_state = EnvelopeState_Decay;
            }
            break;

        case EnvelopeState_Decay:
            if (inst->decay_speed == 0) {
                vi->envelope_state = EnvelopeState_Sustain;
            } else {
                vi->sample_volume -= inst->decay_speed;
                if (vi->sample_volume <= inst->decay_min) {
                    vi->sample_volume = inst->decay_min;
                    vi->envelope_state = EnvelopeState_Sustain;
                }
            }
            break;

        case EnvelopeState_Sustain:
            if (vi->sustain_counter == inst->sustain_time) {
                vi->envelope_state = EnvelopeState_Release;
            } else {
                vi->sustain_counter++;
            }
            break;

        case EnvelopeState_Release:
            if (inst->release_speed == 0) {
                vi->envelope_state = EnvelopeState_Done;
            } else {
                vi->sample_volume -= inst->release_speed;
                if (vi->sample_volume <= inst->release_min) {
                    vi->sample_volume = inst->release_min;
                    vi->envelope_state = EnvelopeState_Done;
                }
            }
            break;

        case EnvelopeState_Done:
            break;
    }

    // SetVolume maps to vr->volume. C# uses 0-256 range.
    uint16_t vol = (vi->sample_volume < 0) ? 0 : (uint16_t)vi->sample_volume;
    vr->volume = vol;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void do_waveform(Sd2Module* mod, VoiceInfo* vi, VoiceRender* vr) {
    Instrument* inst = vi->instrument;

    if (inst->waveform_list_length != 0) {
        if (vi->wave_list_delay == inst->waveform_list_delay) {
            vi->wave_list_delay -= inst->waveform_list_speed;

            if (vi->wave_list_offset == inst->waveform_list_length) {
                vi->wave_list_offset = -1;
            }

            vi->wave_list_offset++;

            if (inst->waveform_list_number < mod->waveform_count && vi->wave_list_offset < 16) {
                int8_t waveform_value =
                    (int8_t)mod->waveform_info[inst->waveform_list_number][vi->wave_list_offset];
                if (waveform_value >= 0) {
                    vi->current_sample = (uint16_t)waveform_value;

                    if (vi->current_sample < (uint16_t)mod->sample_count) {
                        Sample* smp = &mod->samples[vi->current_sample];
                        vi->loop_sample = smp->sample_data;
                        vi->loop_offset = smp->loop_start;
                        vi->loop_length = smp->loop_length;

                        if (vr->active) {
                            // SetSample (deferred) + SetLoop
                            voice_render_set_sample(vr, vi->loop_sample, vi->loop_offset, vi->loop_length);
                            // The C# also calls SetLoop(offset, length) separately but set_sample already sets loop
                        }
                    }
                } else {
                    vi->wave_list_offset--;
                }
            }
        } else {
            vi->wave_list_delay++;
        }
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void do_arpeggio(Sd2Module* mod, VoiceInfo* vi) {
    Instrument* inst = vi->instrument;

    if (inst->arpeggio_length != 0) {
        if (vi->arpeggio_delay == inst->arpeggio_delay) {
            vi->arpeggio_delay -= inst->arpeggio_speed;

            if (vi->arpeggio_offset == inst->arpeggio_length) {
                vi->arpeggio_offset = -1;
            }

            vi->arpeggio_offset++;

            if (inst->arpeggio_number < mod->arpeggio_count && vi->arpeggio_offset < 16) {
                int8_t arp_value = mod->arpeggios[inst->arpeggio_number][vi->arpeggio_offset];
                int16_t new_note = (int16_t)(vi->original_note + arp_value);
                if (new_note >= 0 && new_note < (int16_t)PERIODS_COUNT) {
                    vi->sample_period = s_periods[new_note];
                }
            }
        } else {
            vi->arpeggio_delay++;
        }
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void do_vibrato(Sd2Module* mod, VoiceInfo* vi) {
    Instrument* inst = vi->instrument;

    if (inst->vibrato_length != 0) {
        if (vi->vibrato_delay == inst->vibrato_delay) {
            vi->vibrato_delay -= inst->vibrato_speed;

            if (vi->vibrato_offset == inst->vibrato_length) {
                vi->vibrato_offset = -1;
            }

            vi->vibrato_offset++;

            if (inst->vibrato_number < mod->vibrato_count && vi->vibrato_offset < 16) {
                int8_t vib_value = mod->vibratoes[inst->vibrato_number][vi->vibrato_offset];
                vi->sample_period = (uint16_t)(vi->sample_period + vib_value);
            }
        } else {
            vi->vibrato_delay++;
        }
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void do_pitch_bend(VoiceInfo* vi) {
    Instrument* inst = vi->instrument;

    if (inst->pitch_bend_speed != 0) {
        if (vi->pitch_bend_counter == inst->pitch_bend_delay) {
            vi->pitch_bend_value = (int16_t)(vi->pitch_bend_value + inst->pitch_bend_speed);
        } else {
            vi->pitch_bend_counter++;
        }
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void do_note_slide(VoiceInfo* vi) {
    if (vi->current_effect != 0 && vi->current_effect < 0x70 && vi->current_effect_arg != 0) {
        if (vi->current_effect < PERIODS_COUNT) {
            vi->note_slide_note = s_periods[vi->current_effect];
        }

        int direction = (int)vi->note_slide_note - (int)vi->sample_period;
        if (direction == 0) {
            return;
        }

        int16_t effect_arg = (int16_t)vi->current_effect_arg;
        if (direction < 0) {
            effect_arg = (int16_t)-effect_arg;
        }

        vi->note_slide_speed = effect_arg;
    }

    int16_t speed = vi->note_slide_speed;
    if (speed != 0) {
        if (speed < 0) {
            vi->sample_period = (uint16_t)(vi->sample_period + speed);
            if (vi->sample_period <= vi->note_slide_note) {
                vi->sample_period = vi->note_slide_note;
                vi->note_slide_speed = 0;
            }
        } else {
            vi->sample_period = (uint16_t)(vi->sample_period + speed);
            if (vi->sample_period >= vi->note_slide_note) {
                vi->sample_period = vi->note_slide_note;
                vi->note_slide_speed = 0;
            }
        }
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void do_sound_tracker(Sd2Module* mod, VoiceInfo* vi, VoiceRender* vr) {
    uint16_t effect = vi->current_effect;
    if (effect >= 0x70) {
        effect &= 0x0f;

        if (mod->current_rast != 0 || effect >= 5) {
            switch (effect) {
                // Arpeggio
                case 0x0: {
                    uint8_t arp_tab[] = {
                        (uint8_t)(vi->current_effect_arg >> 4),
                        0,
                        (uint8_t)(vi->current_effect_arg & 0x0f),
                        0
                    };
                    uint8_t arp_value = arp_tab[mod->current_rast2];
                    int idx = vi->original_note + arp_value;
                    if (idx >= 0 && idx < (int)PERIODS_COUNT) {
                        vi->sample_period = s_periods[idx];
                    }
                    break;
                }

                // Pitch up
                case 0x1:
                    vi->pitch_bend_value = (int16_t)(-(int16_t)vi->current_effect_arg);
                    break;

                // Pitch down
                case 0x2:
                    vi->pitch_bend_value = (int16_t)vi->current_effect_arg;
                    break;

                // Volume up
                case 0x3:
                    if (vi->envelope_state == EnvelopeState_Done) {
                        if (mod->current_rast == 0 && vi->current_instrument != 0) {
                            vi->sample_volume = vi->instrument->attack_speed;
                        }
                        int16_t volume = (int16_t)(vi->sample_volume + vi->current_effect_arg * 4);
                        if (volume >= 256) {
                            volume = 255;
                        }
                        vi->sample_volume = volume;
                    }
                    break;

                // Volume down
                case 0x4:
                    if (vi->envelope_state == EnvelopeState_Done) {
                        if (mod->current_rast == 0 && vi->current_instrument != 0) {
                            vi->sample_volume = vi->instrument->attack_speed;
                        }
                        int16_t volume = (int16_t)(vi->sample_volume - vi->current_effect_arg * 4);
                        if (volume < 0) {
                            volume = 0;
                        }
                        vi->sample_volume = volume;
                    }
                    break;

                // Set ADSR attack
                case 0x5:
                    vi->instrument->attack_max = (uint8_t)vi->current_effect_arg;
                    vi->instrument->attack_speed = (uint8_t)vi->current_effect_arg;
                    break;

                // Set pattern length
                case 0x6:
                    if (vi->current_effect_arg != 0) {
                        mod->pattern_length = (uint8_t)vi->current_effect_arg;
                    }
                    break;

                // Volume change
                case 0xc: {
                    uint16_t volume = vi->current_effect_arg;
                    if (volume > 64) {
                        volume = 64;
                    }
                    // SetAmigaVolume: convert 0-64 to mixer volume
                    // We store the *4 version as sample_volume
                    uint16_t vol4 = volume * 4;
                    if (vol4 >= 255) {
                        vol4 = 255;
                    }
                    vi->sample_volume = (int16_t)vol4;
                    vr->volume = vol4;
                    break;
                }

                // Speed change
                case 0xf: {
                    uint8_t new_speed = (uint8_t)(vi->current_effect_arg & 0x0f);
                    if (new_speed != 0 && new_speed != mod->speed) {
                        mod->speed = new_speed;
                    }
                    break;
                }

                default:
                    break;
            }
        }
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void do_effect(Sd2Module* mod, VoiceInfo* vi, VoiceRender* vr) {
    do_adsr_curve(vi, vr);
    do_waveform(mod, vi, vr);
    do_arpeggio(mod, vi);
    do_sound_tracker(mod, vi, vr);
    do_vibrato(mod, vi);
    do_pitch_bend(vi);
    do_note_slide(vi);

    vi->sample_period = (uint16_t)(vi->sample_period + vi->pitch_bend_value);

    if (vi->sample_period < 95) {
        vi->sample_period = 95;
    } else if (vi->sample_period > 5760) {
        vi->sample_period = 5760;
    }

    update_period(vr, vi->sample_period, mod->sample_rate);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Sample negation
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void do_negation(Sd2Module* mod) {
    SampleNegateInfo* working[4];

    for (int i = 0; i < 4; i++) {
        VoiceInfo* vi = &mod->voices[i];
        uint16_t sample_number = vi->current_sample;
        if (sample_number >= (uint16_t)mod->sample_count) {
            working[i] = nullptr;
            continue;
        }
        SampleNegateInfo* ni = &mod->sample_negate_info[sample_number];
        working[i] = ni;

        if (ni->do_negation == 0) {
            ni->do_negation = -1;

            if (ni->index == 0) {
                ni->index = ni->loop_index;

                if (ni->status != 0) {
                    Sample* smp = &mod->samples[sample_number];
                    uint32_t end_offset = ni->end_offset - 1;

                    int32_t position = (int32_t)(ni->start_offset + ni->position);
                    if (position >= 0 && position < (int32_t)smp->length) {
                        smp->sample_data[position] = (int8_t)~smp->sample_data[position];
                    }

                    ni->position += ni->speed;
                    if (ni->position < 0) {
                        if (ni->status == 2) {
                            ni->position = (int32_t)end_offset;
                        } else {
                            ni->position += -ni->speed;
                            ni->speed = (int16_t)-ni->speed;
                        }
                    } else {
                        if ((uint32_t)ni->position > end_offset) {
                            if (ni->status == 1) {
                                ni->position = 0;
                            } else {
                                ni->position += -ni->speed;
                                ni->speed = (int16_t)-ni->speed;
                            }
                        }
                    }
                }
            } else {
                ni->index++;
                ni->index &= 0x1f;
            }
        }
    }

    for (int i = 0; i < 4; i++) {
        if (working[i] != nullptr) {
            working[i]->do_negation = 0;
        }
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Tick function
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void player_tick(Sd2Module* mod) {
    mod->current_rast2++;
    if (mod->current_rast2 == 3) {
        mod->current_rast2 = 0;
    }

    mod->current_rast++;
    if (mod->current_rast >= mod->speed) {
        mod->current_rast = 0;
        mod->current_rast2 = 0;

        for (int i = 0; i < 4; i++) {
            get_note(mod, &mod->voices[i]);
        }

        for (int i = 0; i < 4; i++) {
            play_voice(mod, &mod->voices[i], &mod->voice_render[i]);
        }

        do_negation(mod);

        mod->current_row++;
        if (mod->current_row == mod->pattern_length) {
            mod->current_row = 0;

            if (mod->current_position == mod->number_of_positions) {
                mod->current_position = -1;
                mod->ended = true;
            }

            mod->current_position++;

            for (int i = 0; i < 4; i++) {
                find_note(mod, &mod->voices[i]);
            }
        }
    }

    for (int i = 0; i < 4; i++) {
        do_effect(mod, &mod->voices[i], &mod->voice_render[i]);
    }

    if (mod->current_rast != 0) {
        do_negation(mod);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Render function
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void mix_voices(Sd2Module* mod, float* stereo, size_t frames) {
    for (int ch = 0; ch < 4; ch++) {
        if (!(mod->channel_mask & (1U << ch))) {
            continue;
        }

        VoiceRender* vr = &mod->voice_render[ch];
        if (!vr->active || vr->sample_inc == 0) {
            continue;
        }

        float gain = (float)vr->volume / 512.0f; // 0-256 range / 256 * 0.5 NP mixer gain
        // Amiga panning: 0,3=left, 1,2=right
        int stereo_ch = (ch == 0 || ch == 3) ? 0 : 1;

        // Compute fixed-point end positions (matching NP mixer: ((start+length) << FracBits) - 1)
        uint32_t idx_end = vr->sample_length > 0
            ? (vr->sample_length << SAMPLE_FRAC_BITS) - 1 : 0;
        uint32_t idx_loop_end = (vr->loop_length > 2)
            ? ((vr->loop_offset + vr->loop_length) << SAMPLE_FRAC_BITS) - 1 : 0;
        uint32_t idx_loop_start = vr->loop_offset << SAMPLE_FRAC_BITS;

        for (size_t i = 0; i < frames; i++) {
            // Check loop/end boundaries (NP checks loop end for looping, sample end for non-looping)
            if (vr->loop_length > 2) {
                // Looping sample: check against loop end
                if (vr->sample_pos >= idx_loop_end) {
                    if (vr->has_pending_sample) {
                        // New sample pending: reset position exactly (no overflow)
                        vr->sample_data = vr->pending_sample_data;
                        vr->sample_length = vr->pending_sample_length;
                        vr->has_pending_sample = false;
                        vr->sample_pos = vr->pending_loop_offset << SAMPLE_FRAC_BITS;
                        if (vr->has_pending_loop) {
                            vr->loop_data = vr->pending_loop_data;
                            vr->loop_offset = vr->pending_loop_offset;
                            vr->loop_length = vr->pending_loop_length;
                            vr->has_pending_loop = false;
                        }
                        idx_end = vr->sample_length > 0
                            ? (vr->sample_length << SAMPLE_FRAC_BITS) - 1 : 0;
                        idx_loop_start = vr->loop_offset << SAMPLE_FRAC_BITS;
                        idx_loop_end = (vr->loop_length > 2)
                            ? ((vr->loop_offset + vr->loop_length) << SAMPLE_FRAC_BITS) - 1 : 0;
                    } else {
                        // Normal loop: preserve fractional overflow
                        vr->sample_data = vr->loop_data;
                        vr->sample_length = vr->loop_offset + vr->loop_length;
                        uint32_t new_loop_start = vr->loop_offset << SAMPLE_FRAC_BITS;
                        uint32_t new_loop_end = (vr->loop_length > 2)
                            ? ((vr->loop_offset + vr->loop_length) << SAMPLE_FRAC_BITS) - 1 : 0;
                        vr->sample_pos = new_loop_start + (vr->sample_pos - idx_loop_end);
                        idx_loop_start = new_loop_start;
                        idx_loop_end = new_loop_end;
                        idx_end = vr->sample_length > 0
                            ? (vr->sample_length << SAMPLE_FRAC_BITS) - 1 : 0;
                    }
                }
            } else {
                // Non-looping sample: check against sample end
                if (vr->sample_pos >= idx_end) {
                    if (vr->has_pending_sample) {
                        vr->sample_data = vr->pending_sample_data;
                        vr->sample_length = vr->pending_sample_length;
                        vr->has_pending_sample = false;
                        vr->sample_pos = vr->pending_loop_offset << SAMPLE_FRAC_BITS;
                        if (vr->has_pending_loop) {
                            vr->loop_data = vr->pending_loop_data;
                            vr->loop_offset = vr->pending_loop_offset;
                            vr->loop_length = vr->pending_loop_length;
                            vr->has_pending_loop = false;
                        }
                        idx_end = vr->sample_length > 0
                            ? (vr->sample_length << SAMPLE_FRAC_BITS) - 1 : 0;
                        idx_loop_end = (vr->loop_length > 2)
                            ? ((vr->loop_offset + vr->loop_length) << SAMPLE_FRAC_BITS) - 1 : 0;
                        idx_loop_start = vr->loop_offset << SAMPLE_FRAC_BITS;
                    } else {
                        vr->active = false;
                        break;
                    }
                }
            }

            uint32_t int_pos = vr->sample_pos >> SAMPLE_FRAC_BITS;

            // No interpolation (NP default: InterpolationMode.None)
            int32_t sample_int = (int32_t)vr->sample_data[int_pos] * 256;
            float sample_val = (float)sample_int / 32768.0f;
            stereo[i * 2 + stereo_ch] += sample_val * gain;

            vr->sample_pos += vr->sample_inc;
        }
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static size_t sd2_render_internal(Sd2Module* mod, float* stereo, size_t frames) {
    memset(stereo, 0, frames * 2 * sizeof(float));

    size_t pos = 0;
    while (pos < frames) {
        if (mod->frames_until_tick <= 0.0f) {
            player_tick(mod);
            mod->frames_until_tick += mod->frames_per_tick;
        }

        size_t chunk = frames - pos;
        if ((float)chunk > mod->frames_until_tick) {
            chunk = (size_t)mod->frames_until_tick;
        }
        if (chunk == 0) {
            chunk = 1;
        }

        mix_voices(mod, stereo + pos * 2, chunk);
        mod->frames_until_tick -= (float)chunk;
        pos += chunk;
    }

    return frames;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Player create/destroy
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

Sd2Module* sd2_create(const uint8_t* data, size_t size, float sample_rate) {
    if (data == nullptr || size == 0) {
        return nullptr;
    }

    if (!sd2_identify(data, size)) {
        return nullptr;
    }

    Sd2Module* mod = (Sd2Module*)calloc(1, sizeof(Sd2Module));
    if (mod == nullptr) {
        return nullptr;
    }

    mod->sample_rate = sample_rate;
    mod->channel_mask = 0xf; // all 4 channels enabled

    if (!sd2_parse(mod, data, size)) {
        sd2_destroy(mod);
        return nullptr;
    }

    init_sound(mod);
    return mod;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void sd2_destroy(Sd2Module* mod) {
    if (mod == nullptr) {
        return;
    }

    if (mod->waveform_info != nullptr) {
        free(mod->waveform_info[0]);  // free the flat data block
        free(mod->waveform_info);     // free the pointer array
    }

    if (mod->arpeggios != nullptr) {
        free(mod->arpeggios[0]);
        free(mod->arpeggios);
    }

    if (mod->vibratoes != nullptr) {
        free(mod->vibratoes[0]);
        free(mod->vibratoes);
    }

    free(mod->sequences);

    if (mod->tracks != nullptr) {
        for (int i = 0; i < mod->track_count; i++) {
            free(mod->tracks[i].track_data);
        }
        free(mod->tracks);
    }

    free(mod->instruments);

    if (mod->samples != nullptr) {
        for (int i = 0; i < mod->sample_count; i++) {
            free(mod->samples[i].sample_data);
        }
        free(mod->samples);
    }

    free(mod->sample_negate_info);
    free(mod);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Public API
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int sd2_subsong_count(const Sd2Module* mod) {
    if (mod == nullptr) {
        return 0;
    }
    return 1;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool sd2_select_subsong(Sd2Module* mod, int subsong) {
    if (mod == nullptr || subsong != 0) {
        return false;
    }
    init_sound(mod);
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int sd2_channel_count(const Sd2Module* mod) {
    if (mod == nullptr) {
        return 0;
    }
    return 4;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void sd2_set_channel_mask(Sd2Module* mod, uint32_t mask) {
    if (mod == nullptr) {
        return;
    }
    mod->channel_mask = mask;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

size_t sd2_render(Sd2Module* mod, float* interleaved_stereo, size_t frames) {
    if (mod == nullptr || interleaved_stereo == nullptr || frames == 0) {
        return 0;
    }
    return sd2_render_internal(mod, interleaved_stereo, frames);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool sd2_has_ended(const Sd2Module* mod) {
    if (mod == nullptr) {
        return true;
    }
    return mod->ended;
}
