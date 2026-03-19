# Retrovert Playback Plugins Bundle
#
# Aggregates all playback plugins as submodules.
# Each plugin is self-contained and independently buildable.
#
# Usage:
#   cmake -B build -G Ninja
#   cmake --build build
#
# Or when included as a subdirectory from a host project:
#   set(RETROVERT_PLUGIN_OUTPUT_DIR "${CMAKE_BINARY_DIR}/plugins/playback")
#   add_subdirectory(path/to/playback_plugins)

cmake_minimum_required(VERSION 3.16)
project(playback_plugins C CXX)

add_subdirectory(plugins/adplug)
add_subdirectory(plugins/art_of_noise)
add_subdirectory(plugins/asap)
add_subdirectory(plugins/audio_stream)
add_subdirectory(plugins/cpsycle)
add_subdirectory(plugins/eupmini)
add_subdirectory(plugins/fmplayer)
add_subdirectory(plugins/furnace)
add_subdirectory(plugins/gme)
add_subdirectory(plugins/hively)
add_subdirectory(plugins/ixalance)
add_subdirectory(plugins/klystrack)
add_subdirectory(plugins/libkss)
add_subdirectory(plugins/libvgm)
add_subdirectory(plugins/mdxmini)
add_subdirectory(plugins/openmpt)
add_subdirectory(plugins/organya)
add_subdirectory(plugins/pmdmini)
add_subdirectory(plugins/pxtone)
add_subdirectory(plugins/sc68)
add_subdirectory(plugins/sidplayfp)
add_subdirectory(plugins/spu)
add_subdirectory(plugins/sunvox)
add_subdirectory(plugins/tfmx)
add_subdirectory(plugins/uade)
add_subdirectory(plugins/xsf)
add_subdirectory(plugins/zxtune)
