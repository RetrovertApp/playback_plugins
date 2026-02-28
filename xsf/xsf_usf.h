///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// xSF USF Wrapper - Nintendo 64 emulation via lazyusf2
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void* xsf_usf_create(void);
int xsf_usf_load(void* context, const uint8_t* exe, size_t exe_size, const uint8_t* reserved, size_t reserved_size);
int xsf_usf_start(void* state, int psf_version);
int xsf_usf_render(void* state, int16_t* buffer, int frames);
int xsf_usf_sample_rate(void* state);
int xsf_usf_seek_reset(void* state);
void xsf_usf_destroy(void* state);
int xsf_usf_info(void* state, const char* name, const char* value);

#ifdef __cplusplus
}
#endif
