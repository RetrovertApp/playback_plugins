///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// xSF QSF Wrapper - Capcom QSound emulation via highly_quixotic
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void* xsf_qsf_create(void);
int xsf_qsf_load(void* context, const uint8_t* exe, size_t exe_size, const uint8_t* reserved, size_t reserved_size);
int xsf_qsf_start(void* state, int psf_version);
int xsf_qsf_post_load(void* state);
int xsf_qsf_render(void* state, int16_t* buffer, int frames);
int xsf_qsf_sample_rate(void* state);
int xsf_qsf_seek_reset(void* state);
void xsf_qsf_destroy(void* state);

#ifdef __cplusplus
}
#endif
