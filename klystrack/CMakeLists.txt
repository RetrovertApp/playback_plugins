# Klystrack Playback Plugin
#
# This plugin plays Klystrack music files (.kt) using the klystron sound engine.
# Klystrack is a chiptune tracker by kometbomb.

cmake_minimum_required(VERSION 3.16)

set(output_dir "${RETROVERT_PLUGIN_OUTPUT_DIR}")

# Download klystron sound engine
download_library(
    NAME klystron
    URL "https://github.com/kometbomb/klystron/archive/989fafc4fffb1bb881ab677fe52eb34527e08129.tar.gz"
    FILENAME "klystron-989fafc.tar.gz"
    HASH "b90b23b1808340e35ef314c57b82489df0e57c039b04a78171f879b2b0449afc"
    EXTRACT_DIR "klystron-989fafc4fffb1bb881ab677fe52eb34527e08129"
    PATCHES patches/klystron-fix-rwops-leak.patch patches/scope-capture.patch
)

# Build klystron as a static library (sound engine only, no graphics/GUI)
set(KLYSTRON_SRC
    ${klystron_SOURCE_DIR}/src/lib/ksnd.c
    ${klystron_SOURCE_DIR}/src/snd/cyd.c
    ${klystron_SOURCE_DIR}/src/snd/cydchr.c
    ${klystron_SOURCE_DIR}/src/snd/cydcrush.c
    ${klystron_SOURCE_DIR}/src/snd/cydentry.c
    ${klystron_SOURCE_DIR}/src/snd/cydflt.c
    ${klystron_SOURCE_DIR}/src/snd/cydfm.c
    ${klystron_SOURCE_DIR}/src/snd/cydfx.c
    ${klystron_SOURCE_DIR}/src/snd/cydosc.c
    ${klystron_SOURCE_DIR}/src/snd/cydrvb.c
    ${klystron_SOURCE_DIR}/src/snd/cydwave.c
    ${klystron_SOURCE_DIR}/src/snd/freqs.c
    ${klystron_SOURCE_DIR}/src/snd/music.c
    ${klystron_SOURCE_DIR}/src/snd/pack.c
)

add_library(klystron_static STATIC ${KLYSTRON_SRC})

target_include_directories(klystron_static PUBLIC
    ${klystron_SOURCE_DIR}/src
    ${klystron_SOURCE_DIR}/src/snd
    ${klystron_SOURCE_DIR}/src/lib
)

# Use our SDL type shims instead of real SDL
target_include_directories(klystron_static BEFORE PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/sdl_shim
)

target_compile_definitions(klystron_static PRIVATE
    NOSDL_MIXER
    STEREOOUTPUT
    _USE_MATH_DEFINES
)

suppress_external_warnings(klystron_static)
set_target_properties(klystron_static PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Build plugin shared library
add_library(klystrack_playback SHARED
    klystrack_plugin.c
)

target_include_directories(klystrack_playback PRIVATE
    ${RETROVERT_INCLUDE_DIR}
    ${klystron_SOURCE_DIR}/src/lib
    ${klystron_SOURCE_DIR}/src/snd
)

target_link_libraries(klystrack_playback PRIVATE klystron_static)

target_compile_options(klystrack_playback PRIVATE
    -Wno-unused-parameter
)

set_target_properties(klystrack_playback PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${output_dir}"
    RUNTIME_OUTPUT_DIRECTORY "${output_dir}"
    PREFIX ""
    SUFFIX "${PLAYBACK_PLUGIN_SUFFIX}"
    VERSION "1.0.0"
    SOVERSION 1
)

install(TARGETS klystrack_playback
    LIBRARY DESTINATION plugins
    RUNTIME DESTINATION plugins
)

message(STATUS "Klystrack playback plugin configured")
