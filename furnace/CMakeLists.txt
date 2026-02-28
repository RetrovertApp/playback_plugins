# Furnace Playback Plugin
#
# This plugin plays Furnace (.fur), Deflemask (.dmf), and FamiTracker (.ftm) files
# using the Furnace tracker engine (tildearrow/furnace).
# Furnace emulates 50+ sound chips for multi-system chiptune playback.

cmake_minimum_required(VERSION 3.16)

enable_language(CXX)

set(output_dir "${RETROVERT_PLUGIN_OUTPUT_DIR}")

find_package(ZLIB REQUIRED)

# =====================================================================================
# Furnace engine (main library)
# =====================================================================================

download_library(
    NAME furnace
    URL "https://github.com/tildearrow/furnace/archive/refs/tags/v0.6.8.3.tar.gz"
    FILENAME "furnace-0.6.8.3.tar.gz"
    HASH "c54b93dc7211a8e86828ea6b436628920df7a7dc87b130b3752f2251f5466e9c"
    EXTRACT_DIR "furnace-0.6.8.3"
    PATCHES
        patches/engine-cpp.patch
        patches/engine-h.patch
        patches/config-cpp.patch
        patches/log-cpp.patch
        patches/adpcm-xq-c.patch
        patches/ymf278-missing-includes.patch
)

# =====================================================================================
# adpcm (superctr) - ADPCM codec submodule missing from release tarball
# =====================================================================================

download_library(
    NAME furnace_adpcm
    URL "https://github.com/superctr/adpcm/archive/ef7a217154ba.tar.gz"
    FILENAME "adpcm-ef7a217.tar.gz"
    HASH "46da29342d2968ff222ba00e07c646e038b76af2e6c86de037c653059a056251"
    EXTRACT_DIR "adpcm-ef7a217154badc3b99978ac481b268c8aab67bd8"
)

# Copy adpcm headers/sources into furnace's extern/adpcm/ so relative includes work
file(GLOB ADPCM_FILES "${furnace_adpcm_SOURCE_DIR}/*.c" "${furnace_adpcm_SOURCE_DIR}/*.h")
file(COPY ${ADPCM_FILES} DESTINATION "${furnace_SOURCE_DIR}/extern/adpcm/")

# =====================================================================================
# fmt - String formatting library (submodule missing from release tarball)
# =====================================================================================

download_library(
    NAME furnace_fmt
    URL "https://github.com/fmtlib/fmt/archive/e57ca2e.tar.gz"
    FILENAME "fmt-e57ca2e.tar.gz"
    HASH "2d18d7b1c393791a180c8321cedd702093b5527625be955178bef8af2b1cf6db"
    EXTRACT_DIR "fmt-e57ca2e3685b160617d3d95fcd9e789c4e06ca88"
)

# =====================================================================================
# Furnace engine static library
# =====================================================================================

set(FURNACE_ROOT "${furnace_SOURCE_DIR}")

# Core utility sources
set(FURNACE_UTIL_SOURCES
    ${FURNACE_ROOT}/src/log.cpp
    ${FURNACE_ROOT}/src/baseutils.cpp
    ${FURNACE_ROOT}/src/fileutils.cpp
    ${FURNACE_ROOT}/src/utfutils.cpp
    ${FURNACE_ROOT}/src/pch.cpp
    ${FURNACE_ROOT}/src/audio/abstract.cpp
    ${FURNACE_ROOT}/src/audio/midi.cpp
    ${FURNACE_ROOT}/src/audio/pipe.cpp
)

# External chip emulator sources (vendored in Furnace)
set(FURNACE_EXTERN_SOURCES
    ${FURNACE_ROOT}/extern/itcompress/compression.c
    ${FURNACE_ROOT}/extern/SAASound/src/SAAAmp.cpp
    ${FURNACE_ROOT}/extern/SAASound/src/SAADevice.cpp
    ${FURNACE_ROOT}/extern/SAASound/src/SAAEnv.cpp
    ${FURNACE_ROOT}/extern/SAASound/src/SAAFreq.cpp
    ${FURNACE_ROOT}/extern/SAASound/src/SAAImpl.cpp
    ${FURNACE_ROOT}/extern/SAASound/src/SAANoise.cpp
    ${FURNACE_ROOT}/extern/SAASound/src/SAASndC.cpp
    ${FURNACE_ROOT}/extern/SAASound/src/SAASound.cpp
    ${FURNACE_ROOT}/extern/adpcm/bs_codec.c
    ${FURNACE_ROOT}/extern/adpcm/oki_codec.c
    ${FURNACE_ROOT}/extern/adpcm/yma_codec.c
    ${FURNACE_ROOT}/extern/adpcm/ymb_codec.c
    ${FURNACE_ROOT}/extern/adpcm/ymz_codec.c
    ${FURNACE_ROOT}/extern/adpcm-xq-s/adpcm-lib.c
    ${FURNACE_ROOT}/extern/opn/ym3438.c
    ${FURNACE_ROOT}/extern/Nuked-PSG/ympsg.c
    ${FURNACE_ROOT}/extern/opm/opm.c
    ${FURNACE_ROOT}/extern/Nuked-OPLL/opll.c
    ${FURNACE_ROOT}/extern/opl/opl3.c
    ${FURNACE_ROOT}/extern/YM3812-LLE/fmopl2.c
    ${FURNACE_ROOT}/extern/YMF262-LLE/fmopl3.c
    ${FURNACE_ROOT}/extern/YMF276-LLE/fmopn2.c
    ${FURNACE_ROOT}/extern/ESFMu/esfm.c
    ${FURNACE_ROOT}/extern/ESFMu/esfm_registers.c
    ${FURNACE_ROOT}/extern/emu2413/emu2413.c
    ${FURNACE_ROOT}/extern/YM2608-LLE/fmopna_2608.c
    ${FURNACE_ROOT}/extern/YM2608-LLE/fmopna_2610.c
    ${FURNACE_ROOT}/extern/YM2608-LLE/fmopna_2612.c
    ${FURNACE_ROOT}/extern/pwrnoise/pwrnoise.c
    ${FURNACE_ROOT}/extern/blip_buf/blip_buf.c
)

# vgsound_emu sources
file(GLOB_RECURSE FURNACE_VGSOUND_SOURCES
    ${FURNACE_ROOT}/extern/vgsound_emu-modified/vgsound_emu/src/*.cpp
)

# fmt library sources
set(FURNACE_FMT_SOURCES
    ${furnace_fmt_SOURCE_DIR}/src/format.cc
    ${furnace_fmt_SOURCE_DIR}/src/os.cc
)

# Platform sound emulator sources
file(GLOB_RECURSE FURNACE_PLATFORM_SOUND_SOURCES
    ${FURNACE_ROOT}/src/engine/platform/sound/*.cpp
    ${FURNACE_ROOT}/src/engine/platform/sound/*.c
    ${FURNACE_ROOT}/src/engine/platform/sound/*.cc
)
# Exclude files not part of Furnace engine build
list(FILTER FURNACE_PLATFORM_SOUND_SOURCES EXCLUDE REGEX "resample/test\\.cpp$")
list(FILTER FURNACE_PLATFORM_SOUND_SOURCES EXCLUDE REGEX "m114s\\.c$")

# Engine core sources
file(GLOB FURNACE_ENGINE_SOURCES ${FURNACE_ROOT}/src/engine/*.cpp)
file(GLOB FURNACE_ENGINE_C_SOURCES ${FURNACE_ROOT}/src/engine/*.c)

# Platform dispatch sources
file(GLOB FURNACE_PLATFORM_SOURCES ${FURNACE_ROOT}/src/engine/platform/*.cpp)

# File operations
file(GLOB FURNACE_FILEOPS_SOURCES ${FURNACE_ROOT}/src/engine/fileOps/*.cpp)

# Export sources
file(GLOB FURNACE_EXPORT_SOURCES ${FURNACE_ROOT}/src/engine/export/*.cpp)

# Effect sources
file(GLOB FURNACE_EFFECT_SOURCES ${FURNACE_ROOT}/src/engine/effect/*.cpp)

# Remove sources that shouldn't be in our build
list(FILTER FURNACE_ENGINE_SOURCES EXCLUDE REGEX "sfWrapper\\.cpp$")
list(FILTER FURNACE_PLATFORM_SOURCES EXCLUDE REGEX "swan_before\\.cpp$")

# winStuff.cpp is only needed on Windows (provides getWinConfigPath)
if(NOT WIN32)
    list(FILTER FURNACE_ENGINE_SOURCES EXCLUDE REGEX "winStuff\\.cpp$")
endif()

add_library(furnace_static STATIC
    ${FURNACE_UTIL_SOURCES}
    ${FURNACE_EXTERN_SOURCES}
    ${FURNACE_VGSOUND_SOURCES}
    ${FURNACE_FMT_SOURCES}
    ${FURNACE_PLATFORM_SOUND_SOURCES}
    ${FURNACE_ENGINE_SOURCES}
    ${FURNACE_ENGINE_C_SOURCES}
    ${FURNACE_PLATFORM_SOURCES}
    ${FURNACE_FILEOPS_SOURCES}
    ${FURNACE_EXPORT_SOURCES}
    ${FURNACE_EFFECT_SOURCES}
)

target_include_directories(furnace_static PRIVATE
    ${FURNACE_ROOT}/src
    ${FURNACE_ROOT}/src/icon
    ${FURNACE_ROOT}/extern/fmt/include
    ${furnace_fmt_SOURCE_DIR}/include
    ${FURNACE_ROOT}/extern/IconFontCppHeaders
    ${FURNACE_ROOT}/extern/blip_buf
    ${FURNACE_ROOT}/extern/vgsound_emu-modified
    ${FURNACE_ROOT}/extern
    ${FURNACE_ROOT}
)

target_compile_definitions(furnace_static PRIVATE
    DIV_NO_SNDFILE
)

target_link_libraries(furnace_static PRIVATE ZLIB::ZLIB)

target_compile_options(furnace_static PRIVATE -w)

set_target_properties(furnace_static PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

# =====================================================================================
# Furnace playback plugin (shared library)
# =====================================================================================

add_library(furnace_playback SHARED
    furnace_plugin.cpp
)

target_include_directories(furnace_playback PRIVATE
    ${RETROVERT_INCLUDE_DIR}
    ${FURNACE_ROOT}/src
    ${FURNACE_ROOT}/src/icon
    ${FURNACE_ROOT}/extern/fmt/include
    ${furnace_fmt_SOURCE_DIR}/include
    ${FURNACE_ROOT}/extern/IconFontCppHeaders
    ${FURNACE_ROOT}/extern/blip_buf
    ${FURNACE_ROOT}/extern/vgsound_emu-modified
    ${FURNACE_ROOT}/extern
    ${FURNACE_ROOT}
)

target_link_libraries(furnace_playback PRIVATE furnace_static ZLIB::ZLIB)

if(UNIX)
    target_link_libraries(furnace_playback PRIVATE stdc++ m pthread)
endif()

if(WIN32)
    target_link_libraries(furnace_playback PRIVATE shlwapi)
endif()

target_compile_options(furnace_playback PRIVATE
    -Wno-unused-parameter
    -Wno-deprecated-literal-operator
)

set_target_properties(furnace_playback PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${output_dir}"
    RUNTIME_OUTPUT_DIRECTORY "${output_dir}"
    PREFIX ""
    SUFFIX "${PLAYBACK_PLUGIN_SUFFIX}"
    VERSION "1.0.0"
    SOVERSION 1
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

install(TARGETS furnace_playback
    LIBRARY DESTINATION plugins
    RUNTIME DESTINATION plugins
)

message(STATUS "Furnace playback plugin configured (Furnace 0.6.8.3)")
