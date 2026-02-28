# SPU/VAG Playback Plugin
#
# Self-contained PlayStation SPU ADPCM decoder. No external library needed.
# Supported formats: VAG (with header), VB (raw ADPCM blocks)

cmake_minimum_required(VERSION 3.16)

set(output_dir "${RETROVERT_PLUGIN_OUTPUT_DIR}")

add_library(spu_playback SHARED
    spu_plugin.c
)

target_include_directories(spu_playback PRIVATE
    ${RETROVERT_INCLUDE_DIR}
)

target_compile_options(spu_playback PRIVATE
    -Wno-unused-parameter
)

set_target_properties(spu_playback PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${output_dir}"
    RUNTIME_OUTPUT_DIRECTORY "${output_dir}"
    PREFIX ""
    SUFFIX "${PLAYBACK_PLUGIN_SUFFIX}"
    VERSION "1.0.0"
    SOVERSION 1
)

install(TARGETS spu_playback
    LIBRARY DESTINATION plugins
    RUNTIME DESTINATION plugins
)

message(STATUS "SPU playback plugin configured")
