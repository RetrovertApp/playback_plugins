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

typedef struct MaModule MaModule;

MaModule* ma_create(const uint8_t* data, size_t size, float sample_rate);
void ma_destroy(MaModule* module);

int ma_subsong_count(const MaModule* module);
bool ma_select_subsong(MaModule* module, int subsong);

size_t ma_render(MaModule* module, float* interleaved_stereo, size_t frames);
bool ma_has_ended(const MaModule* module);

#ifdef __cplusplus
}
#endif
