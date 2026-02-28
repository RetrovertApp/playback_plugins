# V2M Playback Plugin
#
# This plugin plays V2 Synthesizer Music files (v2m) using jgilje's v2m-player.
# V2M is the format used by Farbrausch's V2 synthesizer in demoscene productions.

cmake_minimum_required(VERSION 3.16)

enable_language(CXX)

set(output_dir "${RETROVERT_PLUGIN_OUTPUT_DIR}")

# Download v2m-player (jgilje fork, BZRPlayer-tested)
download_library(
    NAME v2m
    URL "https://github.com/jgilje/v2m-player/archive/385ad6956202d09a2912ef91df9c44e13f9e4a84.tar.gz"
    FILENAME "v2m-player-385ad69.tar.gz"
    HASH "b98a6707445e81434ecc051a5be91155fda667eafff2933da2e4495677a4b13e"
    EXTRACT_DIR "v2m-player-385ad6956202d09a2912ef91df9c44e13f9e4a84"
    PATCHES
        patches/v2m-sound-fixes.patch
        patches/scope-capture.patch
)

# Build v2m as a static library
add_library(v2m_static STATIC
    ${v2m_SOURCE_DIR}/src/ronan.cpp
    ${v2m_SOURCE_DIR}/src/v2mplayer.cpp
    ${v2m_SOURCE_DIR}/src/v2mconv.cpp
    ${v2m_SOURCE_DIR}/src/synth_core.cpp
    ${v2m_SOURCE_DIR}/src/sounddef.cpp
)

target_include_directories(v2m_static PUBLIC
    ${v2m_SOURCE_DIR}/src
)

suppress_external_warnings(v2m_static)
set_target_properties(v2m_static PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    CXX_STANDARD 11
)

# Build plugin shared library
add_library(v2m_playback SHARED
    v2m_plugin.cpp
)

target_include_directories(v2m_playback PRIVATE
    ${RETROVERT_INCLUDE_DIR}
    ${v2m_SOURCE_DIR}/src
)

target_link_libraries(v2m_playback PRIVATE v2m_static)

if(NOT WIN32)
    target_link_libraries(v2m_playback PRIVATE stdc++ m)
endif()

target_compile_options(v2m_playback PRIVATE
    -Wno-unused-parameter
)

set_target_properties(v2m_playback PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${output_dir}"
    RUNTIME_OUTPUT_DIRECTORY "${output_dir}"
    PREFIX ""
    SUFFIX "${PLAYBACK_PLUGIN_SUFFIX}"
    VERSION "1.0.0"
    SOVERSION 1
    CXX_STANDARD 11
)

install(TARGETS v2m_playback
    LIBRARY DESTINATION plugins
    RUNTIME DESTINATION plugins
)

message(STATUS "V2M playback plugin configured")
