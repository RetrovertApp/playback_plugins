// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Daniel Collin
// Ported from NostalgicPlayer C# implementation by Thomas Neumann (MIT)
#include "music_assembler.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef __cplusplus
#ifndef nullptr
#define nullptr ((void*)0)
#endif
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define SAMPLE_FRAC_BITS 11

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Types
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef enum MaLoopMode {
    MaLoopMode_PlayOnce = 0,
    MaLoopMode_Loop,
} MaLoopMode;

typedef struct MaRenderConfig {
    uint32_t sample_rate;
    float master_gain;
    MaLoopMode loop_mode;
    uint32_t channel_enable_mask;
} MaRenderConfig;

typedef struct SongInfo {
    uint8_t start_speed;
    uint16_t position_lists[4];
} SongInfo;

typedef struct PositionInfo {
    uint8_t track_number;
    uint8_t transpose;
    int8_t repeat_counter;
} PositionInfo;

typedef struct Track {
    uint8_t* data;
    size_t length;
} Track;

typedef struct Instrument {
    uint8_t sample_number;
    uint8_t attack;
    uint8_t decay_sustain;
    uint8_t release;
    uint8_t vibrato_delay;
    uint8_t vibrato_speed;
    uint8_t vibrato_level;
    uint8_t arpeggio;
    uint8_t fx_arp_spdlp;
    uint8_t hold;
    uint8_t key_wave_rate;
    uint8_t wave_level_speed;
} Instrument;

typedef struct Sample {
    char name[17];
    const int8_t* sample_data;
    uint16_t length;
    uint16_t loop_length;
} Sample;

typedef enum VoiceFlag {
    VoiceFlag_None = 0,
    VoiceFlag_SetLoop = 0x02,
    VoiceFlag_Synthesis = 0x04,
    VoiceFlag_Portamento = 0x20,
    VoiceFlag_Release = 0x40,
    VoiceFlag_Retrig = 0x80
} VoiceFlag;

typedef struct VoiceInfo {
    int channel_number;

    const PositionInfo* position_list;
    uint16_t position_list_length;

    uint16_t current_position;
    uint16_t current_track_row;
    int8_t track_repeat_counter;
    int8_t row_delay_counter;

    uint8_t flag;

    const Instrument* current_instrument;

    uint8_t current_note;
    uint8_t current_sample_number;
    uint8_t transpose;

    uint8_t volume;
    uint8_t sustain_counter;
    uint8_t arpeggio_counter;
    uint8_t arpeggio_value_to_use;
    uint8_t portamento_or_vibrato_value;
    uint8_t vibrato_delay_counter;
    uint8_t vibrato_direction;
    uint8_t vibrato_speed_counter;
    uint8_t wave_speed_counter;

    int16_t portamento_add_value;
    int16_t vibrato_add_value;

    uint16_t wave_length_modifier;

    bool decrease_volume;
    bool wave_direction;
    bool muted;

    const int8_t* sample_data;
    uint32_t sample_start_offset;
    uint16_t sample_length;

    // Mixer state
    const int8_t* play_data;
    uint32_t play_start_offset;
    uint16_t play_length_words;
    uint32_t play_loop_start_offset;
    uint32_t play_loop_length_bytes;
    const int8_t* pending_data;
    uint32_t pending_start_offset;
    uint16_t pending_length_words;
    uint32_t pending_loop_start_offset;
    uint32_t pending_loop_length_bytes;
    uint8_t pending_delay_ticks;
    bool pending_unmuted_this_tick;
    bool has_pending_sample;
    bool active;
    bool active_before_tick;
    bool muted_before_tick;
    uint64_t sample_pos_fp;
    uint32_t sample_step_fp;
    uint16_t current_period;
    uint16_t current_volume;
    uint16_t previous_volume;
    uint8_t ramp_counter;
} VoiceInfo;

typedef struct GlobalPlayingInfo {
    uint16_t master_volume;
    uint8_t speed;
    uint8_t speed_counter;
} GlobalPlayingInfo;

typedef struct PositionListEntry {
    uint16_t offset;
    PositionInfo* positions;
    uint16_t length;
} PositionListEntry;

typedef struct MaPlayer {
    char error_message[160];

    const MaModule* module;
    MaRenderConfig config;

    int current_subsong;
    bool ended;
    bool channel_end[4];

    GlobalPlayingInfo playing_info;
    VoiceInfo voices[4];

    float frames_until_tick;
    float frames_per_tick;
    bool filter_enabled;
    int64_t filter_a1;
    int64_t filter_a2;
    int64_t filter_b0;
    int64_t filter_b1;
    int64_t filter_b2;
    int64_t filter_x1[2];
    int64_t filter_x2[2];
    int64_t filter_y1[2];
    int64_t filter_y2[2];
} MaPlayer;

struct MaModule {
    char error_message[160];

    int sub_song_count;
    int sub_song_speed_offset;
    int sub_song_position_list_offset;
    int module_start_offset;
    int instrument_info_offset_offset;
    int sample_info_offset_offset;
    int tracks_offset_offset;

    SongInfo* songs;
    size_t song_count;

    PositionListEntry* position_lists;
    size_t position_list_count;

    Track* tracks;
    size_t track_count;

    Instrument* instruments;
    size_t instrument_count;

    Sample* samples;
    size_t sample_count;

    MaPlayer* player;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Globals
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static const int s_channel_map[4] = { 0, 3, 1, 2 };
static const uint16_t s_periods[48] = {
    1712, 1616, 1524, 1440, 1356, 1280, 1208, 1140, 1076, 1016, 960, 906,
    856,  808,  762,  720,  678,  640,  604,  570,  538,  508,  480, 453,
    428,  404,  381,  360,  339,  320,  302,  285,  269,  254,  240, 226,
    214,  202,  190,  180,  170,  160,  151,  143,  135,  127,  120, 113
};
static const int8_t s_empty_sample[4] = { 0, 0, 0, 0 };

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Module loading helpers
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void ma_set_module_error(MaModule* module, const char* message) {
    if (message == nullptr) {
        module->error_message[0] = '\0';
    } else {
        snprintf(module->error_message, sizeof(module->error_message), "%s", message);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static int ma_read_s16_be(const uint8_t* p) {
    return (int16_t)(((uint16_t)p[0] << 8) | p[1]);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint16_t ma_read_u16_be(const uint8_t* p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static int ma_read_i32_be(const uint8_t* p) {
    return (int32_t)(((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3]);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool ma_check_bounds(size_t offset, size_t need, size_t size) {
    return offset <= size && need <= (size - offset);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool ma_extract_info_from_init_function(MaModule* module, const uint8_t* search_buffer, size_t search_length) {
    int start_of_init = ma_read_s16_be(&search_buffer[2]) + 2;
    if (start_of_init < 0 || (size_t)start_of_init >= search_length) {
        return false;
    }

    size_t index = (size_t)start_of_init;
    for (; index + 4 < search_length; index += 2) {
        if (search_buffer[index] == 0xb0 && search_buffer[index + 1] == 0x7c) {
            break;
        }
    }

    if (index + 4 >= search_length) {
        return false;
    }

    module->sub_song_count = (search_buffer[index + 2] << 8) | search_buffer[index + 3];
    index += 4;

    for (; index + 4 < search_length; index += 2) {
        if (search_buffer[index] == 0x49 && search_buffer[index + 1] == 0xfa) {
            break;
        }
    }

    if (index + 4 >= search_length) {
        return false;
    }

    module->sub_song_speed_offset = ma_read_s16_be(&search_buffer[index + 2]) + (int)index + 2;
    index += 4;

    for (; index + 4 < search_length; index += 2) {
        if (search_buffer[index] == 0x49 && search_buffer[index + 1] == 0xfb) {
            break;
        }
    }

    if (index + 4 >= search_length) {
        return false;
    }

    module->sub_song_position_list_offset = (int8_t)search_buffer[index + 3] + (int)index + 2;
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool ma_extract_info_from_play_function(MaModule* module, const uint8_t* search_buffer, size_t search_length) {
    size_t index = 0x0c;

    for (; index + 4 < search_length; index += 2) {
        if (search_buffer[index] == 0x43 && search_buffer[index + 1] == 0xfa) {
            break;
        }
    }

    if (index + 4 >= search_length) {
        return false;
    }

    module->module_start_offset = ma_read_s16_be(&search_buffer[index + 2]) + (int)index + 2;
    index += 4;

    for (; index + 8 < search_length; index += 2) {
        if (search_buffer[index] == 0xd3 && search_buffer[index + 1] == 0xfa) {
            break;
        }
    }

    if (index + 8 >= search_length) {
        return false;
    }

    module->instrument_info_offset_offset = ma_read_s16_be(&search_buffer[index + 2]) + (int)index + 2;
    if (!(search_buffer[index + 4] == 0xd5 && search_buffer[index + 5] == 0xfa)) {
        return false;
    }

    module->sample_info_offset_offset = ma_read_s16_be(&search_buffer[index + 6]) + (int)index + 6;
    index += 8;

    for (; index + 2 < search_length; index += 2) {
        if (search_buffer[index] == 0x61) {
            break;
        }
    }

    if (index + 2 >= search_length) {
        return false;
    }

    int jump_target = (int8_t)search_buffer[index + 1] + (int)index + 2;
    if (jump_target < 0 || (size_t)jump_target >= search_length) {
        return false;
    }

    index = (size_t)jump_target;
    for (; index + 4 < search_length; index += 2) {
        if (search_buffer[index] == 0xdb && search_buffer[index + 1] == 0xfa) {
            break;
        }
    }

    if (index + 4 >= search_length) {
        return false;
    }

    module->tracks_offset_offset = ma_read_s16_be(&search_buffer[index + 2]) + (int)index + 2;
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool ma_test_module(MaModule* module, const uint8_t* data, size_t size) {
    size_t probe_size = size < 0x700 ? size : 0x700;
    if (probe_size < 0x14) {
        return false;
    }

    if (!(data[0] == 0x60 && data[1] == 0x00 && data[4] == 0x60 && data[5] == 0x00 && data[8] == 0x60 && data[9] == 0x00
            && data[12] == 0x48 && data[13] == 0xe7)) {
        return false;
    }

    if (!ma_extract_info_from_init_function(module, data, probe_size)) {
        return false;
    }

    if (!ma_extract_info_from_play_function(module, data, probe_size)) {
        return false;
    }

    return module->sub_song_count > 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool ma_append_position(PositionInfo** items, uint16_t* count, uint16_t* capacity, PositionInfo value) {
    if (*count == *capacity) {
        uint16_t new_capacity = (*capacity == 0) ? 16 : (uint16_t)(*capacity * 2);
        PositionInfo* new_items = (PositionInfo*)realloc(*items, (size_t)new_capacity * sizeof(PositionInfo));
        if (new_items == nullptr) {
            return false;
        }

        *items = new_items;
        *capacity = new_capacity;
    }

    (*items)[*count] = value;
    (*count)++;
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool ma_append_u8(uint8_t** items, size_t* count, size_t* capacity, uint8_t value) {
    if (*count == *capacity) {
        size_t new_capacity = (*capacity == 0) ? 32 : (*capacity * 2);
        uint8_t* new_items = (uint8_t*)realloc(*items, new_capacity);
        if (new_items == nullptr) {
            return false;
        }

        *items = new_items;
        *capacity = new_capacity;
    }

    (*items)[*count] = value;
    (*count)++;
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool ma_append_song(MaModule* module, SongInfo value) {
    size_t new_count = module->song_count + 1;
    SongInfo* songs = (SongInfo*)realloc(module->songs, new_count * sizeof(SongInfo));
    if (songs == nullptr) {
        return false;
    }

    module->songs = songs;
    module->songs[module->song_count] = value;
    module->song_count = new_count;
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static const PositionListEntry* ma_find_position_list(const MaModule* module, uint16_t offset) {
    for (size_t i = 0; i < module->position_list_count; i++) {
        if (module->position_lists[i].offset == offset) {
            return &module->position_lists[i];
        }
    }

    return nullptr;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool ma_add_position_list(MaModule* module, uint16_t offset, PositionInfo* positions, uint16_t length) {
    size_t new_count = module->position_list_count + 1;
    PositionListEntry* entries = (PositionListEntry*)realloc(module->position_lists, new_count * sizeof(PositionListEntry));
    if (entries == nullptr) {
        return false;
    }

    module->position_lists = entries;
    module->position_lists[module->position_list_count] = (PositionListEntry){
        .offset = offset,
        .positions = positions,
        .length = length,
    };
    module->position_list_count = new_count;
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool ma_load_subsong_info(MaModule* module, const uint8_t* data, size_t size) {
    if (module->sub_song_count <= 0) {
        ma_set_module_error(module, "subsong count is zero");
        return false;
    }

    size_t speed_off = (size_t)module->sub_song_speed_offset;
    size_t pos_off = (size_t)module->sub_song_position_list_offset;
    size_t sub_song_count = (size_t)module->sub_song_count;

    if (!ma_check_bounds(speed_off, sub_song_count, size) || !ma_check_bounds(pos_off, sub_song_count * 8, size)) {
        ma_set_module_error(module, "subsong tables exceed file size");
        return false;
    }

    for (size_t i = 0; i < sub_song_count; i++) {
        uint16_t voice_offsets[4];
        for (int c = 0; c < 4; c++) {
            voice_offsets[c] = ma_read_u16_be(&data[pos_off + i * 8 + (size_t)c * 2]);
        }

        if ((uint16_t)(voice_offsets[0] + 2) == voice_offsets[1] && (uint16_t)(voice_offsets[1] + 2) == voice_offsets[2]
            && (uint16_t)(voice_offsets[2] + 2) == voice_offsets[3]) {
            continue;
        }

        SongInfo song = { 0 };
        song.start_speed = data[speed_off + i];
        for (int c = 0; c < 4; c++) {
            song.position_lists[c] = voice_offsets[c];
        }

        if (!ma_append_song(module, song)) {
            ma_set_module_error(module, "failed to append subsong");
            return false;
        }
    }

    if (module->song_count == 0) {
        ma_set_module_error(module, "module has no usable subsongs");
        return false;
    }

    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool ma_load_single_position_list(
    MaModule* module,
    PositionInfo** out_positions,
    uint16_t* out_length,
    const uint8_t* data,
    size_t size,
    size_t offset) {
    PositionInfo* list = nullptr;
    uint16_t count = 0;
    uint16_t capacity = 0;

    size_t pos = offset;
    while (true) {
        if (!ma_check_bounds(pos, 2, size)) {
            free(list);
            ma_set_module_error(module, "position list truncated");
            return false;
        }

        PositionInfo info;
        info.track_number = data[pos++];

        uint8_t byt = data[pos++];
        uint16_t val = (uint16_t)(byt << 4);
        byt = (uint8_t)((val & 0xff) >> 1);

        info.transpose = (uint8_t)(val >> 8);
        info.repeat_counter = (int8_t)byt;

        if (!ma_append_position(&list, &count, &capacity, info)) {
            free(list);
            ma_set_module_error(module, "failed to append position");
            return false;
        }

        if (info.track_number == 0xff || info.track_number == 0xfe) {
            break;
        }
    }

    *out_positions = list;
    *out_length = count;
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool ma_load_position_lists(MaModule* module, const uint8_t* data, size_t size) {
    for (size_t s = 0; s < module->song_count; s++) {
        for (int c = 0; c < 4; c++) {
            uint16_t list_offset = module->songs[s].position_lists[c];
            if (ma_find_position_list(module, list_offset) != nullptr) {
                continue;
            }

            size_t offset = (size_t)(module->module_start_offset + list_offset);
            PositionInfo* positions = nullptr;
            uint16_t length = 0;
            if (!ma_load_single_position_list(module, &positions, &length, data, size, offset)) {
                return false;
            }

            if (!ma_add_position_list(module, list_offset, positions, length)) {
                free(positions);
                ma_set_module_error(module, "failed to add position list");
                return false;
            }
        }
    }

    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool ma_load_single_track(
    MaModule* module,
    uint8_t** out_track,
    size_t* out_len,
    const uint8_t* data,
    size_t size,
    size_t offset) {
    uint8_t* bytes = nullptr;
    size_t count = 0;
    size_t capacity = 0;

    size_t pos = offset;
    while (true) {
        if (!ma_check_bounds(pos, 1, size)) {
            free(bytes);
            ma_set_module_error(module, "track truncated");
            return false;
        }

        uint8_t byt = data[pos++];
        if (!ma_append_u8(&bytes, &count, &capacity, byt)) {
            free(bytes);
            ma_set_module_error(module, "failed to append track byte");
            return false;
        }

        if ((byt & 0x80) != 0) {
            if ((byt & 0x40) != 0) {
                if (!ma_check_bounds(pos, 2, size)) {
                    free(bytes);
                    ma_set_module_error(module, "track event truncated");
                    return false;
                }

                uint8_t b1 = data[pos++];
                uint8_t b2 = data[pos++];
                if (!ma_append_u8(&bytes, &count, &capacity, b1) || !ma_append_u8(&bytes, &count, &capacity, b2)) {
                    free(bytes);
                    ma_set_module_error(module, "failed to append track event");
                    return false;
                }

                if ((b2 & 0x80) != 0) {
                    if (!ma_check_bounds(pos, 1, size)) {
                        free(bytes);
                        ma_set_module_error(module, "track extra byte truncated");
                        return false;
                    }

                    uint8_t b3 = data[pos++];
                    if (!ma_append_u8(&bytes, &count, &capacity, b3)) {
                        free(bytes);
                        ma_set_module_error(module, "failed to append track extra byte");
                        return false;
                    }
                }
            }
        } else {
            if (!ma_check_bounds(pos, 1, size)) {
                free(bytes);
                ma_set_module_error(module, "track note info truncated");
                return false;
            }

            uint8_t b1 = data[pos++];
            if (!ma_append_u8(&bytes, &count, &capacity, b1)) {
                free(bytes);
                ma_set_module_error(module, "failed to append track note info");
                return false;
            }

            if ((b1 & 0x80) != 0) {
                if (!ma_check_bounds(pos, 1, size)) {
                    free(bytes);
                    ma_set_module_error(module, "track portamento byte truncated");
                    return false;
                }

                uint8_t b2 = data[pos++];
                if (!ma_append_u8(&bytes, &count, &capacity, b2)) {
                    free(bytes);
                    ma_set_module_error(module, "failed to append track portamento byte");
                    return false;
                }
            }
        }

        if (!ma_check_bounds(pos, 1, size)) {
            free(bytes);
            ma_set_module_error(module, "track sentinel truncated");
            return false;
        }

        uint8_t next_byte = data[pos++];
        if (next_byte == 0xff) {
            if (!ma_append_u8(&bytes, &count, &capacity, next_byte)) {
                free(bytes);
                ma_set_module_error(module, "failed to append track terminator");
                return false;
            }

            break;
        }

        pos--;
    }

    *out_track = bytes;
    *out_len = count;
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool ma_load_tracks(MaModule* module, const uint8_t* data, size_t size) {
    int max_track = -1;

    for (size_t i = 0; i < module->position_list_count; i++) {
        const PositionListEntry* entry = &module->position_lists[i];
        for (uint16_t j = 0; j < entry->length; j++) {
            uint8_t trk = entry->positions[j].track_number;
            if (trk != 0xfe && trk != 0xff && (int)trk > max_track) {
                max_track = (int)trk;
            }
        }
    }

    if (max_track < 0) {
        ma_set_module_error(module, "no tracks found");
        return false;
    }

    module->track_count = (size_t)(max_track + 1);
    module->tracks = (Track*)calloc(module->track_count, sizeof(Track));
    if (module->tracks == nullptr) {
        ma_set_module_error(module, "failed to allocate tracks");
        return false;
    }

    if (!ma_check_bounds((size_t)module->tracks_offset_offset, 4, size)) {
        ma_set_module_error(module, "tracks offset pointer truncated");
        return false;
    }

    int tracks_start_offset = ma_read_i32_be(&data[module->tracks_offset_offset]) + module->module_start_offset;
    if (tracks_start_offset < 0) {
        ma_set_module_error(module, "negative tracks start offset");
        return false;
    }

    size_t track_table_offset = (size_t)module->module_start_offset;
    if (!ma_check_bounds(track_table_offset, module->track_count * 2, size)) {
        ma_set_module_error(module, "track offset table truncated");
        return false;
    }

    for (size_t i = 0; i < module->track_count; i++) {
        uint16_t rel_off = ma_read_u16_be(&data[track_table_offset + i * 2]);
        size_t abs_off = (size_t)tracks_start_offset + rel_off;

        uint8_t* track_data = nullptr;
        size_t track_len = 0;
        if (!ma_load_single_track(module, &track_data, &track_len, data, size, abs_off)) {
            return false;
        }

        module->tracks[i].data = track_data;
        module->tracks[i].length = track_len;
    }

    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool ma_load_instruments(MaModule* module, const uint8_t* data, size_t size) {
    if (!ma_check_bounds((size_t)module->instrument_info_offset_offset, 4, size)
        || !ma_check_bounds((size_t)module->sample_info_offset_offset, 4, size)) {
        ma_set_module_error(module, "instrument/sample table pointers truncated");
        return false;
    }

    int instruments_start = ma_read_i32_be(&data[module->instrument_info_offset_offset]);
    int sample_start = ma_read_i32_be(&data[module->sample_info_offset_offset]);

    if (sample_start < instruments_start || instruments_start < 0) {
        ma_set_module_error(module, "invalid instrument/sample layout");
        return false;
    }

    int instrument_count = (sample_start - instruments_start) / 16;
    if (instrument_count <= 0) {
        ma_set_module_error(module, "no instruments found");
        return false;
    }

    module->instrument_count = (size_t)instrument_count;
    module->instruments = (Instrument*)calloc(module->instrument_count, sizeof(Instrument));
    if (module->instruments == nullptr) {
        ma_set_module_error(module, "failed to allocate instruments");
        return false;
    }

    size_t off = (size_t)(module->module_start_offset + instruments_start);
    size_t need = module->instrument_count * 16;
    if (!ma_check_bounds(off, need, size)) {
        ma_set_module_error(module, "instrument table truncated");
        return false;
    }

    for (size_t i = 0; i < module->instrument_count; i++) {
        Instrument* instr = &module->instruments[i];
        instr->sample_number = data[off + 0];
        instr->attack = data[off + 1];
        instr->decay_sustain = data[off + 2];
        instr->vibrato_delay = data[off + 3];
        instr->release = data[off + 4];
        instr->vibrato_speed = data[off + 5];
        instr->vibrato_level = data[off + 6];
        instr->arpeggio = data[off + 7];
        instr->fx_arp_spdlp = data[off + 8];
        instr->hold = data[off + 9];
        instr->key_wave_rate = data[off + 10];
        instr->wave_level_speed = data[off + 11];
        off += 16;
    }

    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void ma_trim_sample_name(char* dst, const uint8_t* src16) {
    for (int i = 0; i < 16; i++) {
        unsigned char ch = src16[i];
        dst[i] = (char)ch;
    }
    dst[16] = '\0';

    for (int i = 15; i >= 0; i--) {
        if (dst[i] == '\0' || isspace((unsigned char)dst[i])) {
            dst[i] = '\0';
            continue;
        }

        break;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool ma_load_samples(MaModule* module, const uint8_t* data, size_t size) {
    if (!ma_check_bounds((size_t)module->sample_info_offset_offset, 4, size)) {
        ma_set_module_error(module, "sample table pointer truncated");
        return false;
    }

    int sample_start = ma_read_i32_be(&data[module->sample_info_offset_offset]);
    if (sample_start < 0) {
        ma_set_module_error(module, "invalid sample start");
        return false;
    }

    sample_start += module->module_start_offset;
    if ((size_t)sample_start >= size || module->position_list_count == 0) {
        ma_set_module_error(module, "invalid sample table placement");
        return false;
    }

    uint16_t min_pos_offset = module->position_lists[0].offset;
    for (size_t i = 1; i < module->position_list_count; i++) {
        if (module->position_lists[i].offset < min_pos_offset) {
            min_pos_offset = module->position_lists[i].offset;
        }
    }

    int number_of_samples = (int)(((int)min_pos_offset + module->module_start_offset - sample_start) / 24);
    if (number_of_samples <= 0) {
        ma_set_module_error(module, "no samples found");
        return false;
    }

    module->sample_count = (size_t)number_of_samples;
    module->samples = (Sample*)calloc(module->sample_count, sizeof(Sample));
    int* sample_offsets = (int*)malloc(module->sample_count * sizeof(int));
    if (module->samples == nullptr || sample_offsets == nullptr) {
        free(sample_offsets);
        ma_set_module_error(module, "failed to allocate sample tables");
        return false;
    }

    size_t off = (size_t)sample_start;
    size_t info_need = module->sample_count * 24;
    if (!ma_check_bounds(off, info_need, size)) {
        free(sample_offsets);
        ma_set_module_error(module, "sample info table truncated");
        return false;
    }

    for (size_t i = 0; i < module->sample_count; i++) {
        int rel_offset = ma_read_i32_be(&data[off + 0]);
        sample_offsets[i] = (rel_offset < 0) ? -1 : (sample_start + rel_offset);

        module->samples[i].length = ma_read_u16_be(&data[off + 4]);
        module->samples[i].loop_length = ma_read_u16_be(&data[off + 6]);
        ma_trim_sample_name(module->samples[i].name, &data[off + 8]);

        off += 24;
    }

    for (size_t i = 0; i < module->sample_count; i++) {
        Sample* sample = &module->samples[i];
        if (sample_offsets[i] < 0) {
            sample->sample_data = s_empty_sample;
            sample->length = 1;
            continue;
        }

        size_t sample_offset = (size_t)sample_offsets[i];
        size_t sample_len = (size_t)sample->length * 2;
        if (!ma_check_bounds(sample_offset, sample_len, size)) {
            free(sample_offsets);
            ma_set_module_error(module, "sample data truncated");
            return false;
        }

        void* dst = malloc(sample_len);
        if (dst == nullptr) {
            free(sample_offsets);
            ma_set_module_error(module, "failed to allocate sample data");
            return false;
        }

        memcpy(dst, &data[sample_offset], sample_len);
        sample->sample_data = (const int8_t*)dst;
    }

    free(sample_offsets);
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool ma_module_parse(MaModule* module, const uint8_t* data, size_t size) {
    if (!ma_test_module(module, data, size)) {
        ma_set_module_error(module, "not a recognized Music Assembler module");
        return false;
    }

    if (!ma_load_subsong_info(module, data, size)) {
        return false;
    }

    if (!ma_load_position_lists(module, data, size)) {
        return false;
    }

    if (!ma_load_tracks(module, data, size)) {
        return false;
    }

    if (!ma_load_instruments(module, data, size)) {
        return false;
    }

    if (!ma_load_samples(module, data, size)) {
        return false;
    }

    module->error_message[0] = '\0';
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void ma_module_cleanup(MaModule* module) {
    if (module == nullptr) {
        return;
    }

    free(module->songs);
    module->songs = nullptr;
    module->song_count = 0;

    for (size_t i = 0; i < module->position_list_count; i++) {
        free(module->position_lists[i].positions);
    }
    free(module->position_lists);
    module->position_lists = nullptr;
    module->position_list_count = 0;

    for (size_t i = 0; i < module->track_count; i++) {
        free(module->tracks[i].data);
    }
    free(module->tracks);
    module->tracks = nullptr;
    module->track_count = 0;

    free(module->instruments);
    module->instruments = nullptr;
    module->instrument_count = 0;

    for (size_t i = 0; i < module->sample_count; i++) {
        if (module->samples[i].sample_data != s_empty_sample) {
            free((void*)module->samples[i].sample_data);
        }
    }
    free(module->samples);
    module->samples = nullptr;
    module->sample_count = 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Player
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static const PositionInfo* ma_player_get_position_list(const MaModule* module, uint16_t offset, uint16_t* length) {
    const PositionListEntry* entry = ma_find_position_list(module, offset);
    if (entry == nullptr) {
        *length = 0;
        return nullptr;
    }

    *length = entry->length;
    return entry->positions;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void ma_initialize_voice(VoiceInfo* voice) {
    voice->decrease_volume = false;
    voice->arpeggio_value_to_use = 0;
    voice->volume = 0;
    voice->vibrato_direction = 0;
    voice->vibrato_speed_counter = 0;
    voice->vibrato_add_value = 0;
    voice->flag = VoiceFlag_None;

    const Instrument* instr = voice->current_instrument;
    voice->vibrato_delay_counter = instr->vibrato_delay;
    voice->arpeggio_counter = instr->fx_arp_spdlp;
    voice->sustain_counter = instr->hold;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void ma_force_retrigger(VoiceInfo* voice) {
    voice->flag &= VoiceFlag_Release;
    voice->flag |= VoiceFlag_Retrig;
    voice->muted = true;
    voice->active = false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void ma_do_wave_length_modifying(const Instrument* instr, const Sample* sample, VoiceInfo* voice) {
    int8_t wave_length = (int8_t)(instr->wave_level_speed >> 4);
    uint8_t wave_speed = (uint8_t)(instr->wave_level_speed & 0x0f);

    if (wave_speed != 0) {
        wave_speed *= 2;
        voice->wave_speed_counter++;

        if (wave_speed < voice->wave_speed_counter) {
            voice->wave_speed_counter = 0;
            voice->wave_direction = !voice->wave_direction;
        }

        if (voice->wave_direction) {
            wave_length = (int8_t)-wave_length;
        }
    }

    voice->wave_length_modifier = (uint16_t)(voice->wave_length_modifier + wave_length);

    uint16_t length = sample->length;
    uint32_t start_offset = ((voice->wave_length_modifier / 4u) + instr->key_wave_rate) & ((uint32_t)length - 1u);

    voice->sample_data = sample->sample_data;
    voice->sample_start_offset = start_offset;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint16_t ma_do_arpeggio(const Instrument* instr, VoiceInfo* voice) {
    uint8_t arp = instr->arpeggio;
    int8_t arp_to_use = (int8_t)(voice->arpeggio_value_to_use - 2);

    if (arp != 0) {
        if (arp_to_use == 0) {
            arp >>= 4;
        } else if (arp_to_use < 0) {
            arp = 0;
        } else {
            arp &= 0x0f;
        }
    }

    if (instr->fx_arp_spdlp != 0) {
        voice->arpeggio_counter += 0x10;

        if (voice->arpeggio_counter >= instr->fx_arp_spdlp) {
            voice->arpeggio_counter = 0;

            uint8_t spd = (uint8_t)(instr->fx_arp_spdlp & 0x03);
            if (spd == 0) {
                voice->decrease_volume = !voice->decrease_volume;
                spd = 1;
            }

            arp_to_use += 3;
            if (arp_to_use >= 4) {
                arp_to_use = (int8_t)spd;
            }

            voice->arpeggio_value_to_use = (uint8_t)arp_to_use;
        }
    }

    uint8_t note = (uint8_t)(voice->current_note + arp);
    if ((voice->flag & VoiceFlag_Synthesis) == 0) {
        note = (uint8_t)(note + instr->key_wave_rate);
    }

    if (note >= 48) {
        note = 47;
    }

    return s_periods[note];
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint16_t ma_do_portamento(uint16_t period, VoiceInfo* voice) {
    if (voice->portamento_or_vibrato_value != 0) {
        bool flag_set = (voice->flag & VoiceFlag_Portamento) != 0;
        voice->flag ^= VoiceFlag_Portamento;

        if (!flag_set || ((voice->portamento_or_vibrato_value & 0x01) == 0)) {
            period <<= 1;
            period = (uint16_t)(period + voice->portamento_add_value);
            period >>= 1;

            if ((voice->portamento_or_vibrato_value & 0x80) != 0) {
                voice->portamento_add_value
                    = (int16_t)(voice->portamento_add_value + (((-(int8_t)voice->portamento_or_vibrato_value + 1) >> 1)));
            } else {
                voice->portamento_add_value = (int16_t)(voice->portamento_add_value - ((voice->portamento_or_vibrato_value + 1) >> 1));
            }
        }
    }

    return period;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint16_t ma_do_vibrato(uint16_t period, const Instrument* instr, VoiceInfo* voice) {
    if (voice->vibrato_delay_counter == 0) {
        if (voice->portamento_or_vibrato_value == 0 && instr->vibrato_level != 0) {
            int8_t level = (int8_t)instr->vibrato_level;
            uint8_t direction = (uint8_t)(voice->vibrato_direction & 0x03);
            if (direction != 0 && direction != 3) {
                level = (int8_t)-level;
            }

            voice->vibrato_add_value = (int16_t)(voice->vibrato_add_value + level);
            voice->vibrato_speed_counter++;

            if (voice->vibrato_speed_counter == instr->vibrato_speed) {
                voice->vibrato_speed_counter = 0;
                voice->vibrato_direction++;
            }

            period <<= 1;
            period = (uint16_t)(period + voice->vibrato_add_value);
            period >>= 1;
        }
    } else {
        voice->vibrato_delay_counter--;
    }

    return period;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void ma_do_adsr(const Instrument* instr, VoiceInfo* voice) {
    if ((voice->flag & VoiceFlag_Release) != 0) {
        if (voice->sustain_counter == 0) {
            int new_volume = voice->volume - instr->release;
            if (new_volume < 0) {
                new_volume = 0;
            }

            voice->volume = (uint8_t)new_volume;
        } else {
            voice->sustain_counter--;
        }
    } else {
        int new_volume = voice->volume + instr->attack;
        int decay_sustain = (instr->decay_sustain & 0xf0) | 0x0f;

        if (new_volume >= decay_sustain) {
            voice->flag |= VoiceFlag_Release;
            new_volume = instr->decay_sustain << 4;
        }

        voice->volume = (uint8_t)new_volume;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void ma_commit_pending_sample(const Instrument* instr, VoiceInfo* voice) {
    uint32_t sample_len_bytes = (uint32_t)voice->sample_length * 2u;

    voice->pending_data = voice->sample_data;
    voice->current_sample_number = instr->sample_number;
    voice->pending_start_offset = voice->sample_start_offset;
    voice->pending_length_words = voice->sample_length;
    uint32_t loop_len_bytes = sample_len_bytes;
    if ((voice->sample_data != s_empty_sample) && (loop_len_bytes > 2u)) {
        voice->pending_loop_start_offset = voice->sample_start_offset;
        voice->pending_loop_length_bytes = loop_len_bytes;
    } else {
        voice->pending_loop_start_offset = 0;
        voice->pending_loop_length_bytes = 0;
    }
    voice->pending_delay_ticks = 0;
    voice->pending_unmuted_this_tick = false;
    voice->has_pending_sample = true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void ma_apply_pending_sample(VoiceInfo* voice) {
    if (!voice->has_pending_sample) {
        return;
    }

    voice->play_data = voice->pending_data;
    voice->play_start_offset = voice->pending_start_offset;
    voice->play_length_words = voice->pending_length_words;
    voice->play_loop_start_offset = voice->pending_loop_start_offset;
    voice->play_loop_length_bytes = voice->pending_loop_length_bytes;
    voice->sample_pos_fp = 0;
    voice->has_pending_sample = false;
    voice->muted = false;
    voice->active = true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void ma_setup_sample(const MaModule* module, const Instrument* instr, VoiceInfo* voice) {
    if (instr->sample_number >= module->sample_count) {
        return;
    }

    const Sample* sample = &module->samples[instr->sample_number];

    if ((voice->flag & VoiceFlag_SetLoop) != 0) {
        voice->flag &= (uint8_t)~VoiceFlag_SetLoop;

        if (sample->loop_length != 0) {
            voice->sample_length = sample->loop_length;

            if (sample->length <= 128) {
                ma_do_wave_length_modifying(instr, sample, voice);
            } else {
                voice->sample_start_offset = (uint32_t)((sample->length - sample->loop_length) * 2);
                voice->sample_data = sample->sample_data;
            }

            ma_commit_pending_sample(instr, voice);
            return;
        }

        voice->sample_data = s_empty_sample;
        voice->sample_start_offset = 0;
        voice->sample_length = 2;
        voice->flag &= (uint8_t)~VoiceFlag_Synthesis;
        ma_commit_pending_sample(instr, voice);
        return;
    }

    if ((voice->flag & VoiceFlag_Retrig) != 0) {
        voice->flag &= VoiceFlag_Release;
        voice->flag |= VoiceFlag_SetLoop;

        const int8_t* sample_data = sample->sample_data;
        uint32_t start_offset = 0;

        uint16_t sample_length = sample->length;
        uint16_t loop_length = sample->loop_length;
        uint8_t key_wave_rate = instr->key_wave_rate;

        if (sample_length <= 128 && sample_data != s_empty_sample) {
            voice->flag |= VoiceFlag_Synthesis;
            start_offset = key_wave_rate;
            sample_length = loop_length;
            if (sample_length == 0) {
                sample_length = 1;
            }
        }

        voice->sample_data = sample_data;
        voice->sample_start_offset = start_offset;
        voice->sample_length = sample_length;
        ma_commit_pending_sample(instr, voice);
        return;
    }

    if ((voice->flag & VoiceFlag_Synthesis) != 0) {
        ma_do_wave_length_modifying(instr, sample, voice);
        ma_commit_pending_sample(instr, voice);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void ma_do_voice(MaPlayer* player, VoiceInfo* voice) {
    const Instrument* instr = voice->current_instrument;
    ma_setup_sample(player->module, instr, voice);

    uint16_t period = ma_do_arpeggio(instr, voice);
    period = ma_do_portamento(period, voice);
    period = ma_do_vibrato(period, instr, voice);

    ma_do_adsr(instr, voice);

    uint16_t volume = (uint16_t)((voice->volume * player->playing_info.master_volume) / 256);
    if (voice->vibrato_delay_counter == 0 && voice->decrease_volume) {
        volume /= 4;
    }

    if (period < 127) {
        period = 127;
    }

    voice->current_period = period;
    voice->current_volume = volume;
    uint32_t frequency = 3546895u / (uint32_t)period;
    voice->sample_step_fp = ((uint64_t)frequency << SAMPLE_FRAC_BITS) / (uint64_t)player->config.sample_rate;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool ma_get_next_row_in_track(MaPlayer* player, VoiceInfo* voice) {
    voice->row_delay_counter--;
    if (voice->row_delay_counter >= 0) {
        return true;
    }

    if (voice->current_position >= voice->position_list_length) {
        player->ended = true;
        return true;
    }

    const PositionInfo* position_info = &voice->position_list[voice->current_position];
    if (position_info->track_number == 0xfe) {
        return true;
    }

    if (position_info->track_number >= player->module->track_count) {
        player->ended = true;
        return true;
    }

    const Track* track = &player->module->tracks[position_info->track_number];
    uint16_t track_row = voice->current_track_row;
    if (track_row >= track->length) {
        player->ended = true;
        return true;
    }

    uint8_t track_byte = track->data[track_row++];

    if ((track_byte & 0x80) == 0) {
        if ((track_byte & 0x40) == 0) {
            ma_initialize_voice(voice);
            ma_force_retrigger(voice);
        }

        voice->portamento_add_value = 0;
        voice->current_note = (uint8_t)((track_byte & 0x3f) + voice->transpose);

        if (track_row >= track->length) {
            player->ended = true;
            return true;
        }

        track_byte = track->data[track_row++];
        if ((track_byte & 0x80) == 0) {
            voice->portamento_or_vibrato_value = 0;
        } else {
            track_byte &= 0x7f;
            if (track_row >= track->length) {
                player->ended = true;
                return true;
            }
            voice->portamento_or_vibrato_value = track->data[track_row++];
        }
    } else {
        if ((track_byte & 0x40) == 0) {
            track_byte &= 0x3f;
            voice->flag |= VoiceFlag_Release;
            voice->sustain_counter = 0;
        } else {
            uint8_t instrument_index = (uint8_t)(track_byte & 0x3f);
            if (instrument_index < player->module->instrument_count) {
                voice->current_instrument = &player->module->instruments[instrument_index];
            }

            if (track_row >= track->length) {
                player->ended = true;
                return true;
            }

            track_byte = track->data[track_row++];
            if ((track_byte & 0x40) == 0) {
                voice->wave_length_modifier = 0;
                voice->wave_direction = false;
                voice->wave_speed_counter = 1;
                ma_initialize_voice(voice);
            }

            ma_force_retrigger(voice);

            voice->portamento_add_value = 0;
            voice->current_note = (uint8_t)((track_byte & 0x3f) + voice->transpose);

            if (track_row >= track->length) {
                player->ended = true;
                return true;
            }

            track_byte = track->data[track_row++];
            if ((track_byte & 0x80) == 0) {
                voice->portamento_or_vibrato_value = 0;
            } else {
                track_byte &= 0x7f;
                if (track_row >= track->length) {
                    player->ended = true;
                    return true;
                }
                voice->portamento_or_vibrato_value = track->data[track_row++];
            }
        }
    }

    voice->row_delay_counter = (int8_t)track_byte;

    if (track_row < track->length && track->data[track_row] == 0xff) {
        voice->track_repeat_counter -= 8;
        if (voice->track_repeat_counter < 0) {
            voice->current_position++;
            if (voice->current_position >= voice->position_list_length) {
                voice->current_position = 0;
            }

            position_info = &voice->position_list[voice->current_position];
            if (position_info->track_number == 0xff) {
                voice->current_position = 0;
                position_info = &voice->position_list[0];
            }

            voice->transpose = position_info->transpose;
            voice->track_repeat_counter = position_info->repeat_counter;
        }

        track_row = 0;
    }

    voice->current_track_row = track_row;
    return false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool ma_voice_prepare_current(VoiceInfo* voice) {
    if (voice->muted || !voice->active || voice->play_data == nullptr || voice->play_length_words == 0) {
        return false;
    }
    if (voice->sample_step_fp == 0) {
        voice->active = false;
        return false;
    }

    uint32_t sample_len_bytes = (uint32_t)voice->play_length_words * 2u;
    uint64_t max_pos_fp = ((uint64_t)sample_len_bytes << SAMPLE_FRAC_BITS) - 1u;
    while (voice->sample_pos_fp > max_pos_fp) {
        if (voice->has_pending_sample) {
            ma_apply_pending_sample(voice);

            if (voice->muted || !voice->active || voice->play_data == nullptr || voice->play_length_words == 0) {
                return false;
            }

            sample_len_bytes = (uint32_t)voice->play_length_words * 2u;
            if (sample_len_bytes == 0) {
                return false;
            }

            max_pos_fp = ((uint64_t)sample_len_bytes << SAMPLE_FRAC_BITS) - 1u;
            continue;
        }

        if (voice->play_loop_length_bytes > 0) {
            uint64_t loop_fp = (uint64_t)voice->play_loop_length_bytes << SAMPLE_FRAC_BITS;
            uint64_t overflow = voice->sample_pos_fp - max_pos_fp;
            voice->sample_pos_fp = (loop_fp != 0) ? (overflow % loop_fp) : 0;
        } else {
            voice->sample_pos_fp = 0;
            voice->active = false;
            return false;
        }
    }

    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void ma_apply_filter_frame(MaPlayer* player, float* left, float* right) {
    const int scale = 24;
    if (!player->filter_enabled) {
        return;
    }

    int64_t in[2] = { (int64_t)(*left * 2147483647.0f), (int64_t)(*right * 2147483647.0f) };
    int64_t out[2] = { 0, 0 };

    for (int ch = 0; ch < 2; ch++) {
        out[ch] = player->filter_b0 * in[ch] + player->filter_b1 * player->filter_x1[ch] + player->filter_b2 * player->filter_x2[ch]
            - player->filter_a1 * player->filter_y1[ch] - player->filter_a2 * player->filter_y2[ch];
        out[ch] >>= scale;

        player->filter_x2[ch] = player->filter_x1[ch];
        player->filter_x1[ch] = in[ch];
        player->filter_y2[ch] = player->filter_y1[ch];
        player->filter_y1[ch] = out[ch];

        if (out[ch] < INT32_MIN) {
            out[ch] = INT32_MIN;
        } else if (out[ch] > INT32_MAX) {
            out[ch] = INT32_MAX;
        }
    }

    *left = (float)((double)out[0] / 2147483647.0);
    *right = (float)((double)out[1] / 2147483647.0);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static float ma_voice_sample(const VoiceInfo* voice) {
    if (voice->muted || !voice->active || voice->play_data == nullptr || voice->play_length_words == 0) {
        return 0.0f;
    }

    uint32_t sample_len_bytes = (uint32_t)voice->play_length_words * 2u;
    uint64_t index = (uint64_t)voice->play_start_offset + (voice->sample_pos_fp >> SAMPLE_FRAC_BITS);

    if (sample_len_bytes == 0) {
        return 0.0f;
    }

    if (index >= (uint64_t)voice->play_start_offset + sample_len_bytes) {
        if (voice->play_loop_length_bytes == 0) {
            return 0.0f;
        }

        uint64_t rel = (index - voice->play_loop_start_offset) % voice->play_loop_length_bytes;
        index = (uint64_t)voice->play_loop_start_offset + rel;
    }

    int8_t s = voice->play_data[(size_t)index];
    return (float)s / 128.0f;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void ma_voice_advance(VoiceInfo* voice) {
    voice->sample_pos_fp += voice->sample_step_fp;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void ma_player_tick(MaPlayer* player) {
    for (int i = 0; i < 4; i++) {
        player->voices[i].active_before_tick = player->voices[i].active;
        player->voices[i].muted_before_tick = player->voices[i].muted;
        player->voices[i].pending_unmuted_this_tick = false;
    }

    player->playing_info.speed_counter--;

    if (player->playing_info.speed_counter == 0) {
        player->playing_info.speed_counter = player->playing_info.speed;

        for (int i = 0; i < 4; i++) {
            if (ma_get_next_row_in_track(player, &player->voices[i])) {
                ma_do_voice(player, &player->voices[i]);
            }
        }
    } else {
        for (int i = 0; i < 4; i++) {
            ma_do_voice(player, &player->voices[i]);
        }
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void ma_player_init_state(MaPlayer* player, int subsong) {
    const SongInfo* song = &player->module->songs[subsong];

    player->current_subsong = subsong;
    player->ended = false;
    for (int i = 0; i < 4; i++) {
        player->channel_end[i] = false;
    }

    player->playing_info = (GlobalPlayingInfo){
        .master_volume = 64,
        .speed = song->start_speed,
        .speed_counter = 1,
    };

    player->frames_per_tick = (player->config.sample_rate == 0) ? 0.0f : ((float)player->config.sample_rate / 50.0f);
    player->frames_until_tick = 0.0f;
    memset(player->filter_x1, 0, sizeof(player->filter_x1));
    memset(player->filter_x2, 0, sizeof(player->filter_x2));
    memset(player->filter_y1, 0, sizeof(player->filter_y1));
    memset(player->filter_y2, 0, sizeof(player->filter_y2));

    for (int i = 0; i < 4; i++) {
        uint16_t pos_len = 0;
        const PositionInfo* pos_list = ma_player_get_position_list(player->module, song->position_lists[s_channel_map[i]], &pos_len);

        VoiceInfo* voice = &player->voices[i];
        memset(voice, 0, sizeof(*voice));

        voice->channel_number = i;
        voice->position_list = pos_list;
        voice->position_list_length = pos_len;
        voice->track_repeat_counter = (pos_len > 0) ? pos_list[0].repeat_counter : 0;
        voice->current_instrument = (player->module->instrument_count > 0) ? &player->module->instruments[0] : nullptr;
        voice->transpose = (pos_len > 0) ? pos_list[0].transpose : 0;
        voice->play_data = s_empty_sample;
        voice->play_length_words = 2;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void ma_player_reset(MaPlayer* player) {
    ma_player_init_state(player, player->current_subsong);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static size_t ma_player_render(MaPlayer* player, float* interleaved_stereo, size_t frames) {
    if (player == nullptr || interleaved_stereo == nullptr) {
        return 0;
    }

    for (size_t frame = 0; frame < frames; frame++) {
        if (player->frames_until_tick <= 0.0f) {
            ma_player_tick(player);
            player->frames_until_tick += player->frames_per_tick;
        }

        float left = 0.0f;
        float right = 0.0f;

        for (int ch = 0; ch < 4; ch++) {
            if ((player->config.channel_enable_mask & (1U << ch)) == 0) {
                continue;
            }

            VoiceInfo* voice = &player->voices[ch];
            bool just_unmuted_while_inactive = voice->muted_before_tick && !voice->muted && !voice->active;
            if (voice->has_pending_sample && !just_unmuted_while_inactive
                && !voice->active_before_tick && (voice->muted || !voice->active)) {
                ma_apply_pending_sample(voice);
            }
            if (!ma_voice_prepare_current(voice)) {
                continue;
            }
            float sample = ma_voice_sample(voice);
            float volume = ((float)voice->current_volume / 64.0f) * player->config.master_gain;
            float out = sample * volume;

            int pan = (ch == 0 || ch == 3) ? 0 : 256;
            float left_gain = (float)(256 - pan) / 256.0f;
            float right_gain = (float)pan / 256.0f;

            left += out * left_gain;
            right += out * right_gain;

            ma_voice_advance(voice);
        }
        ma_apply_filter_frame(player, &left, &right);
        if (left > 1.0f) {
            left = 1.0f;
        } else if (left < -1.0f) {
            left = -1.0f;
        }
        if (right > 1.0f) {
            right = 1.0f;
        } else if (right < -1.0f) {
            right = -1.0f;
        }

        interleaved_stereo[frame * 2 + 0] = left;
        interleaved_stereo[frame * 2 + 1] = right;

        player->frames_until_tick -= 1.0f;

        if (player->ended && player->config.loop_mode == MaLoopMode_PlayOnce) {
            for (size_t rest = frame + 1; rest < frames; rest++) {
                interleaved_stereo[rest * 2 + 0] = 0.0f;
                interleaved_stereo[rest * 2 + 1] = 0.0f;
            }
            return frames;
        }

        if (player->ended && player->config.loop_mode == MaLoopMode_Loop) {
            ma_player_reset(player);
        }
    }

    return frames;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static MaPlayer* ma_player_create(const MaModule* module, const MaRenderConfig* config) {
    if (module == nullptr || config == nullptr) {
        return nullptr;
    }

    if (config->sample_rate == 0 || module->song_count == 0) {
        return nullptr;
    }

    MaPlayer* player = (MaPlayer*)calloc(1, sizeof(MaPlayer));
    if (player == nullptr) {
        return nullptr;
    }

    player->module = module;
    player->config = *config;
    if (player->config.master_gain == 0.0f) {
        player->config.master_gain = 1.0f;
    }
    if ((player->config.channel_enable_mask & 0x0fU) == 0) {
        player->config.channel_enable_mask = 0x0fU;
    }

    double cutoff_hz = 3275.0;
    int scale = 24;
    double scale_factor = (double)(1u << scale);
    double sample_rate = (double)player->config.sample_rate;
    if (sample_rate > 1.0) {
        double a0_coef = 1.0;
        double a1_coef = sqrt(2.0);
        double a2_coef = 1.0;
        double b0_coef = 1.0;

        double omega = 2.0 * M_PI * cutoff_hz / sample_rate;
        double tan_omega = tan(omega * 0.5);
        double norm = 1.0 / (a0_coef + a1_coef * tan_omega + a2_coef * tan_omega * tan_omega);

        player->filter_b0 = (int64_t)(b0_coef * tan_omega * tan_omega * norm * scale_factor);
        player->filter_b1 = 2 * player->filter_b0;
        player->filter_b2 = player->filter_b0;
        player->filter_a1 = (int64_t)(2.0 * (a2_coef * tan_omega * tan_omega - a0_coef) * norm * scale_factor);
        player->filter_a2 = (int64_t)((a0_coef - a1_coef * tan_omega + a2_coef * tan_omega * tan_omega) * norm * scale_factor);
        player->filter_enabled = true;
    }

    ma_player_init_state(player, 0);

    return player;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void ma_player_destroy(MaPlayer* player) {
    free(player);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Public API
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

MaModule* ma_create(const uint8_t* data, size_t size, float sample_rate) {
    if (data == nullptr || size == 0 || sample_rate <= 0.0f) {
        return nullptr;
    }

    MaModule* module = (MaModule*)calloc(1, sizeof(MaModule));
    if (module == nullptr) {
        return nullptr;
    }

    if (!ma_module_parse(module, data, size)) {
        ma_module_cleanup(module);
        free(module);
        return nullptr;
    }

    MaRenderConfig cfg = {
        .sample_rate = (uint32_t)sample_rate,
        .master_gain = 1.0f,
        .loop_mode = MaLoopMode_PlayOnce,
        .channel_enable_mask = 0x0f,
    };

    module->player = ma_player_create(module, &cfg);
    if (module->player == nullptr) {
        ma_module_cleanup(module);
        free(module);
        return nullptr;
    }

    return module;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ma_destroy(MaModule* module) {
    if (module == nullptr) {
        return;
    }

    ma_player_destroy(module->player);
    ma_module_cleanup(module);
    free(module);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int ma_subsong_count(const MaModule* module) {
    if (module == nullptr) {
        return 0;
    }

    return (int)module->song_count;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool ma_select_subsong(MaModule* module, int subsong) {
    if (module == nullptr || module->player == nullptr) {
        return false;
    }

    if (subsong < 0 || subsong >= (int)module->song_count) {
        return false;
    }

    ma_player_init_state(module->player, subsong);
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

size_t ma_render(MaModule* module, float* interleaved_stereo, size_t frames) {
    if (module == nullptr || module->player == nullptr) {
        return 0;
    }

    return ma_player_render(module->player, interleaved_stereo, frames);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool ma_has_ended(const MaModule* module) {
    if (module == nullptr || module->player == nullptr) {
        return true;
    }

    return module->player->ended;
}
