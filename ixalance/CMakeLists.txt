# Ixalance Playback Plugin
#
# This plugin plays IXS (Impulse Tracker eXtendable Sequencer) files using
# webixs by Juergen Wothke, reverse-engineered from Shortcut Software's player.

cmake_minimum_required(VERSION 3.16)

enable_language(C CXX)

find_package(ZLIB REQUIRED)

set(output_dir "${RETROVERT_PLUGIN_OUTPUT_DIR}")

# Build ixalance as a static library (vendored webixs source)
add_library(ixalance_static STATIC
    src/asmEmu.cpp
    src/FileMap.cpp
    src/FileMapSFXI.cpp
    src/FileSFXI.cpp
    src/Mixer1.cpp
    src/Mixer2.cpp
    src/Mixer3.cpp
    src/Mixer4.cpp
    src/MixerBase.cpp
    src/Module.cpp
    src/Packer.cpp
    src/PlayerCore.cpp
    src/PlayerIXS.cpp
    src/WaveGen.cpp
    src/WaveLibrary.c
)

target_include_directories(ixalance_static PUBLIC
    src
)

target_link_libraries(ixalance_static PUBLIC ZLIB::ZLIB)

# The webixs codebase uses LINUX to mean "modern non-Emscripten build" and gates out
# Windows-specific code (Win32 audio, File0.h, etc.) that was not ported.
# Define it on all platforms so the portable code paths are used everywhere.
target_compile_definitions(ixalance_static PUBLIC LINUX)

suppress_external_warnings(ixalance_static)
set_target_properties(ixalance_static PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    CXX_STANDARD 14
)

# Disable strict aliasing for the asmEmu type-punning code
target_compile_options(ixalance_static PRIVATE
    -fno-strict-aliasing
)

# Build plugin shared library
add_library(ixalance_playback SHARED
    ixalance_plugin.cpp
)

target_include_directories(ixalance_playback PRIVATE
    ${RETROVERT_INCLUDE_DIR}
    src
)

target_link_libraries(ixalance_playback PRIVATE ixalance_static ZLIB::ZLIB)

if(NOT WIN32)
    target_link_libraries(ixalance_playback PRIVATE stdc++ m)
endif()

target_compile_options(ixalance_playback PRIVATE
    -Wno-unused-parameter
)

set_target_properties(ixalance_playback PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${output_dir}"
    RUNTIME_OUTPUT_DIRECTORY "${output_dir}"
    PREFIX ""
    SUFFIX "${PLAYBACK_PLUGIN_SUFFIX}"
    VERSION "1.0.0"
    SOVERSION 1
    CXX_STANDARD 14
)

install(TARGETS ixalance_playback
    LIBRARY DESTINATION plugins
    RUNTIME DESTINATION plugins
)

message(STATUS "Ixalance (IXS) playback plugin configured")
