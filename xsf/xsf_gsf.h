///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// xSF GSF Wrapper - Game Boy Advance emulation via viogsf
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void* xsf_gsf_create(void);
int xsf_gsf_load(void* context, const uint8_t* exe, size_t exe_size, const uint8_t* reserved, size_t reserved_size);
int xsf_gsf_start(void* state, int psf_version);
int xsf_gsf_post_load(void* state);
int xsf_gsf_render(void* state, int16_t* buffer, int frames);
int xsf_gsf_sample_rate(void* state);
int xsf_gsf_seek_reset(void* state);
void xsf_gsf_destroy(void* state);

#ifdef __cplusplus
}
#endif
