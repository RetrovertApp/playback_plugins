# eupmini Playback Plugin
#
# This plugin plays FM TOWNS Euphony (.eup) music files using the eupmini library.
# Euphony uses FM TOWNS sound hardware: 6 FM channels (YM2612-compatible) and
# 8 PCM channels.

cmake_minimum_required(VERSION 3.16)

enable_language(CXX)

set(output_dir "${RETROVERT_PLUGIN_OUTPUT_DIR}")

# Download eupmini
download_library(
    NAME eupmini
    URL "https://codeload.github.com/gzaffin/eupmini/tar.gz/3640a82f343360bac08041ae82d2938efe444f17"
    FILENAME "eupmini-3640a82.tar.gz"
    HASH "3035d096333de832ecf23c35a0f1519530e3b6215f1482df4c10b1d92b156cbd"
    EXTRACT_DIR "eupmini-3640a82f343360bac08041ae82d2938efe444f17"
    PATCHES patches/cstdint-fix.patch
)

# Core library sources (excluding eupplay.cpp CLI main)
set(EUPMINI_SOURCES
    ${eupmini_SOURCE_DIR}/eupplayer.cpp
    ${eupmini_SOURCE_DIR}/eupplayer_townsEmulator.cpp
    ${eupmini_SOURCE_DIR}/sintbl.cpp
)

add_library(eupmini_static STATIC ${EUPMINI_SOURCES})

# Use our SDL shim instead of real SDL (same technique as klystrack plugin)
target_include_directories(eupmini_static BEFORE PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/sdl_shim
)

target_include_directories(eupmini_static PUBLIC
    ${eupmini_SOURCE_DIR}
)

target_compile_definitions(eupmini_static PRIVATE
    EUPPLAYER_LITTLE_ENDIAN=1
)

suppress_external_warnings(eupmini_static)
set_target_properties(eupmini_static PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    CXX_STANDARD 11
    CXX_STANDARD_REQUIRED ON
)

# Plugin shared library (C++ because eupmini is C++)
add_library(eupmini_playback SHARED
    eupmini_plugin.cpp
)

target_include_directories(eupmini_playback PRIVATE
    ${RETROVERT_INCLUDE_DIR}
    ${eupmini_SOURCE_DIR}
)

target_compile_definitions(eupmini_playback PRIVATE
    EUPPLAYER_LITTLE_ENDIAN=1
)

target_link_libraries(eupmini_playback PRIVATE eupmini_static)
if(UNIX)
    target_link_libraries(eupmini_playback PRIVATE stdc++ m)
endif()

set_target_properties(eupmini_playback PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${output_dir}"
    RUNTIME_OUTPUT_DIRECTORY "${output_dir}"
    PREFIX ""
    SUFFIX "${PLAYBACK_PLUGIN_SUFFIX}"
    VERSION "1.0.0"
    SOVERSION 1
    CXX_STANDARD 11
    CXX_STANDARD_REQUIRED ON
)

install(TARGETS eupmini_playback
    LIBRARY DESTINATION plugins
    RUNTIME DESTINATION plugins
)

message(STATUS "eupmini playback plugin configured")
