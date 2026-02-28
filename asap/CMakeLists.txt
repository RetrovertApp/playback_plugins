# ASAP Playback Plugin
#
# This plugin plays Atari 8-bit music formats (SAP, CMC, RMT, TMC, etc.)
# Based on ASAP (Another Slight Atari Player) by Piotr Fusik.

cmake_minimum_required(VERSION 3.16)

set(output_dir "${RETROVERT_PLUGIN_OUTPUT_DIR}")

add_library(asap_playback SHARED
    asap_plugin.c
    asap.c
)

target_include_directories(asap_playback PRIVATE
    ${RETROVERT_INCLUDE_DIR}
)

suppress_external_warnings(asap_playback)

set_target_properties(asap_playback PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${output_dir}"
    RUNTIME_OUTPUT_DIRECTORY "${output_dir}"
    PREFIX ""
    SUFFIX "${PLAYBACK_PLUGIN_SUFFIX}"
    VERSION "1.0.0"
    SOVERSION 1
)

install(TARGETS asap_playback
    LIBRARY DESTINATION plugins
    RUNTIME DESTINATION plugins
)

message(STATUS "ASAP playback plugin configured")
