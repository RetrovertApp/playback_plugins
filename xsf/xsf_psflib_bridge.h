///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// xSF psflib Bridge - In-memory file I/O adapter for psflib callbacks
//
// psflib expects file-based I/O (fopen/fread/fseek/fclose/ftell). Our IO API provides
// RVIo_read_url_to_memory(). This bridge loads files into memory via the IO API and presents
// them as file-like handles to psflib.
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <retrovert/io.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Context passed to psflib, holds base directory + IO API pointer

typedef struct XsfFileContext {
    const RVIo* io_api;
    char base_dir[2048]; // Directory of the original PSF file (for resolving library refs)
} XsfFileContext;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Initialize file context from a URL (extracts base directory)

void xsf_file_context_init(XsfFileContext* ctx, const RVIo* io_api, const char* url);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// psflib file callback functions (match psf_file_callbacks signature)

void* xsf_fopen(void* context, const char* uri);
size_t xsf_fread(void* buffer, size_t size, size_t count, void* handle);
int xsf_fseek(void* handle, int64_t offset, int whence);
int xsf_fclose(void* handle);
long xsf_ftell(void* handle);

#ifdef __cplusplus
}
#endif
