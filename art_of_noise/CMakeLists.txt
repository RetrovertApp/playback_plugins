# Art of Noise Playback Plugin
#
# This plugin plays Art of Noise (AoN) tracker formats (AON4 and AON8).
# Based on the aon_player C11 replayer library.

cmake_minimum_required(VERSION 3.16)

set(output_dir "${RETROVERT_PLUGIN_OUTPUT_DIR}")

add_library(art_of_noise_playback SHARED
    aon_plugin.c
    aon_player.c
)

target_include_directories(art_of_noise_playback PRIVATE
    ${RETROVERT_INCLUDE_DIR}
)

suppress_external_warnings(art_of_noise_playback)

set_target_properties(art_of_noise_playback PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${output_dir}"
    RUNTIME_OUTPUT_DIRECTORY "${output_dir}"
    PREFIX ""
    SUFFIX "${PLAYBACK_PLUGIN_SUFFIX}"
    VERSION "1.0.0"
    SOVERSION 1
)

install(TARGETS art_of_noise_playback
    LIBRARY DESTINATION plugins
    RUNTIME DESTINATION plugins
)

message(STATUS "Art of Noise playback plugin configured")
