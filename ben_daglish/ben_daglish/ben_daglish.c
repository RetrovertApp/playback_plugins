// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Daniel Collin
// Ported from NostalgicPlayer C# implementation by Thomas Neumann (MIT)
#include "ben_daglish.h"

#include <stdio.h>
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

#define FINE_TUNE_START_INDEX (4 * 12)

static const int s_fine_tune[] = {
    0x1000,   0x10F3,   0x11F5,   0x1306,   0x1429,   0x155B,   0x16A0,   0x17F9,   0x1965,   0x1AE9,   0x1C82,   0x1E34,
    0x2000,   0x21E7,   0x23EB,   0x260D,   0x2851,   0x2AB7,   0x2D41,   0x2FF2,   0x32CC,   0x35D1,   0x3904,   0x3C68,
    0x4000,   0x43CE,   0x47D6,   0x4C1B,   0x50A2,   0x556E,   0x5A82,   0x5FE4,   0x6597,   0x6BA2,   0x7208,   0x78D0,
    0x8000,   0x879C,   0x8FAC,   0x9837,   0xA145,   0xAADC,   0xB504,   0xBFC8,   0xCB2F,   0xD744,   0xE411,   0xF1A1,

    0x10000,  0x10F38,  0x11F59,  0x1306F,  0x1428A,  0x155B8,  0x16A09,  0x17F91,  0x1965F,  0x1AE89,  0x1C823,  0x1E343,
    0x20000,  0x21E71,  0x23EB3,  0x260DF,  0x28514,  0x2AB70,  0x2D413,  0x2FF22,  0x32CBF,  0x35D13,  0x39047,  0x3C686,
    0x40000,  0x43CE3,  0x47D66,  0x4C1BF,  0x50A28,  0x556E0,  0x5A827,  0x5FE44,  0x6597F,  0x6BA27,  0x7208F,  0x78D0D,
    0x80000,  0x879C7,  0x8FACD,  0x9837F,  0xA1451,  0xAADC0,  0xB504F,  0xBFC88,  0xCB2FF,  0xD7450,  0xE411F,  0xF1A1B,
    0x100000, 0x10F38F, 0x11F59A, 0x1307B2, 0x1428A2, 0x155B81, 0x16A09E, 0x17F910, 0x1965FE, 0x1AE8A0, 0x1C823E, 0x1E3438,
    0x200000, 0x21E71F, 0x23EB35, 0x260DFC, 0x285145, 0x2AB702, 0x2D413C, 0x2FF221, 0x32CBFD, 0x35D13F, 0x39047C, 0x3C6870,
};

#define FINE_TUNE_COUNT (sizeof(s_fine_tune) / sizeof(s_fine_tune[0]))

static const int8_t s_empty_sample[] = { 0, 0, 0, 0 };

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Types
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct BdFeatures {
    int master_volume_fade_version;  // -1 = None
    bool set_dma_in_sample_handlers;
    bool enable_counter;
    bool enable_sample_effects;
    bool enable_final_volume_slide;
    bool enable_volume_fade;
    bool enable_portamento;
    bool check_for_ticks;
    bool extra_tick_arg;
    bool uses_9x_track_effects;
    bool uses_cx_track_effects;

    uint8_t max_track_value;
    bool enable_c0_track_loop;
    bool enable_f0_track_loop;

    uint8_t max_sample_mapping_value;
    int get_sample_mapping_version;
    int set_sample_mapping_version;
} BdFeatures;

typedef struct BdSample {
    int16_t sample_number;
    int8_t* sample_data;
    uint16_t length;
    uint32_t loop_offset;
    uint16_t loop_length;

    uint16_t volume;
    int16_t volume_fade_speed;

    int16_t portamento_duration;
    int16_t portamento_add_value;

    uint16_t vibrato_depth;
    uint16_t vibrato_add_value;

    int16_t note_transpose;
    uint16_t fine_tune_period;
} BdSample;

typedef struct BdSongInfo {
    uint16_t position_lists[4];
} BdSongInfo;

typedef struct BdPositionList {
    uint16_t offset_key;
    uint8_t* data;
    size_t length;
} BdPositionList;

typedef enum BdSampleHandler {
    BdSampleHandler_None = 0,
    BdSampleHandler_PlayOnce,
    BdSampleHandler_Loop,
    BdSampleHandler_VolumeFade,
} BdSampleHandler;

typedef struct BdVoiceInfo {
    bool channel_enabled;
    bool reached_end;  // Set when 0xff is reached in position list (for has_ended tracking)

    uint8_t* position_list;
    size_t position_list_length;
    int current_position;
    int next_position;

    int playing_track;
    uint8_t* track;
    size_t track_length;
    int next_track_position;

    bool switch_to_next_position;
    uint8_t track_loop_counter;
    uint8_t ticks_left_for_next_track_command;

    int8_t transpose;
    uint8_t transposed_note;
    uint8_t previous_transposed_note;
    bool use_new_note;

    uint8_t portamento1_enabled;
    bool portamento2_enabled;
    uint8_t portamento_start_delay;
    uint8_t portamento_duration;
    int8_t portamento_delta_note_number;

    uint8_t portamento_control_flag;
    uint8_t portamento_start_delay_counter;
    uint8_t portamento_duration_counter;
    int portamento_add_value;

    bool volume_fade_enabled;
    uint8_t volume_fade_init_speed;
    uint8_t volume_fade_duration;
    int16_t volume_fade_init_add_value;

    bool volume_fade_running;
    uint8_t volume_fade_speed;
    uint8_t volume_fade_speed_counter;
    uint8_t volume_fade_duration_counter;
    int16_t volume_fade_add_value;
    int16_t volume_fade_value;

    uint16_t channel_volume;
    uint16_t channel_volume_slide_speed;
    int16_t channel_volume_slide_add_value;

    BdSample* sample_info;
    BdSample* sample_info2;

    uint8_t sample_mapping[10];
} BdVoiceInfo;

typedef struct BdVoicePlaybackInfo {
    BdSample* playing_sample;
    uint8_t sample_play_ticks_counter;

    uint16_t note_period;
    int16_t final_volume;

    uint16_t final_volume_slide_speed;
    uint16_t final_volume_slide_speed_counter;
    int16_t final_volume_slide_add_value;

    uint16_t loop_delay_counter;

    int16_t portamento_add_value;

    int16_t sample_portamento_duration;
    int16_t sample_portamento_add_value;

    uint16_t sample_vibrato_depth;
    int16_t sample_vibrato_add_value;

    int16_t sample_period_add_value;

    BdSampleHandler handler;

    // Amiga hardware register simulation
    bool dma_enabled;
    int16_t sample_number;
    int8_t* sample_data;
    uint16_t sample_length;

    // Rendering state
    const int8_t* play_data;
    uint32_t play_length_bytes;
    uint32_t play_loop_offset;
    uint32_t play_loop_length_bytes;
    uint64_t sample_pos_fp;
    uint64_t sample_step_fp;
    bool active;
    bool muted;

    // Pending loop/sample change (applied when current buffer ends)
    bool has_pending_loop;
    uint32_t pending_loop_offset;
    uint32_t pending_loop_length;

    bool has_pending_sample;
    const int8_t* pending_sample_data;
    uint32_t pending_sample_length;
} BdVoicePlaybackInfo;

typedef struct BdGlobalPlayingInfo {
    bool enable_playing;

    int16_t master_volume;
    int16_t master_volume_fade_speed;
    int16_t master_volume_fade_speed_counter;

    uint16_t counter;
} BdGlobalPlayingInfo;

typedef struct BdPlayer {
    BdGlobalPlayingInfo playing_info;
    BdVoiceInfo voices[4];
    BdVoicePlaybackInfo playback[4];

    uint32_t sample_rate;
    uint32_t channel_mask;
    float frames_per_tick;
    float frames_until_tick;
    int current_subsong;
} BdPlayer;

struct BdModule {
    // Offsets found during identification
    int sub_song_list_offset;
    int track_offset_table_offset;
    int tracks_offset;
    int sample_info_offset_table_offset;

    BdFeatures features;

    BdSongInfo* sub_songs;
    size_t sub_song_count;

    BdPositionList* position_lists;
    size_t position_list_count;

    uint8_t** tracks;
    size_t* track_lengths;
    size_t track_count;

    BdSample* samples;
    size_t sample_count;

    // Raw file data (kept for sample data references)
    uint8_t* file_data;
    size_t file_size;

    BdPlayer* player;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Byte reading helpers
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline uint16_t read_be16(const uint8_t* p) {
    return (uint16_t)((p[0] << 8) | p[1]);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline int16_t read_be16s(const uint8_t* p) {
    return (int16_t)((p[0] << 8) | p[1]);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline uint32_t read_be32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Period to frequency
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint16_t bd_calc_period(int period_index, uint16_t fine_tune_period) {
    if (period_index < 0 || period_index >= (int)FINE_TUNE_COUNT) {
        return 0;
    }

    int ft = s_fine_tune[period_index];
    uint16_t period = (uint16_t)((((ft & 0xffff) * fine_tune_period) >> 16) + (((ft >> 16) * fine_tune_period)) & 0xffff);
    return period;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void bd_set_period(BdVoicePlaybackInfo* pb, uint16_t period, uint32_t sample_rate) {
    if (period == 0 || sample_rate == 0) {
        pb->sample_step_fp = 0;
        return;
    }

    uint32_t frequency = 3546895u / (uint32_t)period;
    pb->sample_step_fp = ((uint64_t)frequency << SAMPLE_FRAC_BITS) / (uint64_t)sample_rate;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Format identification
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool bd_extract_info_from_init(const uint8_t* buf, size_t buf_len, int start_of_init,
                                      int* out_sub_song_list_offset, int* out_sample_info_offset_table_offset) {
    int search_length = (int)buf_len;
    int index;

    // Find sub-song information offset
    for (index = start_of_init; index < (search_length - 6); index += 2) {
        if (buf[index] == 0x41 && buf[index + 1] == 0xfa && buf[index + 4] == 0x22 && buf[index + 5] == 0x08) {
            break;
        }
    }

    if (index >= (search_length - 6)) {
        return false;
    }

    *out_sub_song_list_offset = (((int8_t)buf[index + 2] << 8) | buf[index + 3]) + index + 2;
    index += 4;

    // Find sample information offset table offset
    for (; index < (search_length - 6); index += 2) {
        if (buf[index] == 0x41 && buf[index + 1] == 0xfa && buf[index + 4] == 0x23 && buf[index + 5] == 0x48) {
            break;
        }
    }

    if (index >= (search_length - 6)) {
        return false;
    }

    *out_sample_info_offset_table_offset = (((int8_t)buf[index + 2] << 8) | buf[index + 3]) + index + 2;
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool bd_extract_info_from_play(const uint8_t* buf, size_t buf_len, int start_of_play,
                                      int* out_track_offset_table_offset, int* out_tracks_offset) {
    int search_length = (int)buf_len;
    int index;

    // Find track offset table offset
    for (index = start_of_play; index < (search_length - 6); index += 2) {
        if (buf[index] == 0x47 && buf[index + 1] == 0xfa
            && ((buf[index + 4] == 0x48 && buf[index + 5] == 0x80)
                || (buf[index + 4] == 0xd0 && buf[index + 5] == 0x40))) {
            break;
        }
    }

    if (index >= (search_length - 6)) {
        return false;
    }

    *out_track_offset_table_offset = (((int8_t)buf[index + 2] << 8) | buf[index + 3]) + index + 2;
    index += 4;

    // Find tracks offset
    for (; index < (search_length - 6); index += 2) {
        if (buf[index] == 0x47 && buf[index + 1] == 0xfa && buf[index + 4] == 0xd6 && buf[index + 5] == 0xc0) {
            break;
        }
    }

    if (index >= (search_length - 6)) {
        return false;
    }

    *out_tracks_offset = (((int8_t)buf[index + 2] << 8) | buf[index + 3]) + index + 2;
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool bd_find_features_in_play(const uint8_t* buf, size_t buf_len, int start_of_play, BdFeatures* f) {
    int search_length = (int)buf_len;
    int index;

    // Check for counter feature
    f->enable_counter = false;

    if (start_of_play >= (search_length - 16)) {
        return false;
    }

    if (buf[start_of_play + 4] == 0x10 && buf[start_of_play + 5] == 0x3a
        && buf[start_of_play + 8] == 0x67 && buf[start_of_play + 14] == 0x53
        && buf[start_of_play + 15] == 0x50) {
        index = (((int8_t)buf[start_of_play + 6] << 8) | buf[start_of_play + 7]) + start_of_play + 6;
        if (index >= search_length) {
            return false;
        }
        f->enable_counter = buf[index] != 0;
    }

    // Check effect calls
    f->enable_portamento = false;
    f->enable_volume_fade = false;

    for (index = start_of_play; index < (search_length - 2); index += 2) {
        if (buf[index] == 0x53 && buf[index + 1] == 0x2c) {
            break;
        }
    }

    if (index >= (search_length - 2)) {
        return false;
    }

    for (; index >= start_of_play; index -= 2) {
        if (buf[index] == 0x49 && buf[index + 1] == 0xfa) {
            break;
        }

        if (buf[index] == 0x61 && buf[index + 1] == 0x00) {
            int method_index = (((int8_t)buf[index + 2] << 8) | buf[index + 3]) + index + 2;

            if (method_index >= (search_length - 14)) {
                return false;
            }

            if (buf[method_index] == 0x4a && buf[method_index + 1] == 0x2c
                && buf[method_index + 4] == 0x67 && buf[method_index + 6] == 0x6a
                && buf[method_index + 8] == 0x30 && buf[method_index + 9] == 0x29) {
                f->enable_portamento = true;
            } else if (buf[method_index] == 0x4a && buf[method_index + 1] == 0x2c
                       && buf[method_index + 4] == 0x67 && buf[method_index + 6] == 0x4a
                       && buf[method_index + 7] == 0x2c && buf[method_index + 10] == 0x67) {
                f->enable_volume_fade = true;
            } else {
                return false;
            }
        }
    }

    // Check for position effects
    f->max_track_value = 0x80;

    for (index = start_of_play; index < (search_length - 6); index += 2) {
        if (buf[index] == 0x10 && buf[index + 1] == 0x1b) {
            break;
        }
    }

    if (index >= (search_length - 6)) {
        return false;
    }

    if ((buf[index + 2] == 0xb0 && buf[index + 3] == 0x3c) || (buf[index + 2] == 0x0c && buf[index + 3] == 0x00)) {
        f->max_track_value = buf[index + 5];
    }

    for (index += 4; index < (search_length - 6); index += 2) {
        if (((buf[index] == 0xb0 && buf[index + 1] == 0x3c) || (buf[index] == 0x0c && buf[index + 1] == 0x00))
            && buf[index + 4] == 0x6c) {
            break;
        }
    }

    if (index >= (search_length - 6)) {
        return false;
    }

    int effect = (buf[index + 2] << 8) | buf[index + 3];
    f->enable_c0_track_loop = effect == 0x00c0;
    f->enable_f0_track_loop = effect == 0x00f0;

    index = buf[index + 5] + index + 6;

    if (index >= (int)buf_len - 1) {
        return false;
    }

    if (buf[index] == 0x02 && buf[index + 1] == 0x40) {
        f->set_sample_mapping_version = 1;
    } else if (buf[index] == 0x04 && buf[index + 1] == 0x00) {
        f->set_sample_mapping_version = 2;
    } else {
        return false;
    }

    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool bd_find_features_in_handle_effects(const uint8_t* buf, size_t buf_len, int start_of_play, BdFeatures* f) {
    int search_length = (int)buf_len;
    int index;

    if (buf[start_of_play] != 0x61 || buf[start_of_play + 1] != 0x00) {
        return false;
    }

    int start_of_handle_effects = (((int8_t)buf[start_of_play + 2] << 8) | buf[start_of_play + 3]) + start_of_play + 2;

    // Find call to sample handler callback method
    for (index = start_of_handle_effects; index < (search_length - 2); index += 2) {
        if (buf[index] == 0x4e && buf[index + 1] == 0x90) {
            break;
        }
    }

    if (index >= (search_length - 2)) {
        return false;
    }

    int callback_index = index;

    // Search back after effect method calls
    f->enable_sample_effects = false;
    f->enable_final_volume_slide = false;

    for (; index >= start_of_handle_effects; index -= 2) {
        if (buf[index] == 0x4e && buf[index + 1] == 0x75) {
            break;
        }

        if (buf[index] == 0x61 && buf[index + 1] == 0x00) {
            int method_index = (((int8_t)buf[index + 2] << 8) | buf[index + 3]) + index + 2;

            if (method_index >= (search_length - 14)) {
                return false;
            }

            if (buf[method_index] == 0x30 && buf[method_index + 1] == 0x2b
                && buf[method_index + 4] == 0x67
                && ((buf[method_index + 6] == 0xb0 && buf[method_index + 7] == 0x7c)
                    || (buf[method_index + 6] == 0x0c && buf[method_index + 7] == 0x40))
                && buf[method_index + 8] == 0xff && buf[method_index + 9] == 0xff) {
                f->enable_sample_effects = true;
            } else if (buf[method_index] == 0x30 && buf[method_index + 1] == 0x2b
                       && buf[method_index + 4] == 0x67
                       && buf[method_index + 6] == 0x53 && buf[method_index + 7] == 0x6b) {
                f->enable_final_volume_slide = true;
            } else {
                return false;
            }
        }
    }

    if (buf[callback_index + 6] != 0x6e && buf[callback_index + 6] != 0x66) {
        return false;
    }

    index = buf[callback_index + 7] + callback_index + 8;
    if (index >= (search_length - 6)) {
        return false;
    }

    // Check for setting DMA in sample handlers
    f->set_dma_in_sample_handlers = true;

    for (; index < search_length; index++) {
        if (buf[index] == 0x4e && buf[index + 1] == 0x75) {
            break;
        }
    }

    if (index >= search_length) {
        return false;
    }

    if (buf[index - 2] == 0x00 && buf[index - 1] == 0x96) {
        f->set_dma_in_sample_handlers = false;
    }

    // Check for master volume fade feature
    if (buf[start_of_handle_effects] == 0x61 && buf[start_of_handle_effects + 1] == 0x00) {
        index = (((int8_t)buf[start_of_handle_effects + 2] << 8) | buf[start_of_handle_effects + 3])
                + start_of_handle_effects + 2;

        if (index >= (search_length - 24)) {
            return false;
        }

        f->master_volume_fade_version = -1;

        if (buf[index] == 0x30 && buf[index + 1] == 0x3a && buf[index + 4] == 0x67 && buf[index + 5] == 0x00
            && buf[index + 8] == 0x41 && buf[index + 9] == 0xfa && buf[index + 18] == 0x30
            && buf[index + 19] == 0x80) {
            f->master_volume_fade_version = 1;
        } else if (buf[index] == 0x30 && buf[index + 1] == 0x39 && buf[index + 6] == 0x67
                   && buf[index + 7] == 0x00 && buf[index + 10] == 0x41 && buf[index + 11] == 0xf9
                   && buf[index + 22] == 0x30 && buf[index + 23] == 0x80) {
            f->master_volume_fade_version = 1;
        } else if (buf[index] == 0x10 && buf[index + 1] == 0x3a && buf[index + 4] == 0x67
                   && buf[index + 5] == 0x00 && buf[index + 8] == 0x41 && buf[index + 9] == 0xfa
                   && buf[index + 18] == 0x53 && buf[index + 19] == 0x00) {
            f->master_volume_fade_version = 2;
        } else {
            return false;
        }
    }

    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool bd_find_features_in_parse_track_effect(const uint8_t* buf, size_t buf_len, int start_of_method,
                                                   BdFeatures* f) {
    int search_length = (int)buf_len;
    int index;

    if ((buf[start_of_method + 2] != 0xb0 || buf[start_of_method + 3] != 0x3c)
        && (buf[start_of_method + 2] != 0x0c || buf[start_of_method + 3] != 0x00)) {
        return false;
    }

    f->max_sample_mapping_value = buf[start_of_method + 5];
    index = start_of_method + 8;

    if (index >= (search_length - 4)) {
        return false;
    }

    if (buf[index] != 0x02 || buf[index + 1] != 0x40 || buf[index + 2] != 0x00) {
        return false;
    }

    if (buf[index + 3] == 0x07) {
        f->get_sample_mapping_version = 1;
    } else if (buf[index + 3] == 0xff) {
        f->get_sample_mapping_version = 2;
    } else {
        return true;
    }

    for (index += 4; index < (search_length - 6); index += 2) {
        if (((buf[index] == 0xb0 && buf[index + 1] == 0x3c) || (buf[index] == 0x0c && buf[index + 1] == 0x00))
            && buf[index + 4] == 0x6c) {
            break;
        }
    }

    if (index >= (search_length - 6)) {
        return false;
    }

    f->uses_9x_track_effects = (buf[index + 3] & 0xf0) == 0x90;
    f->uses_cx_track_effects = (buf[index + 3] & 0xf0) == 0xc0;

    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool bd_find_features_in_parse_track(const uint8_t* buf, size_t buf_len, int start_of_play, BdFeatures* f) {
    int search_length = (int)buf_len;
    int index;

    for (index = start_of_play; index < (search_length - 4); index += 2) {
        if (buf[index] == 0x60 && buf[index + 1] == 0x00) {
            break;
        }
    }

    if (index >= (search_length - 4)) {
        return false;
    }

    index = (((int8_t)buf[index + 2] << 8) | buf[index + 3]) + index + 2;
    if (index >= search_length) {
        return false;
    }

    int start_of_parse_track = index;

    for (; index < (search_length - 8); index += 2) {
        if (buf[index] == 0x4a && buf[index + 1] == 0x2c && buf[index + 4] == 0x67) {
            break;
        }
    }

    if (index >= (search_length - 8)) {
        return false;
    }

    f->check_for_ticks = buf[index + 6] == 0x4a && buf[index + 7] == 0x2c;

    for (index += 8; index < (search_length - 6); index += 2) {
        if (buf[index] == 0x72 && buf[index + 1] == 0x00 && buf[index + 2] == 0x12 && buf[index + 3] == 0x1b) {
            break;
        }
    }

    if (index >= (search_length - 6)) {
        return false;
    }

    f->extra_tick_arg = buf[index + 4] == 0x66;

    for (index = start_of_parse_track; index < (search_length - 4); index += 2) {
        if (buf[index] == 0x61 && buf[index + 1] == 0x00) {
            break;
        }
    }

    if (index >= (search_length - 4)) {
        return false;
    }

    index = (((int8_t)buf[index + 2] << 8) | buf[index + 3]) + index + 2;
    if (index >= search_length) {
        return false;
    }

    return bd_find_features_in_parse_track_effect(buf, buf_len, index, f);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool bd_find_features(const uint8_t* buf, size_t buf_len, int start_of_play, BdFeatures* f) {
    memset(f, 0, sizeof(*f));
    f->master_volume_fade_version = -1;

    if (!bd_find_features_in_play(buf, buf_len, start_of_play, f)) {
        return false;
    }
    if (!bd_find_features_in_handle_effects(buf, buf_len, start_of_play, f)) {
        return false;
    }
    if (!bd_find_features_in_parse_track(buf, buf_len, start_of_play, f)) {
        return false;
    }

    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool bd_test_module(const uint8_t* data, size_t size, int* out_sub_song_list_offset,
                           int* out_track_offset_table_offset, int* out_tracks_offset,
                           int* out_sample_info_offset_table_offset, BdFeatures* features) {
    if (size < 0x1600) {
        return false;
    }

    size_t buf_len = size < 0x3000 ? size : 0x3000;
    const uint8_t* buf = data;

    // Check for BRA instructions at required offsets
    if (buf[0] != 0x60 || buf[1] != 0x00 || buf[4] != 0x60 || buf[5] != 0x00 || buf[10] != 0x60
        || buf[11] != 0x00) {
        return false;
    }

    // Find the init function
    int start_of_init = (((int8_t)buf[2] << 8) | buf[3]) + 2;
    if (start_of_init < 0 || start_of_init >= (int)(buf_len - 14)) {
        return false;
    }

    if (buf[start_of_init] != 0x3f || buf[start_of_init + 1] != 0x00 || buf[start_of_init + 2] != 0x61
        || buf[start_of_init + 3] != 0x00 || buf[start_of_init + 6] != 0x3d || buf[start_of_init + 7] != 0x7c
        || buf[start_of_init + 12] != 0x41 || buf[start_of_init + 13] != 0xfa) {
        return false;
    }

    // Find the play function
    int start_of_play = (((int8_t)buf[6] << 8) | buf[7]) + 4 + 2;
    if (start_of_play < 0 || start_of_play >= (int)buf_len) {
        return false;
    }

    if (!bd_extract_info_from_init(buf, buf_len, start_of_init, out_sub_song_list_offset,
                                   out_sample_info_offset_table_offset)) {
        return false;
    }

    if (!bd_extract_info_from_play(buf, buf_len, start_of_play, out_track_offset_table_offset, out_tracks_offset)) {
        return false;
    }

    if (!bd_find_features(buf, buf_len, start_of_play, features)) {
        return false;
    }

    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Position list command argument count
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static int bd_position_command_arg_count(uint8_t cmd, const BdFeatures* f) {
    if (cmd < f->max_track_value) {
        return 0;
    }

    if (f->enable_c0_track_loop) {
        if (cmd < 0xa0) {
            return 0;
        }
        if (cmd < 0xc8) {
            return 1;
        }
    }

    if (f->enable_f0_track_loop) {
        if (cmd < 0xf0) {
            return 0;
        }
        if (cmd < 0xf8) {
            return 1;
        }
    }

    if (cmd == 0xfd && f->master_volume_fade_version > 0) {
        return 1;
    }

    if (cmd == 0xfe) {
        return 1;
    }

    return -1;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Track command argument count
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static int bd_track_command_arg_count(uint8_t cmd, uint8_t next_byte, const BdFeatures* f) {
    if (cmd < 0x7f) {
        if (f->extra_tick_arg && next_byte == 0) {
            return 2;
        }
        return 1;
    }

    if (cmd == 0x7f) {
        return 1;
    }

    if (cmd <= f->max_sample_mapping_value) {
        return 0;
    }

    if ((f->uses_cx_track_effects && cmd < 0xc0) || (f->uses_9x_track_effects && cmd < 0x9b)) {
        return 0;
    }

    // Portamento enable (3 args)
    if ((cmd == 0xc0 && f->uses_cx_track_effects && f->enable_portamento)
        || (cmd == 0x9b && f->uses_9x_track_effects && f->enable_portamento)) {
        return 3;
    }

    // Portamento disable (0 args)
    if ((cmd == 0xc1 && f->uses_cx_track_effects && f->enable_portamento)
        || (cmd == 0x9c && f->uses_9x_track_effects && f->enable_portamento)) {
        return 0;
    }

    // Volume fade enable (3 args)
    if ((cmd == 0xc2 && f->uses_cx_track_effects && f->enable_volume_fade)
        || (cmd == 0x9d && f->uses_9x_track_effects && f->enable_volume_fade)) {
        return 3;
    }

    // Volume fade disable (0 args)
    if ((cmd == 0xc3 && f->uses_cx_track_effects && f->enable_volume_fade)
        || (cmd == 0x9e && f->uses_9x_track_effects && f->enable_volume_fade)) {
        return 0;
    }

    // Portamento2 enable (1 arg)
    if ((cmd == 0xc4 && f->uses_cx_track_effects && f->enable_portamento)
        || (cmd == 0x9f && f->uses_9x_track_effects && f->enable_portamento)) {
        return 1;
    }

    // Portamento2 disable (0 args)
    if ((cmd == 0xc5 && f->uses_cx_track_effects && f->enable_portamento)
        || (cmd == 0xa0 && f->uses_9x_track_effects && f->enable_portamento)) {
        return 0;
    }

    // Channel volume (1 or 3 args)
    if ((cmd == 0xc6 && f->uses_cx_track_effects && f->enable_volume_fade)
        || (cmd == 0xa1 && f->uses_9x_track_effects && f->enable_volume_fade)) {
        return f->enable_final_volume_slide ? 3 : 1;
    }

    // Final volume slide disable (0 args)
    if ((cmd == 0xc7 && f->uses_cx_track_effects && f->enable_final_volume_slide)
        || (cmd == 0xa2 && f->uses_9x_track_effects && f->enable_final_volume_slide)) {
        return 0;
    }

    return -1;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Module loading
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool bd_load_sub_song_info(BdModule* mod) {
    const uint8_t* data = mod->file_data;
    size_t size = mod->file_size;
    int offset = mod->sub_song_list_offset;

    if (offset < 0 || offset >= (int)size) {
        return false;
    }

    // Count sub-songs: each entry is 8 bytes (4 x uint16), terminated when position reaches first position list
    int first_position_list = 0x7fffffff;
    size_t capacity = 16;
    mod->sub_songs = (BdSongInfo*)calloc(capacity, sizeof(BdSongInfo));
    mod->sub_song_count = 0;

    int pos = offset;

    do {
        if (pos + 8 > (int)size) {
            return false;
        }

        if (mod->sub_song_count >= capacity) {
            capacity *= 2;
            BdSongInfo* new_songs = (BdSongInfo*)realloc(mod->sub_songs, capacity * sizeof(BdSongInfo));
            if (new_songs == nullptr) {
                return false;
            }
            mod->sub_songs = new_songs;
        }

        BdSongInfo* song = &mod->sub_songs[mod->sub_song_count];
        for (int i = 0; i < 4; i++) {
            song->position_lists[i] = read_be16(data + pos);
            pos += 2;

            if ((int)song->position_lists[i] < first_position_list) {
                first_position_list = (int)song->position_lists[i];
            }
        }

        mod->sub_song_count++;
    } while (pos < offset + first_position_list);

    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint8_t* bd_load_single_position_list(const uint8_t* data, size_t size, int offset, const BdFeatures* f,
                                             size_t* out_len) {
    if (offset < 0 || offset >= (int)size) {
        return nullptr;
    }

    size_t capacity = 64;
    uint8_t* list = (uint8_t*)malloc(capacity);
    size_t len = 0;
    int pos = offset;

    for (;;) {
        if (pos >= (int)size) {
            free(list);
            return nullptr;
        }

        uint8_t cmd = data[pos++];

        if (len >= capacity) {
            capacity *= 2;
            uint8_t* new_list = (uint8_t*)realloc(list, capacity);
            if (new_list == nullptr) {
                free(list);
                return nullptr;
            }
            list = new_list;
        }

        list[len++] = cmd;

        if (cmd == 0xff) {
            break;
        }

        int arg_count = bd_position_command_arg_count(cmd, f);
        if (arg_count == -1) {
            free(list);
            return nullptr;
        }

        for (int i = 0; i < arg_count; i++) {
            if (pos >= (int)size) {
                free(list);
                return nullptr;
            }

            if (len >= capacity) {
                capacity *= 2;
                uint8_t* new_list = (uint8_t*)realloc(list, capacity);
                if (new_list == nullptr) {
                    free(list);
                    return nullptr;
                }
                list = new_list;
            }

            list[len++] = data[pos++];
        }
    }

    *out_len = len;
    return list;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool bd_load_position_lists(BdModule* mod) {
    // Collect unique position list offsets
    size_t capacity = 16;
    mod->position_lists = (BdPositionList*)calloc(capacity, sizeof(BdPositionList));
    mod->position_list_count = 0;

    for (size_t s = 0; s < mod->sub_song_count; s++) {
        for (int ch = 0; ch < 4; ch++) {
            uint16_t pl_offset = mod->sub_songs[s].position_lists[ch];

            // Check if already loaded
            bool found = false;
            for (size_t j = 0; j < mod->position_list_count; j++) {
                if (mod->position_lists[j].offset_key == pl_offset) {
                    found = true;
                    break;
                }
            }

            if (found) {
                continue;
            }

            if (mod->position_list_count >= capacity) {
                capacity *= 2;
                BdPositionList* new_pls = (BdPositionList*)realloc(mod->position_lists,
                                                                   capacity * sizeof(BdPositionList));
                if (new_pls == nullptr) {
                    return false;
                }
                mod->position_lists = new_pls;
            }

            size_t pl_len = 0;
            uint8_t* pl_data = bd_load_single_position_list(mod->file_data, mod->file_size,
                                                            mod->sub_song_list_offset + pl_offset, &mod->features,
                                                            &pl_len);

            if (pl_data == nullptr) {
                return false;
            }

            mod->position_lists[mod->position_list_count].offset_key = pl_offset;
            mod->position_lists[mod->position_list_count].data = pl_data;
            mod->position_lists[mod->position_list_count].length = pl_len;
            mod->position_list_count++;
        }
    }

    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint8_t* bd_load_single_track(const uint8_t* data, size_t size, int offset, const BdFeatures* f,
                                     size_t* out_len) {
    if (offset < 0 || offset >= (int)size) {
        return nullptr;
    }

    size_t capacity = 128;
    uint8_t* track = (uint8_t*)malloc(capacity);
    size_t len = 0;
    int pos = offset;

    for (;;) {
        if (pos >= (int)size) {
            free(track);
            return nullptr;
        }

        uint8_t cmd = data[pos++];

        if (len >= capacity) {
            capacity *= 2;
            uint8_t* new_track = (uint8_t*)realloc(track, capacity);
            if (new_track == nullptr) {
                free(track);
                return nullptr;
            }
            track = new_track;
        }

        track[len++] = cmd;

        if (cmd == 0xff) {
            break;
        }

        if (pos >= (int)size) {
            free(track);
            return nullptr;
        }

        uint8_t next_byte = data[pos];

        int arg_count = bd_track_command_arg_count(cmd, next_byte, f);
        if (arg_count == -1) {
            free(track);
            return nullptr;
        }

        if (arg_count > 0) {
            // next_byte is consumed as first arg
            if (len >= capacity) {
                capacity *= 2;
                uint8_t* new_track = (uint8_t*)realloc(track, capacity);
                if (new_track == nullptr) {
                    free(track);
                    return nullptr;
                }
                track = new_track;
            }

            track[len++] = next_byte;
            pos++;

            for (int i = 1; i < arg_count; i++) {
                if (pos >= (int)size) {
                    free(track);
                    return nullptr;
                }

                if (len >= capacity) {
                    capacity *= 2;
                    uint8_t* new_track = (uint8_t*)realloc(track, capacity);
                    if (new_track == nullptr) {
                        free(track);
                        return nullptr;
                    }
                    track = new_track;
                }

                track[len++] = data[pos++];
            }
        }
        // If arg_count == 0, next_byte was not consumed (seek back in C# - we just don't advance pos)
    }

    *out_len = len;
    return track;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool bd_load_tracks(BdModule* mod) {
    int number_of_tracks = (mod->sub_song_list_offset - mod->track_offset_table_offset) / 2;
    if (number_of_tracks <= 0) {
        return false;
    }

    mod->track_count = (size_t)number_of_tracks;
    mod->tracks = (uint8_t**)calloc(mod->track_count, sizeof(uint8_t*));
    mod->track_lengths = (size_t*)calloc(mod->track_count, sizeof(size_t));

    if (mod->tracks == nullptr || mod->track_lengths == nullptr) {
        return false;
    }

    // Read track offset table
    int table_pos = mod->track_offset_table_offset;
    if (table_pos < 0 || table_pos + number_of_tracks * 2 > (int)mod->file_size) {
        return false;
    }

    for (int i = 0; i < number_of_tracks; i++) {
        uint16_t track_offset = read_be16(mod->file_data + table_pos + i * 2);
        int abs_offset = mod->tracks_offset + track_offset;

        size_t track_len = 0;
        uint8_t* track_data = bd_load_single_track(mod->file_data, mod->file_size, abs_offset, &mod->features,
                                                   &track_len);

        if (track_data == nullptr) {
            return false;
        }

        mod->tracks[i] = track_data;
        mod->track_lengths[i] = track_len;
    }

    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool bd_load_sample_info(BdModule* mod) {
    const uint8_t* data = mod->file_data;
    size_t size = mod->file_size;
    int table_offset = mod->sample_info_offset_table_offset;

    if (table_offset < 0 || table_offset >= (int)size) {
        return false;
    }

    // Read offset table
    size_t capacity = 16;
    uint32_t* offset_table = (uint32_t*)calloc(capacity, sizeof(uint32_t));
    size_t offset_count = 0;
    uint32_t first_sample_info = 0xffffffff;

    int pos = table_offset;
    do {
        if (pos + 4 > (int)size) {
            free(offset_table);
            return false;
        }

        if (offset_count >= capacity) {
            capacity *= 2;
            uint32_t* new_table = (uint32_t*)realloc(offset_table, capacity * sizeof(uint32_t));
            if (new_table == nullptr) {
                free(offset_table);
                return false;
            }
            offset_table = new_table;
        }

        uint32_t offset = read_be32(data + pos);
        pos += 4;

        if (offset < first_sample_info) {
            first_sample_info = offset;
        }

        offset_table[offset_count++] = offset;
    } while (pos < table_offset + (int)first_sample_info);

    // Now read sample info and sample data
    mod->sample_count = offset_count;
    mod->samples = (BdSample*)calloc(mod->sample_count, sizeof(BdSample));

    if (mod->samples == nullptr) {
        free(offset_table);
        return false;
    }

    for (size_t i = 0; i < mod->sample_count; i++) {
        int info_pos = table_offset + (int)offset_table[i];
        if (info_pos + 24 > (int)size) {
            free(offset_table);
            return false;
        }

        BdSample* sample = &mod->samples[i];
        sample->sample_number = (int16_t)i;

        uint32_t sample_data_offset = read_be32(data + info_pos);
        info_pos += 4;

        sample->loop_offset = read_be32(data + info_pos);
        info_pos += 4;
        if (sample->loop_offset > 0) {
            sample->loop_offset -= sample_data_offset;
        }

        sample->length = read_be16(data + info_pos);
        info_pos += 2;
        sample->loop_length = read_be16(data + info_pos);
        info_pos += 2;

        sample->volume = read_be16(data + info_pos);
        info_pos += 2;
        sample->volume_fade_speed = read_be16s(data + info_pos);
        info_pos += 2;

        sample->portamento_duration = read_be16s(data + info_pos);
        info_pos += 2;
        sample->portamento_add_value = read_be16s(data + info_pos);
        info_pos += 2;

        sample->vibrato_depth = read_be16(data + info_pos);
        info_pos += 2;
        sample->vibrato_add_value = read_be16(data + info_pos);
        info_pos += 2;

        sample->note_transpose = read_be16s(data + info_pos);
        info_pos += 2;
        sample->fine_tune_period = read_be16(data + info_pos);
        info_pos += 2;

        // Load sample data
        int abs_sample_offset = table_offset + (int)sample_data_offset;
        int sample_end1 = sample->length * 2;
        int sample_end2 = (int)(sample->loop_offset + sample->loop_length * 2);
        int sample_len = sample_end1 > sample_end2 ? sample_end1 : sample_end2;

        if (sample_len <= 0) {
            sample->sample_data = nullptr;
            continue;
        }

        if (abs_sample_offset < 0 || abs_sample_offset + sample_len > (int)size) {
            free(offset_table);
            return false;
        }

        sample->sample_data = (int8_t*)malloc((size_t)sample_len);
        if (sample->sample_data == nullptr) {
            free(offset_table);
            return false;
        }

        memcpy(sample->sample_data, data + abs_sample_offset, (size_t)sample_len);
    }

    free(offset_table);
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Position list lookup
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint8_t* bd_find_position_list(BdModule* mod, uint16_t offset_key, size_t* out_len) {
    for (size_t i = 0; i < mod->position_list_count; i++) {
        if (mod->position_lists[i].offset_key == offset_key) {
            *out_len = mod->position_lists[i].length;
            return mod->position_lists[i].data;
        }
    }

    *out_len = 0;
    return nullptr;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Player state initialization
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void bd_init_sound(BdModule* mod, int sub_song) {
    BdPlayer* p = mod->player;

    if (sub_song < 0 || sub_song >= (int)mod->sub_song_count) {
        return;
    }

    BdSongInfo* song = &mod->sub_songs[sub_song];

    p->playing_info.enable_playing = true;
    p->playing_info.master_volume = 64;
    p->playing_info.master_volume_fade_speed = 0;
    p->playing_info.master_volume_fade_speed_counter = 1;
    p->playing_info.counter = 6;

    for (int i = 0; i < 4; i++) {
        BdVoiceInfo* v = &p->voices[i];
        memset(v, 0, sizeof(*v));

        size_t pl_len = 0;
        v->position_list = bd_find_position_list(mod, song->position_lists[i], &pl_len);
        v->position_list_length = pl_len;
        v->channel_enabled = true;
        v->switch_to_next_position = true;
        v->track_loop_counter = 1;
        v->ticks_left_for_next_track_command = 1;
        v->use_new_note = true;
        v->channel_volume = 0xffff;

        // Initialize sample mapping
        for (uint8_t j = 0; j < 10; j++) {
            v->sample_mapping[j] = j;
        }

        BdVoicePlaybackInfo* pb = &p->playback[i];
        memset(pb, 0, sizeof(*pb));
        pb->sample_number = -1;
        pb->handler = BdSampleHandler_None;
    }

    // Don't reset p->frames_until_tick here - preserve tick phase on restart
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Sample playback
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static float bd_voice_sample(const BdVoicePlaybackInfo* pb) {
    if (pb->muted || !pb->active || pb->play_data == nullptr || pb->play_length_bytes == 0) {
        return 0.0f;
    }

    uint64_t index = pb->sample_pos_fp >> SAMPLE_FRAC_BITS;

    if (index >= pb->play_length_bytes) {
        if (pb->play_loop_length_bytes > 0) {
            uint64_t rel = (index - pb->play_loop_offset) % pb->play_loop_length_bytes;
            index = pb->play_loop_offset + rel;
        } else {
            return 0.0f;
        }
    }

    return (float)pb->play_data[index] / 128.0f;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void bd_voice_advance(BdVoicePlaybackInfo* pb) {
    if (!pb->active) {
        return;
    }

    pb->sample_pos_fp += pb->sample_step_fp;

    uint64_t byte_pos = pb->sample_pos_fp >> SAMPLE_FRAC_BITS;

    if (byte_pos >= pb->play_length_bytes) {
        // Current buffer ended - apply pending changes if any
        if (pb->has_pending_sample) {
            // Switch to a completely new sample (e.g., empty sample for PlayOnce)
            pb->play_data = pb->pending_sample_data;
            pb->play_length_bytes = pb->pending_sample_length;
            pb->play_loop_offset = 0;
            pb->play_loop_length_bytes = pb->pending_sample_length;
            pb->sample_pos_fp = 0;
            pb->has_pending_sample = false;
            pb->has_pending_loop = false;
        } else if (pb->has_pending_loop) {
            // Switch to new loop region within same sample data.
            // C# mixer resets position to loop start via SetNewSample().
            pb->play_loop_offset = pb->pending_loop_offset;
            pb->play_loop_length_bytes = pb->pending_loop_length;
            pb->play_length_bytes = pb->pending_loop_offset + pb->pending_loop_length;
            pb->sample_pos_fp = (uint64_t)pb->pending_loop_offset << SAMPLE_FRAC_BITS;
            pb->has_pending_loop = false;
        } else if (pb->play_loop_length_bytes > 0) {
            // Standard loop wrap
            uint64_t loop_start_fp = (uint64_t)pb->play_loop_offset << SAMPLE_FRAC_BITS;
            uint64_t loop_len_fp = (uint64_t)pb->play_loop_length_bytes << SAMPLE_FRAC_BITS;
            uint64_t end_fp = (uint64_t)pb->play_length_bytes << SAMPLE_FRAC_BITS;

            if (pb->sample_pos_fp >= end_fp && loop_len_fp > 0) {
                uint64_t overflow = pb->sample_pos_fp - end_fp;
                pb->sample_pos_fp = loop_start_fp + (overflow % loop_len_fp);
            }
        } else {
            pb->active = false;
        }
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Amiga hardware simulation
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void bd_play_sample(BdVoicePlaybackInfo* pb, int16_t sample_number, const int8_t* data, uint32_t length) {
    pb->play_data = data;
    pb->play_length_bytes = length;
    pb->play_loop_offset = 0;
    pb->play_loop_length_bytes = length;  // Amiga always loops
    pb->sample_pos_fp = 0;
    pb->active = true;
    pb->muted = false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void bd_set_loop(BdVoicePlaybackInfo* pb, uint32_t offset, uint32_t length) {
    if (!pb->dma_enabled) {
        // Not yet playing - set directly
        pb->play_loop_offset = offset;
        pb->play_loop_length_bytes = length;
    } else {
        // Already playing - defer until current buffer ends
        pb->has_pending_loop = true;
        pb->pending_loop_offset = offset;
        pb->pending_loop_length = length;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// SetSample with same data - changes loop region (deferred)

static void bd_set_sample_region(BdVoicePlaybackInfo* pb, uint32_t offset, uint32_t length) {
    // Defer the loop region change to when current buffer ends
    pb->has_pending_loop = true;
    pb->pending_loop_offset = offset;
    pb->pending_loop_length = length;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// SetSample with new data - switches to different sample data (deferred)

static void bd_set_sample_data(BdVoicePlaybackInfo* pb, const int8_t* data, uint32_t length) {
    // Defer switch to new sample data until current buffer ends
    pb->has_pending_sample = true;
    pb->pending_sample_data = data;
    pb->pending_sample_length = length;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void bd_mute_channel(BdVoicePlaybackInfo* pb) {
    pb->muted = true;
    pb->active = false;
    pb->has_pending_loop = false;
    pb->has_pending_sample = false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void bd_enable_dma(BdVoicePlaybackInfo* pb) {
    if (!pb->dma_enabled) {
        uint32_t sample_length = pb->sample_length * 2u;
        bd_play_sample(pb, pb->sample_number, pb->sample_data, sample_length);
        // On Amiga, DMA always loops the buffer. Set initial loop to full sample.
        pb->play_loop_offset = 0;
        pb->play_loop_length_bytes = sample_length;
        pb->has_pending_loop = false;
        pb->has_pending_sample = false;
        pb->dma_enabled = true;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Sample handler callbacks
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void bd_handle_sample_loop(BdVoicePlaybackInfo* pb, BdSample* sample, bool set_dma_in_handlers) {
    if (pb->loop_delay_counter < 0x8000) {
        if (set_dma_in_handlers) {
            bd_enable_dma(pb);
        }

        pb->loop_delay_counter--;

        if (pb->loop_delay_counter == 0) {
            pb->loop_delay_counter = (uint16_t)~pb->loop_delay_counter;

            uint32_t loop_length = sample->loop_length * 2u;
            if (loop_length > 0) {
                // Amiga: update loop registers, takes effect when current buffer ends
                bd_set_sample_region(pb, sample->loop_offset, loop_length);
                bd_set_loop(pb, sample->loop_offset, loop_length);
            } else {
                bd_mute_channel(pb);
            }
        }
    } else {
        if (pb->sample_play_ticks_counter <= 1) {
            pb->handler = BdSampleHandler_VolumeFade;
        }
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void bd_handle_sample_volume_fade(BdVoicePlaybackInfo* pb, BdSample* sample) {
    pb->loop_delay_counter = 0x8000;
    pb->final_volume = (int16_t)(pb->final_volume + sample->volume_fade_speed);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void bd_handle_sample_play_once(BdVoicePlaybackInfo* pb, BdSample* sample, bool set_dma_in_handlers) {
    (void)sample;

    if (pb->loop_delay_counter < 0x8000) {
        pb->loop_delay_counter--;

        if (pb->loop_delay_counter == 0) {
            pb->loop_delay_counter = (uint16_t)~pb->loop_delay_counter;

            // Stop sample - simulate Amiga Audio IRQ stopping DMA
            // Deferred: switches to empty sample when current buffer finishes
            bd_set_sample_data(pb, s_empty_sample, (uint32_t)sizeof(s_empty_sample));
        } else {
            if (set_dma_in_handlers) {
                bd_enable_dma(pb);
            }
        }
    } else {
        if (pb->sample_play_ticks_counter == 1) {
            pb->loop_delay_counter = 0x8000;
        }

        if (!pb->active) {
            pb->final_volume = 0;
        }
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Effects
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void bd_do_sample_effects(BdVoicePlaybackInfo* pb, BdSample* sample) {
    if (pb->sample_portamento_duration != 0) {
        if (pb->sample_portamento_duration == -1) {
            return;
        }

        pb->sample_period_add_value += pb->sample_portamento_add_value;
        pb->sample_portamento_duration--;

        if (pb->sample_portamento_duration != 0) {
            return;
        }
    }

    pb->sample_period_add_value += pb->sample_vibrato_add_value;
    pb->sample_vibrato_depth--;

    if (pb->sample_vibrato_depth == 0) {
        if (sample->vibrato_depth != 0) {
            pb->sample_vibrato_depth = sample->vibrato_depth;
            pb->sample_vibrato_add_value = (int16_t)-pb->sample_vibrato_add_value;
        }
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void bd_do_final_volume_slide(BdVoicePlaybackInfo* pb) {
    if (pb->final_volume_slide_speed != 0) {
        pb->final_volume_slide_speed_counter--;

        if (pb->final_volume_slide_speed_counter == 0) {
            pb->final_volume_slide_speed_counter = pb->final_volume_slide_speed;
            pb->final_volume = (int16_t)(pb->final_volume + pb->final_volume_slide_add_value);
        }
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void bd_do_portamento(BdVoiceInfo* v, BdVoicePlaybackInfo* pb) {
    if (v->portamento_control_flag != 0) {
        if (v->portamento_control_flag >= 0x80) {
            int temp = (int16_t)((pb->note_period * (v->portamento_add_value & 0xffff) >> 16)
                                 + (pb->note_period * (v->portamento_add_value >> 16)))
                       - pb->note_period;
            v->portamento_add_value = temp / v->portamento_duration_counter;
            v->portamento_control_flag &= 0x7f;
        }

        if (v->portamento_start_delay_counter == 0) {
            if (v->portamento_duration_counter != 0) {
                v->portamento_duration_counter--;
                pb->portamento_add_value = (int16_t)(pb->portamento_add_value + v->portamento_add_value);
            }
        } else {
            v->portamento_start_delay_counter--;
        }
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void bd_setup_sample(BdPlayer* p, const BdFeatures* f, BdVoicePlaybackInfo* pb, BdSample* sample,
                            uint8_t transposed_note, uint8_t play_ticks, int16_t volume,
                            uint16_t volume_slide_speed, int16_t volume_slide_add_value) {
    bd_mute_channel(pb);

    pb->dma_enabled = false;
    pb->sample_number = sample->sample_number;
    pb->sample_data = sample->sample_data;
    pb->sample_length = sample->length;

    pb->playing_sample = sample;
    pb->sample_play_ticks_counter = play_ticks;

    int period_index = -(transposed_note & 0x7f) + sample->note_transpose + FINE_TUNE_START_INDEX;
    pb->note_period = (period_index >= 0 && period_index < (int)FINE_TUNE_COUNT)
                          ? bd_calc_period(period_index, sample->fine_tune_period)
                          : 0;

    pb->sample_portamento_duration = sample->portamento_duration;

    if (pb->sample_portamento_duration >= 0) {
        pb->sample_vibrato_depth = (uint16_t)(sample->vibrato_depth / 2);
        if ((sample->vibrato_depth & 1) != 0) {
            pb->sample_vibrato_depth++;
        }

        pb->sample_portamento_add_value = (int16_t)((sample->portamento_add_value * pb->note_period) / 32768);
        pb->sample_vibrato_add_value = (int16_t)((sample->vibrato_add_value * pb->note_period) / 32768);
    }

    pb->sample_period_add_value = 0;

    pb->handler = (sample->volume_fade_speed == 0) ? BdSampleHandler_PlayOnce : BdSampleHandler_Loop;

    pb->final_volume = volume;

    if (volume > p->playing_info.master_volume) {
        volume = p->playing_info.master_volume;
    }

    if (p->playing_info.master_volume < 0) {
        volume = 0;
    }

    // Volume is set in the hardware register equivalent - handled at render time

    if (f->enable_final_volume_slide) {
        pb->final_volume_slide_speed = volume_slide_speed;
        pb->final_volume_slide_speed_counter = volume_slide_speed;
        pb->final_volume_slide_add_value = volume_slide_add_value;
    }

    pb->loop_delay_counter = 2;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void bd_do_volume_fade(BdPlayer* p, const BdFeatures* f, BdVoiceInfo* v, BdVoicePlaybackInfo* pb) {
    if (v->volume_fade_running && v->volume_fade_duration_counter != 0) {
        v->volume_fade_speed_counter--;

        if (v->volume_fade_speed_counter == 0) {
            v->volume_fade_duration_counter--;
            v->volume_fade_speed_counter = v->volume_fade_speed;

            int volume = v->volume_fade_value + v->volume_fade_add_value;
            v->volume_fade_value = (int16_t)volume;

            volume += v->sample_info2->volume;
            if (volume < 0) {
                v->volume_fade_duration_counter = 0;
            } else {
                if (volume > 64) {
                    volume = 64;
                }

                bd_setup_sample(p, f, pb, v->sample_info2, v->transposed_note, v->volume_fade_speed,
                                (int16_t)volume, 0, 0);
            }
        }
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void bd_do_master_volume_fade(BdPlayer* p, const BdFeatures* f) {
    if (p->playing_info.master_volume_fade_speed != 0) {
        p->playing_info.master_volume_fade_speed_counter--;

        if (p->playing_info.master_volume_fade_speed_counter == 0) {
            p->playing_info.master_volume_fade_speed_counter = p->playing_info.master_volume_fade_speed;

            if (f->master_volume_fade_version == 2) {
                p->playing_info.master_volume_fade_speed_counter--;
            }

            p->playing_info.master_volume--;

            if (p->playing_info.master_volume < 0) {
                p->playing_info.enable_playing = false;

                for (int i = 0; i < 4; i++) {
                    bd_mute_channel(&p->playback[i]);
                }
            }
        }
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Voice effects
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void bd_handle_voice_effects(BdPlayer* p, BdVoicePlaybackInfo* pb, const BdFeatures* f) {
    if (pb->loop_delay_counter != 0) {
        BdSample* sample = pb->playing_sample;

        if (pb->sample_play_ticks_counter > 0) {
            pb->sample_play_ticks_counter--;
        }

        if (f->enable_sample_effects && sample != nullptr) {
            bd_do_sample_effects(pb, sample);
        }

        if (f->enable_final_volume_slide) {
            bd_do_final_volume_slide(pb);
        }

        // Dispatch sample handler
        if (sample != nullptr) {
            switch (pb->handler) {
                case BdSampleHandler_PlayOnce:
                    bd_handle_sample_play_once(pb, sample, f->set_dma_in_sample_handlers);
                    break;
                case BdSampleHandler_Loop:
                    bd_handle_sample_loop(pb, sample, f->set_dma_in_sample_handlers);
                    break;
                case BdSampleHandler_VolumeFade:
                    bd_handle_sample_volume_fade(pb, sample);
                    break;
                default:
                    break;
            }
        }

        int16_t volume = pb->final_volume;
        if (volume > 0) {
            if (volume > p->playing_info.master_volume) {
                volume = p->playing_info.master_volume;
            }

            if (volume < 0) {
                volume = 0;
            }

            // Calculate period with effects
            uint16_t period =
                (uint16_t)(pb->note_period + pb->sample_period_add_value + pb->portamento_add_value);

            bd_set_period(pb, period, p->sample_rate);

            if (!f->set_dma_in_sample_handlers) {
                bd_enable_dma(pb);
            }
        } else {
            bd_mute_channel(pb);
            pb->loop_delay_counter = 0;
        }
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Track parsing
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void bd_parse_track_effect(BdModule* mod, BdVoiceInfo* v, int* position) {
    const BdFeatures* f = &mod->features;
    uint8_t cmd = v->track[(*position)++];

    if (cmd <= f->max_sample_mapping_value) {
        int index = (f->get_sample_mapping_version == 1) ? (cmd & 0x07) : (cmd - 0x80);
        if (index >= 0 && index < 10 && v->sample_mapping[index] < (int)mod->sample_count) {
            v->sample_info = &mod->samples[v->sample_mapping[index]];
        }
    } else if (cmd == 0xff) {
        v->switch_to_next_position = true;
        (*position)--;
    } else if ((f->uses_cx_track_effects && cmd < 0xc0) || (f->uses_9x_track_effects && cmd < 0x9b)) {
        // Set control flag - not used
    } else if ((cmd == 0xc0 && f->uses_cx_track_effects && f->enable_portamento)
               || (cmd == 0x9b && f->uses_9x_track_effects && f->enable_portamento)) {
        v->portamento1_enabled = 255;
        v->portamento2_enabled = false;
        v->portamento_start_delay = v->track[(*position)++];
        v->portamento_duration = v->track[(*position)++];
        v->portamento_delta_note_number = (int8_t)v->track[(*position)++];
    } else if ((cmd == 0xc1 && f->uses_cx_track_effects && f->enable_portamento)
               || (cmd == 0x9c && f->uses_9x_track_effects && f->enable_portamento)) {
        v->portamento1_enabled = 0;
    } else if ((cmd == 0xc2 && f->uses_cx_track_effects && f->enable_volume_fade)
               || (cmd == 0x9d && f->uses_9x_track_effects && f->enable_volume_fade)) {
        v->volume_fade_enabled = true;
        v->volume_fade_init_speed = v->track[(*position)++];
        v->volume_fade_duration = v->track[(*position)++];
        v->volume_fade_init_add_value = (int8_t)v->track[(*position)++];
    } else if ((cmd == 0xc3 && f->uses_cx_track_effects && f->enable_volume_fade)
               || (cmd == 0x9e && f->uses_9x_track_effects && f->enable_volume_fade)) {
        v->volume_fade_enabled = false;
    } else if ((cmd == 0xc4 && f->uses_cx_track_effects && f->enable_portamento)
               || (cmd == 0x9f && f->uses_9x_track_effects && f->enable_portamento)) {
        v->portamento2_enabled = true;
        v->portamento1_enabled = 0;
        v->portamento_duration = v->track[(*position)++];
    } else if ((cmd == 0xc5 && f->uses_cx_track_effects && f->enable_portamento)
               || (cmd == 0xa0 && f->uses_9x_track_effects && f->enable_portamento)) {
        v->portamento2_enabled = false;
    } else if ((cmd == 0xc6 && f->uses_cx_track_effects && f->enable_volume_fade)
               || (cmd == 0xa1 && f->uses_9x_track_effects && f->enable_volume_fade)) {
        v->channel_volume = (uint16_t)((v->track[(*position)++] << 8) | 0xff);

        if (f->enable_final_volume_slide) {
            v->channel_volume_slide_speed = v->track[(*position)++];
            v->channel_volume_slide_add_value = (int8_t)v->track[(*position)++];
        }
    } else if ((cmd == 0xc7 && f->uses_cx_track_effects && f->enable_final_volume_slide)
               || (cmd == 0xa2 && f->uses_9x_track_effects && f->enable_final_volume_slide)) {
        v->channel_volume_slide_speed = 0;
        v->channel_volume = 0xffff;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool bd_take_next_position(BdModule* mod, BdVoiceInfo* v, BdVoicePlaybackInfo* pb) {
    const BdFeatures* f = &mod->features;

    v->current_position = v->next_position;

    int position = v->next_position;
    uint8_t cmd;

    for (;;) {
        if (position < 0 || position >= (int)v->position_list_length) {
            v->channel_enabled = false;
            return true;
        }

        cmd = v->position_list[position++];
        if (cmd < f->max_track_value) {
            break;
        }

        if (cmd == 0xfe) {
            if (position >= (int)v->position_list_length) {
                v->channel_enabled = false;
                return true;
            }
            v->transpose = (int8_t)v->position_list[position++];
        } else if (cmd == 0xff) {
            v->reached_end = true;
            if (pb->loop_delay_counter == 0 || pb->loop_delay_counter == 0x8000) {
                v->channel_enabled = false;
            }
            return true;
        } else if (cmd == 0xfd && f->master_volume_fade_version > 0) {
            if (position >= (int)v->position_list_length) {
                v->channel_enabled = false;
                return true;
            }
            mod->player->playing_info.master_volume_fade_speed = (int8_t)v->position_list[position++];
        } else if (f->enable_f0_track_loop && cmd < 0xf0) {
            v->track_loop_counter = (uint8_t)(cmd - 0xc8);
        } else if (f->enable_c0_track_loop && cmd < 0xc0) {
            v->track_loop_counter = (uint8_t)(cmd & 0x1f);
        } else {
            // Sample mapping
            int index;
            if (f->set_sample_mapping_version == 1) {
                index = cmd & 0x07;
            } else {
                index = cmd - 0xf0;
            }

            if (position >= (int)v->position_list_length) {
                v->channel_enabled = false;
                return true;
            }

            if (index >= 0 && index < 10) {
                v->sample_mapping[index] = (uint8_t)(v->position_list[position++] / 4);
            } else {
                position++;
            }
        }
    }

    v->switch_to_next_position = false;
    v->next_position = position;

    v->playing_track = cmd;

    if (cmd < (int)mod->track_count) {
        v->track = mod->tracks[cmd];
        v->track_length = mod->track_lengths[cmd];
    } else {
        v->track = nullptr;
        v->track_length = 0;
    }

    v->next_track_position = 0;

    return false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool bd_parse_track(BdModule* mod, BdVoiceInfo* v, BdVoicePlaybackInfo* pb) {
    const BdFeatures* f = &mod->features;
    BdPlayer* p = mod->player;

    if (v->track == nullptr) {
        v->channel_enabled = false;
        return false;
    }

    int position = v->next_track_position;

    v->ticks_left_for_next_track_command--;

    if (v->ticks_left_for_next_track_command != 0) {
        if (position < (int)v->track_length && v->track[position] >= 0x80) {
            bd_parse_track_effect(mod, v, &position);
        }

        v->next_track_position = position;
        return false;
    }

    for (;;) {
        if (position >= (int)v->track_length) {
            v->switch_to_next_position = true;
            break;
        }

        if (v->track[position] < 0x80) {
            break;
        }

        bd_parse_track_effect(mod, v, &position);

        if (v->switch_to_next_position) {
            if (f->check_for_ticks && v->ticks_left_for_next_track_command == 0) {
                v->ticks_left_for_next_track_command = 1;
            }
            return true;
        }
    }

    if (position >= (int)v->track_length) {
        v->switch_to_next_position = true;
        v->ticks_left_for_next_track_command = 1;
        v->next_track_position = position;
        return true;
    }

    uint8_t note = v->track[position++];

    if (note == 0x7f) {
        if (position >= (int)v->track_length) {
            v->switch_to_next_position = true;
            v->ticks_left_for_next_track_command = 1;
            v->next_track_position = position;
            return true;
        }

        v->ticks_left_for_next_track_command = v->track[position++];
        v->next_track_position = position;
        return false;
    }

    pb->portamento_add_value = 0;

    note = (uint8_t)(note + v->transpose);
    v->transposed_note = note;

    if (f->enable_portamento) {
        v->portamento_control_flag = v->portamento1_enabled;
        if (v->portamento_control_flag != 0) {
            v->portamento_start_delay_counter = v->portamento_start_delay;
            v->portamento_duration_counter = v->portamento_duration;
            int ft_index = FINE_TUNE_START_INDEX - v->portamento_delta_note_number;
            if (ft_index >= 0 && ft_index < (int)FINE_TUNE_COUNT) {
                v->portamento_add_value = s_fine_tune[ft_index];
            }
        }
    }

    if (f->enable_volume_fade) {
        v->volume_fade_running = v->volume_fade_enabled;
        if (v->volume_fade_running) {
            v->volume_fade_speed = v->volume_fade_init_speed;
            v->volume_fade_speed_counter = v->volume_fade_init_speed;
            v->volume_fade_duration_counter = v->volume_fade_duration;
            v->volume_fade_add_value = v->volume_fade_init_add_value;
            v->volume_fade_value = 0;
        }
    }

    if (position >= (int)v->track_length) {
        v->switch_to_next_position = true;
        v->ticks_left_for_next_track_command = 1;
        v->next_track_position = position;
        return true;
    }

    uint8_t ticks = v->track[position++];
    if (f->extra_tick_arg && ticks == 0) {
        if (position >= (int)v->track_length) {
            v->switch_to_next_position = true;
            v->ticks_left_for_next_track_command = 1;
            v->next_track_position = position;
            return true;
        }
        v->ticks_left_for_next_track_command = v->track[position++];
        ticks = 0xff;
    } else {
        v->ticks_left_for_next_track_command = ticks;
    }

    if (f->enable_portamento && v->portamento2_enabled) {
        v->portamento_control_flag = 0xff;
        v->portamento_start_delay_counter = 0;
        v->portamento_duration_counter = v->portamento_duration;

        uint8_t note1 = note;
        if (!v->use_new_note) {
            note = v->previous_transposed_note;
        }

        v->transposed_note = note;
        int ft_index = FINE_TUNE_START_INDEX - (note1 - note);
        if (ft_index >= 0 && ft_index < (int)FINE_TUNE_COUNT) {
            v->portamento_add_value = s_fine_tune[ft_index];
        }
    }

    v->previous_transposed_note = v->transposed_note;
    v->next_track_position = position;
    v->use_new_note = false;

    BdSample* sample = v->sample_info;
    v->sample_info2 = sample;

    if (sample == nullptr) {
        return false;
    }

    int16_t volume = f->enable_volume_fade ? (int16_t)((sample->volume * v->channel_volume) / 16384)
                                           : (int16_t)sample->volume;

    bd_setup_sample(p, f, pb, sample, note, ticks, volume, v->channel_volume_slide_speed,
                    v->channel_volume_slide_add_value);

    return false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void bd_handle_voice(BdModule* mod, BdVoiceInfo* v, BdVoicePlaybackInfo* pb) {
    const BdFeatures* f = &mod->features;
    BdPlayer* p = mod->player;

    if (!v->channel_enabled) {
        return;
    }

    if (f->enable_portamento) {
        bd_do_portamento(v, pb);
    }

    if (f->enable_volume_fade) {
        bd_do_volume_fade(p, f, v, pb);
    }

    for (;;) {
        if (v->switch_to_next_position) {
            v->track_loop_counter--;

            if (v->track_loop_counter == 0) {
                v->track_loop_counter = 1;

                if (bd_take_next_position(mod, v, pb)) {
                    return;
                }
            } else {
                v->next_track_position = 0;
                v->switch_to_next_position = false;
            }
        }

        if (!bd_parse_track(mod, v, pb)) {
            break;
        }
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Player tick
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void bd_player_tick(BdModule* mod) {
    BdPlayer* p = mod->player;
    const BdFeatures* f = &mod->features;

    // Handle effects first
    if (f->master_volume_fade_version > 0) {
        bd_do_master_volume_fade(p, f);
    }

    for (int i = 0; i < 4; i++) {
        bd_handle_voice_effects(p, &p->playback[i], f);
    }

    // Counter feature (subdivide ticks)
    if (f->enable_counter) {
        p->playing_info.counter--;
        if (p->playing_info.counter == 0) {
            p->playing_info.counter = 6;
            return;
        }
    }

    if (!p->playing_info.enable_playing) {
        p->playing_info.enable_playing = true;

        // Restart song (matches C# RestartSong behavior)
        bd_init_sound(mod, p->current_subsong);

        for (int i = 0; i < 4; i++) {
            bd_mute_channel(&p->playback[i]);
        }

        return;
    }

    p->playing_info.enable_playing = p->voices[0].channel_enabled || p->voices[1].channel_enabled
                                     || p->voices[2].channel_enabled || p->voices[3].channel_enabled;

    if (p->playing_info.enable_playing) {
        for (int i = 0; i < 4; i++) {
            bd_handle_voice(mod, &p->voices[i], &p->playback[i]);
        }
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Render
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static size_t bd_player_render(BdModule* mod, float* interleaved_stereo, size_t frames) {
    BdPlayer* p = mod->player;

    if (p == nullptr || interleaved_stereo == nullptr) {
        return 0;
    }

    for (size_t frame = 0; frame < frames; frame++) {
        if (p->frames_until_tick <= 0.0f) {
            bd_player_tick(mod);
            p->frames_until_tick += p->frames_per_tick;
        }

        float left = 0.0f;
        float right = 0.0f;

        for (int ch = 0; ch < 4; ch++) {
            if ((p->channel_mask & (1U << ch)) == 0) {
                continue;
            }

            BdVoicePlaybackInfo* pb = &p->playback[ch];
            float sample = bd_voice_sample(pb);

            int16_t vol = pb->final_volume;
            if (vol > p->playing_info.master_volume) {
                vol = p->playing_info.master_volume;
            }
            if (vol < 0) {
                vol = 0;
            }

            // NostalgicPlayer mixer applies 0.5x gain per channel
            float gain = (float)vol / 128.0f;
            float out = sample * gain;

            // Amiga panning: 0,3=left; 1,2=right
            if (ch == 0 || ch == 3) {
                left += out;
            } else {
                right += out;
            }

            bd_voice_advance(pb);
        }

        // Soft clamp
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

        p->frames_until_tick -= 1.0f;
    }

    return frames;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Module cleanup
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void bd_module_cleanup(BdModule* mod) {
    if (mod->sub_songs != nullptr) {
        free(mod->sub_songs);
        mod->sub_songs = nullptr;
    }

    if (mod->position_lists != nullptr) {
        for (size_t i = 0; i < mod->position_list_count; i++) {
            free(mod->position_lists[i].data);
        }
        free(mod->position_lists);
        mod->position_lists = nullptr;
    }

    if (mod->tracks != nullptr) {
        for (size_t i = 0; i < mod->track_count; i++) {
            free(mod->tracks[i]);
        }
        free(mod->tracks);
        mod->tracks = nullptr;
    }

    if (mod->track_lengths != nullptr) {
        free(mod->track_lengths);
        mod->track_lengths = nullptr;
    }

    if (mod->samples != nullptr) {
        for (size_t i = 0; i < mod->sample_count; i++) {
            free(mod->samples[i].sample_data);
        }
        free(mod->samples);
        mod->samples = nullptr;
    }

    if (mod->file_data != nullptr) {
        free(mod->file_data);
        mod->file_data = nullptr;
    }

    if (mod->player != nullptr) {
        free(mod->player);
        mod->player = nullptr;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Public API
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

BdModule* bd_create(const uint8_t* data, size_t size, float sample_rate) {
    if (data == nullptr || size == 0 || sample_rate <= 0.0f) {
        return nullptr;
    }

    BdModule* mod = (BdModule*)calloc(1, sizeof(BdModule));
    if (mod == nullptr) {
        return nullptr;
    }

    // Test module format and extract offsets + features
    if (!bd_test_module(data, size, &mod->sub_song_list_offset, &mod->track_offset_table_offset, &mod->tracks_offset,
                        &mod->sample_info_offset_table_offset, &mod->features)) {
        free(mod);
        return nullptr;
    }

    // Keep a copy of the file data
    mod->file_data = (uint8_t*)malloc(size);
    if (mod->file_data == nullptr) {
        free(mod);
        return nullptr;
    }
    memcpy(mod->file_data, data, size);
    mod->file_size = size;

    // Load all data structures
    if (!bd_load_sub_song_info(mod)) {
        bd_module_cleanup(mod);
        free(mod);
        return nullptr;
    }

    if (!bd_load_position_lists(mod)) {
        bd_module_cleanup(mod);
        free(mod);
        return nullptr;
    }

    if (!bd_load_tracks(mod)) {
        bd_module_cleanup(mod);
        free(mod);
        return nullptr;
    }

    if (!bd_load_sample_info(mod)) {
        bd_module_cleanup(mod);
        free(mod);
        return nullptr;
    }

    // Create player
    mod->player = (BdPlayer*)calloc(1, sizeof(BdPlayer));
    if (mod->player == nullptr) {
        bd_module_cleanup(mod);
        free(mod);
        return nullptr;
    }

    mod->player->sample_rate = (uint32_t)sample_rate;
    mod->player->channel_mask = 0x0f;
    mod->player->frames_per_tick = sample_rate / 50.0f;  // 50 Hz PAL VBlank
    mod->player->frames_until_tick = 0.0f;
    mod->player->current_subsong = 0;

    bd_init_sound(mod, 0);

    return mod;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void bd_destroy(BdModule* module) {
    if (module == nullptr) {
        return;
    }

    bd_module_cleanup(module);
    free(module);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int bd_subsong_count(const BdModule* module) {
    if (module == nullptr) {
        return 0;
    }

    return (int)module->sub_song_count;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool bd_select_subsong(BdModule* module, int subsong) {
    if (module == nullptr || module->player == nullptr) {
        return false;
    }

    if (subsong < 0 || subsong >= (int)module->sub_song_count) {
        return false;
    }

    module->player->current_subsong = subsong;
    module->player->frames_until_tick = 0.0f;
    bd_init_sound(module, subsong);
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int bd_channel_count(const BdModule* module) {
    (void)module;
    return 4;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void bd_set_channel_mask(BdModule* module, uint32_t mask) {
    if (module == nullptr || module->player == nullptr) {
        return;
    }

    module->player->channel_mask = mask;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

size_t bd_render(BdModule* module, float* interleaved_stereo, size_t frames) {
    if (module == nullptr || module->player == nullptr) {
        return 0;
    }

    return bd_player_render(module, interleaved_stereo, frames);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool bd_has_ended(const BdModule* module) {
    if (module == nullptr || module->player == nullptr) {
        return true;
    }

    // Song has ended when all 4 channels have reached the 0xff end marker
    // in their position lists. This is independent of loop_delay_counter state.
    const BdPlayer* p = module->player;
    return p->voices[0].reached_end && p->voices[1].reached_end
           && p->voices[2].reached_end && p->voices[3].reached_end;
}
