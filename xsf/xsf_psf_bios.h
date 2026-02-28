///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Embedded PSX BIOS for PSF playback via highly_experimental
//
// The HE BIOS is a stripped-down PS2 BIOS containing only the IOP modules
// needed for PSF/PSF2 playback (~512KB). It was pre-generated using the
// mkhebios tool from the highly_experimental library.
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get pointer to the embedded HE BIOS data and its size in bytes.
// Returns non-null pointer to static data. Size is written to *out_size.

const uint8_t* xsf_psf_get_embedded_bios(uint32_t* out_size);

#ifdef __cplusplus
}
#endif
