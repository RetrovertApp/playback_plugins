# Hively Playback Plugin
#
# This plugin plays AHX and HVL (Hively Tracker) music formats.
# Based on the hvl_replay library by Xeron/IRIS.

cmake_minimum_required(VERSION 3.16)

set(output_dir "${RETROVERT_PLUGIN_OUTPUT_DIR}")

add_library(hively_playback SHARED
    hively_plugin.c
    hvl_replay.c
)

target_include_directories(hively_playback PRIVATE
    ${RETROVERT_INCLUDE_DIR}
)

suppress_external_warnings(hively_playback)

set_target_properties(hively_playback PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${output_dir}"
    RUNTIME_OUTPUT_DIRECTORY "${output_dir}"
    PREFIX ""
    SUFFIX "${PLAYBACK_PLUGIN_SUFFIX}"
    VERSION "1.0.0"
    SOVERSION 1
)

install(TARGETS hively_playback
    LIBRARY DESTINATION plugins
    RUNTIME DESTINATION plugins
)

message(STATUS "Hively playback plugin configured")
