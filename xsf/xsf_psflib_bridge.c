///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// xSF psflib Bridge - In-memory file I/O adapter for psflib callbacks
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// C11 nullptr compatibility
#ifndef nullptr
#define nullptr ((void*)0)
#endif

#include "xsf_psflib_bridge.h"

#include <retrovert/log.h>

#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
#else
#include <strings.h>
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// In-memory file handle for psflib callbacks

typedef struct XsfMemFile {
    uint8_t* data;
    size_t size;
    size_t pos;
    const RVIo* io_api; // Needed to free the data
} XsfMemFile;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Extract directory from a URL path (everything up to and including the last '/')

static void extract_base_dir(const char* url, char* base_dir, size_t base_dir_size) {
    if (url == nullptr || base_dir == nullptr || base_dir_size == 0) {
        if (base_dir && base_dir_size > 0) {
            base_dir[0] = '\0';
        }
        return;
    }

    size_t url_len = strlen(url);
    const char* last_sep = nullptr;

    // Find the last path separator (/ or \)
    for (size_t i = url_len; i > 0; i--) {
        if (url[i - 1] == '/' || url[i - 1] == '\\') {
            last_sep = &url[i - 1];
            break;
        }
    }

    if (last_sep != nullptr) {
        size_t dir_len = (size_t)(last_sep - url) + 1; // Include the separator
        if (dir_len >= base_dir_size) {
            dir_len = base_dir_size - 1;
        }
        memcpy(base_dir, url, dir_len);
        base_dir[dir_len] = '\0';
    } else {
        base_dir[0] = '\0';
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void xsf_file_context_init(XsfFileContext* ctx, const RVIo* io_api, const char* url) {
    ctx->io_api = io_api;
    extract_base_dir(url, ctx->base_dir, sizeof(ctx->base_dir));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Resolve a relative path against the base directory

static void resolve_path(const XsfFileContext* ctx, const char* uri, char* resolved, size_t resolved_size) {
    // If uri is already absolute (starts with / or contains ://) use it directly
    if (uri[0] == '/' || strstr(uri, "://") != nullptr) {
        size_t len = strlen(uri);
        if (len >= resolved_size) {
            len = resolved_size - 1;
        }
        memcpy(resolved, uri, len);
        resolved[len] = '\0';
        return;
    }

    // Concatenate base_dir + uri
    size_t base_len = strlen(ctx->base_dir);
    size_t uri_len = strlen(uri);

    if (base_len + uri_len >= resolved_size) {
        // Truncate if too long (shouldn't happen with reasonable paths)
        if (base_len >= resolved_size) {
            base_len = resolved_size - 1;
            uri_len = 0;
        } else {
            uri_len = resolved_size - base_len - 1;
        }
    }

    memcpy(resolved, ctx->base_dir, base_len);
    memcpy(resolved + base_len, uri, uri_len);
    resolved[base_len + uri_len] = '\0';
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void* xsf_fopen(void* context, const char* uri) {
    XsfFileContext* ctx = (XsfFileContext*)context;

    // psflib already resolves paths internally (base_path + filename) before calling
    // fopen, so the URI is already a full path. Open it directly.
    rv_debug("xSF fopen: '%s'", uri);
    RVIoReadUrlResult result = RVIo_read_url_to_memory(ctx->io_api, uri);
    if (result.data == nullptr) {
        rv_error("xSF fopen: failed to open '%s'", uri);
        return nullptr;
    }

    // Create in-memory file handle
    XsfMemFile* mem_file = (XsfMemFile*)malloc(sizeof(XsfMemFile));
    if (mem_file == nullptr) {
        RVIo_free_url_to_memory(ctx->io_api, result.data);
        return nullptr;
    }

    mem_file->data = result.data;
    mem_file->size = (size_t)result.data_size;
    mem_file->pos = 0;
    mem_file->io_api = ctx->io_api;

    return mem_file;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

size_t xsf_fread(void* buffer, size_t size, size_t count, void* handle) {
    XsfMemFile* mem_file = (XsfMemFile*)handle;
    if (mem_file == nullptr || mem_file->data == nullptr) {
        return 0;
    }

    size_t total_bytes = size * count;
    size_t available = mem_file->size - mem_file->pos;

    if (total_bytes > available) {
        total_bytes = available;
    }

    // Round down to whole items
    size_t items_to_read = (size > 0) ? (total_bytes / size) : 0;
    size_t bytes_to_copy = items_to_read * size;

    if (bytes_to_copy > 0) {
        memcpy(buffer, mem_file->data + mem_file->pos, bytes_to_copy);
        mem_file->pos += bytes_to_copy;
    }

    return items_to_read;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int xsf_fseek(void* handle, int64_t offset, int whence) {
    XsfMemFile* mem_file = (XsfMemFile*)handle;
    if (mem_file == nullptr) {
        return -1;
    }

    int64_t new_pos;
    switch (whence) {
        case 0: // SEEK_SET
            new_pos = offset;
            break;
        case 1: // SEEK_CUR
            new_pos = (int64_t)mem_file->pos + offset;
            break;
        case 2: // SEEK_END
            new_pos = (int64_t)mem_file->size + offset;
            break;
        default:
            return -1;
    }

    if (new_pos < 0 || new_pos > (int64_t)mem_file->size) {
        return -1;
    }

    mem_file->pos = (size_t)new_pos;
    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int xsf_fclose(void* handle) {
    XsfMemFile* mem_file = (XsfMemFile*)handle;
    if (mem_file == nullptr) {
        return -1;
    }

    if (mem_file->data != nullptr) {
        RVIo_free_url_to_memory(mem_file->io_api, mem_file->data);
    }

    free(mem_file);
    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

long xsf_ftell(void* handle) {
    XsfMemFile* mem_file = (XsfMemFile*)handle;
    if (mem_file == nullptr) {
        return -1;
    }

    return (long)mem_file->pos;
}
