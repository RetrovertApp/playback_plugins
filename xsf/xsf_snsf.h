///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// xSF SNSF Wrapper - Super Nintendo emulation via snsf9x
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void* xsf_snsf_create(void);
int xsf_snsf_load(void* context, const uint8_t* exe, size_t exe_size, const uint8_t* reserved, size_t reserved_size);
int xsf_snsf_start(void* state, int psf_version);
int xsf_snsf_post_load(void* state);
int xsf_snsf_render(void* state, int16_t* buffer, int frames);
int xsf_snsf_sample_rate(void* state);
int xsf_snsf_seek_reset(void* state);
void xsf_snsf_destroy(void* state);

#ifdef __cplusplus
}
#endif
