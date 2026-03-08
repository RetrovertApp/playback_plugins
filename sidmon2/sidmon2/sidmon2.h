// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Daniel Collin
// Ported from NostalgicPlayer C# implementation by Thomas Neumann (MIT)

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Sd2Module Sd2Module;

Sd2Module* sd2_create(const uint8_t* data, size_t size, float sample_rate);
void sd2_destroy(Sd2Module* module);

int sd2_subsong_count(const Sd2Module* module);
bool sd2_select_subsong(Sd2Module* module, int subsong);

int sd2_channel_count(const Sd2Module* module);
void sd2_set_channel_mask(Sd2Module* module, uint32_t mask);

size_t sd2_render(Sd2Module* module, float* interleaved_stereo, size_t frames);
bool sd2_has_ended(const Sd2Module* module);

#ifdef __cplusplus
}
#endif
