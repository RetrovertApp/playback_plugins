///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// xSF SSF/DSF Wrapper - Sega Saturn/Dreamcast emulation via highly_theoretical
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void* xsf_ssf_create(void);
int xsf_ssf_load(void* context, const uint8_t* exe, size_t exe_size, const uint8_t* reserved, size_t reserved_size);
int xsf_ssf_start(void* state, int psf_version);
int xsf_ssf_post_load(void* state);
int xsf_ssf_render(void* state, int16_t* buffer, int frames);
int xsf_ssf_sample_rate(void* state);
int xsf_ssf_seek_reset(void* state);
void xsf_ssf_destroy(void* state);

#ifdef __cplusplus
}
#endif
