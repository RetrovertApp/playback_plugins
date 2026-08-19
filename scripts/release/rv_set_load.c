// rv_set_load — whole-set load check for a gathered plugin generation.
//
// Loads every module given on the command line into one process
// simultaneously, resolves and validates each plugin's identity block, and
// only then unloads — what a catalog activation actually does, and the one
// property the per-artifact load smoke cannot see (symbol clashes and
// static-state conflicts between plugins). Built against the harness-pinned
// ABI headers, so the api_version comparison doubles as the runtime ABI
// cross-check. Exit code 0 means the set coexists.
//
// Usage: rv_set_load <module> [<module>...]

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <retrovert/playback.h>

#ifdef _WIN32
#include <windows.h>
typedef HMODULE ModuleHandle;
#else
#include <dlfcn.h>
typedef void* ModuleHandle;
#endif

typedef RVPlaybackPlugin* (*PluginEntry)(void);

static ModuleHandle module_open(const char* path) {
#ifdef _WIN32
    wchar_t wide[MAX_PATH];
    if (MultiByteToWideChar(CP_UTF8, 0, path, -1, wide, MAX_PATH) == 0) {
        return NULL;
    }
    // The safe-loading policy the consumers use: module dir + System32 only.
    return LoadLibraryExW(wide, NULL, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
#else
    return dlopen(path, RTLD_NOW | RTLD_LOCAL);
#endif
}

static void module_close(ModuleHandle handle) {
#ifdef _WIN32
    FreeLibrary(handle);
#else
    dlclose(handle);
#endif
}

static PluginEntry module_entry(ModuleHandle handle) {
#ifdef _WIN32
    return (PluginEntry)(void*)GetProcAddress(handle, "rv_playback_plugin");
#else
    // Object-to-function cast required by the dlsym API.
    union {
        void* obj;
        PluginEntry fn;
    } cast;
    cast.obj = dlsym(handle, "rv_playback_plugin");
    return cast.fn;
#endif
}

static void print_load_error(const char* path) {
#ifdef _WIN32
    fprintf(stderr, "rv_set_load: failed to load %s (error %lu)\n", path, GetLastError());
#else
    fprintf(stderr, "rv_set_load: failed to load %s: %s\n", path, dlerror());
#endif
}

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: rv_set_load <module> [<module>...]\n");
        return 2;
    }
    int count = argc - 1;
    ModuleHandle* handles = calloc((size_t)count, sizeof(ModuleHandle));
    const char** names = calloc((size_t)count, sizeof(char*));
    if (!handles || !names) {
        fprintf(stderr, "rv_set_load: out of memory\n");
        return 1;
    }

    int status = 0;
    int loaded = 0;
    for (; loaded < count; ++loaded) {
        const char* path = argv[loaded + 1];
        handles[loaded] = module_open(path);
        if (!handles[loaded]) {
            print_load_error(path);
            status = 1;
            break;
        }
        PluginEntry entry = module_entry(handles[loaded]);
        if (!entry) {
            fprintf(stderr, "rv_set_load: %s does not export rv_playback_plugin\n", path);
            status = 1;
            ++loaded;
            break;
        }
        RVPlaybackPlugin* plugin = entry();
        if (!plugin) {
            fprintf(stderr, "rv_set_load: %s: rv_playback_plugin() returned NULL\n", path);
            status = 1;
            ++loaded;
            break;
        }
        if (plugin->api_version != RV_PLAYBACK_PLUGIN_API_VERSION) {
            fprintf(stderr, "rv_set_load: %s reports api_version %llu, host expects %d\n", path,
                    (unsigned long long)plugin->api_version, RV_PLAYBACK_PLUGIN_API_VERSION);
            status = 1;
            ++loaded;
            break;
        }
        if (!plugin->name || !plugin->probe_can_play || !plugin->create || !plugin->destroy || !plugin->open ||
            !plugin->close || !plugin->read_data) {
            fprintf(stderr, "rv_set_load: %s is missing mandatory ABI entries\n", path);
            status = 1;
            ++loaded;
            break;
        }
        for (int i = 0; i < loaded; ++i) {
            if (strcmp(names[i], plugin->name) == 0) {
                fprintf(stderr, "rv_set_load: duplicate plugin name '%s'\n", plugin->name);
                status = 1;
            }
        }
        names[loaded] = plugin->name;
        fprintf(stderr, "rv_set_load: loaded '%s' api %llu\n", plugin->name,
                (unsigned long long)plugin->api_version);
        if (status != 0) {
            ++loaded;
            break;
        }
    }

    if (status == 0) {
        fprintf(stderr, "rv_set_load: %d plugins coexist in one process\n", count);
    }
    for (int i = loaded - 1; i >= 0; --i) {
        module_close(handles[i]);
    }
    free(names);
    free(handles);
    return status;
}
