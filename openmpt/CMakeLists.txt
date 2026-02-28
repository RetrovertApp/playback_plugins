# OpenMPT Playback Plugin
#
# This plugin uses libopenmpt to decode tracker music formats (MOD, XM, S3M, IT, etc.)
# The plugin implements the RVPlaybackPlugin interface.
#
# Based on libopenmpt autotools build

cmake_minimum_required(VERSION 3.16)

enable_language(CXX)

# Download libopenmpt 0.8.4 (with scope capture patch for audio visualization)
download_library(
    NAME openmpt
    URL "http://lib.openmpt.org/files/libopenmpt/src/libopenmpt-0.8.4+release.autotools.tar.gz"
    FILENAME "libopenmpt-0.8.4.tar.gz"
    HASH "627f9bf11aacae615a1f2c982c7e88cb21f11b2d6f0267946f7c82c5eae4943b"
    EXTRACT_DIR "libopenmpt-0.8.4+release.autotools"
    PATCHES patches/scope-capture.patch
)

set(LIBOPENMPT_ROOT "${openmpt_SOURCE_DIR}")

# Collect sources matching autotools Makefile.am
file(GLOB LIBOPENMPT_COMMON_SOURCES "${LIBOPENMPT_ROOT}/common/*.cpp")
file(GLOB LIBOPENMPT_SOUNDDSP_SOURCES "${LIBOPENMPT_ROOT}/sounddsp/*.cpp")
file(GLOB LIBOPENMPT_SOUNDLIB_SOURCES "${LIBOPENMPT_ROOT}/soundlib/*.cpp")
file(GLOB LIBOPENMPT_SOUNDLIB_PLUGIN_SOURCES "${LIBOPENMPT_ROOT}/soundlib/plugins/*.cpp")
file(GLOB LIBOPENMPT_SOUNDLIB_PLUGIN_DMO_SOURCES "${LIBOPENMPT_ROOT}/soundlib/plugins/dmo/*.cpp")

set(LIBOPENMPT_CORE_SOURCES
    "${LIBOPENMPT_ROOT}/libopenmpt/libopenmpt_c.cpp"
    "${LIBOPENMPT_ROOT}/libopenmpt/libopenmpt_cxx.cpp"
    "${LIBOPENMPT_ROOT}/libopenmpt/libopenmpt_impl.cpp"
    "${LIBOPENMPT_ROOT}/libopenmpt/libopenmpt_ext_impl.cpp"
)

set(output_dir "${RETROVERT_PLUGIN_OUTPUT_DIR}")

add_library(openmpt_playback SHARED
    openmpt_plugin.cpp
    ${LIBOPENMPT_COMMON_SOURCES}
    ${LIBOPENMPT_SOUNDDSP_SOURCES}
    ${LIBOPENMPT_SOUNDLIB_SOURCES}
    ${LIBOPENMPT_SOUNDLIB_PLUGIN_SOURCES}
    ${LIBOPENMPT_SOUNDLIB_PLUGIN_DMO_SOURCES}
    ${LIBOPENMPT_CORE_SOURCES}
)

set_target_properties(openmpt_playback PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

# Include directories matching autotools: -I./ -I./src -I./common
target_include_directories(openmpt_playback PRIVATE
    ${RETROVERT_INCLUDE_DIR}
    ${LIBOPENMPT_ROOT}
    ${LIBOPENMPT_ROOT}/src
    ${LIBOPENMPT_ROOT}/common
)

# Compile definitions matching autotools
target_compile_definitions(openmpt_playback PRIVATE
    LIBOPENMPT_BUILD
)

# Windows-specific definitions
if(WIN32)
    target_compile_definitions(openmpt_playback PRIVATE
        UNICODE
        _UNICODE
    )
endif()

# Force include stdafx.h to provide precompiled header types (int32, int64, etc.)
# libopenmpt expects stdafx.h to be implicitly included before all source files
# Use /FI for MSVC/clang-cl, -include for GCC/Clang
if(MSVC)
    target_compile_options(openmpt_playback PRIVATE
        "/FI${LIBOPENMPT_ROOT}/common/stdafx.h"
    )
else()
    target_compile_options(openmpt_playback PRIVATE
        -include "${LIBOPENMPT_ROOT}/common/stdafx.h"
    )
endif()
suppress_external_warnings(openmpt_playback)

set_target_properties(openmpt_playback PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${output_dir}"
    RUNTIME_OUTPUT_DIRECTORY "${output_dir}"
    PREFIX ""
    SUFFIX "${PLAYBACK_PLUGIN_SUFFIX}"
    VERSION "1.0.0"
    SOVERSION 1
)

install(TARGETS openmpt_playback
    LIBRARY DESTINATION plugins
    RUNTIME DESTINATION plugins
)

message(STATUS "OpenMPT playback plugin configured (libopenmpt 0.8.4)")
