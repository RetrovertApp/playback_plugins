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
add_subdirectory(adplug)
add_subdirectory(art_of_noise)
add_subdirectory(asap)
add_subdirectory(audio_stream)
add_subdirectory(cpsycle)
add_subdirectory(eupmini)
add_subdirectory(fmplayer)
add_subdirectory(furnace)
add_subdirectory(gme)
add_subdirectory(hively)
add_subdirectory(ixalance)
add_subdirectory(klystrack)
add_subdirectory(libkss)
add_subdirectory(libvgm)
add_subdirectory(mdxmini)
add_subdirectory(openmpt)
add_subdirectory(organya)
add_subdirectory(pmdmini)
add_subdirectory(pxtone)
add_subdirectory(sc68)
add_subdirectory(sidplayfp)
add_subdirectory(spu)
add_subdirectory(sunvox)
add_subdirectory(tfmx)
add_subdirectory(uade)
add_subdirectory(v2m)
add_subdirectory(xsf)
add_subdirectory(zxtune)
