# Retrovert Playback Plugins
#
# Standalone build for all Retrovert playback plugins.
# These plugins implement the RVPlaybackPlugin interface (include/retrovert/playback.h)
# and can be loaded dynamically by any host application.
#
# Usage:
#   cmake -B build -G Ninja
#   cmake --build build
#
# Or when included as a subdirectory from a host project:
#   set(RETROVERT_PLUGIN_OUTPUT_DIR "${CMAKE_BINARY_DIR}/plugins/playback")
#   add_subdirectory(path/to/retrovert_playback_plugins)

cmake_minimum_required(VERSION 3.16)

# Prefer Clang over GCC when available (must be set before project())
# Many third-party libraries have issues with GCC 15+ C23 strictness
if(WIN32 AND NOT DEFINED CMAKE_C_COMPILER AND NOT DEFINED ENV{CC})
    if(CMAKE_GENERATOR MATCHES "Visual Studio")
        set(CMAKE_GENERATOR_TOOLSET "ClangCL" CACHE STRING "Use clang-cl toolset" FORCE)
    else()
        find_program(CLANG_CL_FOUND clang-cl)
        if(CLANG_CL_FOUND)
            set(CMAKE_C_COMPILER clang-cl)
            set(CMAKE_CXX_COMPILER clang-cl)
        endif()
    endif()
elseif(NOT WIN32 AND NOT DEFINED CMAKE_C_COMPILER AND NOT DEFINED ENV{CC})
    find_program(CLANG_FOUND clang)
    if(CLANG_FOUND)
        set(CMAKE_C_COMPILER clang)
        find_program(CLANGXX_FOUND clang++)
        if(CLANGXX_FOUND)
            set(CMAKE_CXX_COMPILER clang++)
        endif()
    endif()
endif()

project(retrovert_playback_plugins C CXX)

# Include helper modules
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(DependencyHelpers)

# Retrovert API headers - the contract between host and plugins
set(RETROVERT_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include" CACHE PATH "Retrovert API headers")

# Output directory for built plugins
if(NOT RETROVERT_PLUGIN_OUTPUT_DIR)
    set(RETROVERT_PLUGIN_OUTPUT_DIR "${CMAKE_BINARY_DIR}/plugins")
endif()

# Plugin file suffix
if(WIN32)
    set(PLAYBACK_PLUGIN_SUFFIX ".dll")
else()
    set(PLAYBACK_PLUGIN_SUFFIX ".so")
endif()

# Sanitizer support
option(ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(ENABLE_TSAN "Enable ThreadSanitizer" OFF)
option(ENABLE_FUZZ "Build fuzz targets (requires Clang)" OFF)

if(ENABLE_ASAN)
    set(ASAN_FLAGS -fsanitize=address -fno-omit-frame-pointer)
endif()
if(ENABLE_TSAN)
    set(TSAN_FLAGS -fsanitize=thread)
endif()

# Common dependencies used by multiple plugins
find_package(ZLIB REQUIRED)

# Compiler warnings for our own code (not third-party)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    set(WARNING_FLAGS -Wall -Wextra -Wshadow)
endif()

# Helper: apply standard config to a plugin target
function(apply_target_config target_name)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target_name} PRIVATE ${WARNING_FLAGS})
    endif()

    # Error on undefined symbols for shared libraries (catches missing symbols at build time)
    # Skip when sanitizers are enabled: sanitizer runtime symbols are resolved at load time
    get_target_property(target_type ${target_name} TYPE)
    if(target_type STREQUAL "SHARED_LIBRARY" AND NOT ENABLE_ASAN AND NOT ENABLE_TSAN)
        if(UNIX AND NOT APPLE)
            target_link_options(${target_name} PRIVATE -Wl,--no-undefined)
        elseif(APPLE)
            target_link_options(${target_name} PRIVATE -Wl,-undefined,error)
        endif()
    endif()

    if(ENABLE_TSAN AND TSAN_FLAGS)
        target_compile_options(${target_name} PRIVATE ${TSAN_FLAGS})
        target_link_options(${target_name} PRIVATE ${TSAN_FLAGS})
    endif()
    if(ENABLE_ASAN AND ASAN_FLAGS)
        target_compile_options(${target_name} PRIVATE ${ASAN_FLAGS})
        target_link_options(${target_name} PRIVATE ${ASAN_FLAGS})
    endif()
endfunction()

# Build all playback plugins
add_subdirectory(plugins/adplug)
add_subdirectory(plugins/art_of_noise)
add_subdirectory(plugins/asap)
add_subdirectory(plugins/audio_stream)
add_subdirectory(plugins/cpsycle)
add_subdirectory(plugins/eupmini)
add_subdirectory(plugins/fmplayer)
add_subdirectory(plugins/furnace)
add_subdirectory(plugins/gme)
add_subdirectory(plugins/hively)
add_subdirectory(plugins/ixalance)
add_subdirectory(plugins/klystrack)
add_subdirectory(plugins/libkss)
add_subdirectory(plugins/libvgm)
add_subdirectory(plugins/mdxmini)
add_subdirectory(plugins/openmpt)
add_subdirectory(plugins/organya)
add_subdirectory(plugins/pmdmini)
add_subdirectory(plugins/pxtone)
add_subdirectory(plugins/sc68)
add_subdirectory(plugins/sidplayfp)
add_subdirectory(plugins/spu)
add_subdirectory(plugins/sunvox)
add_subdirectory(plugins/tfmx)
add_subdirectory(plugins/uade)
add_subdirectory(plugins/v2m)
add_subdirectory(plugins/xsf)
add_subdirectory(plugins/zxtune)

# Fuzz targets (only when explicitly enabled)
if(ENABLE_FUZZ)
    add_subdirectory(fuzz)
endif()
