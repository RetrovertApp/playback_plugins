# 98fmplayer Playback Plugin
#
# This plugin plays NEC PC-98 FMP (PLAY6) music files using the 98fmplayer library.
# FMP uses YM2608 (OPNA) FM synthesis: 6 FM channels, 3 SSG channels, rhythm drums,
# ADPCM, and optional PPZ8 PCM sampler.
# The OPNA emulator runs at 55467 Hz natively; this plugin resamples to 48000 Hz.

cmake_minimum_required(VERSION 3.16)

set(output_dir "${RETROVERT_PLUGIN_OUTPUT_DIR}")

# Download 98fmplayer (BSD-2-Clause)
download_library(
    NAME 98fmplayer
    URL "https://github.com/myon98/98fmplayer/archive/4fa914e4b2b9.tar.gz"
    FILENAME "98fmplayer-4fa914e.tar.gz"
    HASH "50dc3b2ef1ac339d5d54a499cbc28baadc1068852d6a16fb16d58dca4d0add48"
    EXTRACT_DIR "98fmplayer-4fa914e4b2b994cb3ccf92d571a20a7cdf1fe36a"
)

set(FMPLAYER_ROOT "${98fmplayer_SOURCE_DIR}")

# libopna: OPNA (YM2608) chip emulator sources
set(LIBOPNA_SOURCES
    ${FMPLAYER_ROOT}/libopna/opna.c
    ${FMPLAYER_ROOT}/libopna/opnafm.c
    ${FMPLAYER_ROOT}/libopna/opnassg.c
    ${FMPLAYER_ROOT}/libopna/opnadrum.c
    ${FMPLAYER_ROOT}/libopna/opnaadpcm.c
    ${FMPLAYER_ROOT}/libopna/opnatimer.c
    ${FMPLAYER_ROOT}/libopna/opnassg-sinc-c.c
)

# fmdriver: format drivers (FMP + PMD parsers, PPZ8 sampler)
set(FMDRIVER_SOURCES
    ${FMPLAYER_ROOT}/fmdriver/fmdriver_common.c
    ${FMPLAYER_ROOT}/fmdriver/fmdriver_fmp.c
    ${FMPLAYER_ROOT}/fmdriver/fmdriver_pmd.c
    ${FMPLAYER_ROOT}/fmdriver/ppz8.c
)

# Build 98fmplayer core as a static library
add_library(fmplayer_static STATIC
    ${LIBOPNA_SOURCES}
    ${FMDRIVER_SOURCES}
)

target_include_directories(fmplayer_static PUBLIC
    ${FMPLAYER_ROOT}
    ${FMPLAYER_ROOT}/libopna
    ${FMPLAYER_ROOT}/fmdriver
)

suppress_external_warnings(fmplayer_static)
set_target_properties(fmplayer_static PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Plugin shared library
add_library(fmplayer_playback SHARED
    fmplayer_plugin.c
)

target_include_directories(fmplayer_playback PRIVATE
    ${RETROVERT_INCLUDE_DIR}
    ${FMPLAYER_ROOT}
    ${FMPLAYER_ROOT}/libopna
    ${FMPLAYER_ROOT}/fmdriver
)

target_link_libraries(fmplayer_playback PRIVATE fmplayer_static)

if(UNIX)
    target_link_libraries(fmplayer_playback PRIVATE m)
endif()

target_compile_options(fmplayer_playback PRIVATE
    -Wno-unused-parameter
)

set_target_properties(fmplayer_playback PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${output_dir}"
    RUNTIME_OUTPUT_DIRECTORY "${output_dir}"
    PREFIX ""
    SUFFIX "${PLAYBACK_PLUGIN_SUFFIX}"
    VERSION "1.0.0"
    SOVERSION 1
)

install(TARGETS fmplayer_playback
    LIBRARY DESTINATION plugins
    RUNTIME DESTINATION plugins
)

message(STATUS "98fmplayer playback plugin configured")
