# Audio Stream Playback Plugin
#
# This plugin plays standard streaming audio formats (MP3, FLAC, WAV, OGG Vorbis)
# using lightweight single-header libraries (dr_libs, stb_vorbis).

cmake_minimum_required(VERSION 3.16)

set(output_dir "${RETROVERT_PLUGIN_OUTPUT_DIR}")

add_library(audio_stream_playback SHARED
    audio_stream_plugin.c
    decoder_wav.c
    decoder_mp3.c
    decoder_flac.c
    decoder_vorbis.c
    metadata.c
)

target_include_directories(audio_stream_playback PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${RETROVERT_INCLUDE_DIR}
)

suppress_external_warnings(audio_stream_playback)

set_target_properties(audio_stream_playback PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${output_dir}"
    RUNTIME_OUTPUT_DIRECTORY "${output_dir}"
    PREFIX ""
    SUFFIX "${PLAYBACK_PLUGIN_SUFFIX}"
    VERSION "1.0.0"
    SOVERSION 1
)

install(TARGETS audio_stream_playback
    LIBRARY DESTINATION plugins
    RUNTIME DESTINATION plugins
)

message(STATUS "Audio Stream playback plugin configured")
