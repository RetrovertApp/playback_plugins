# PMDmini Playback Plugin
#
# This plugin plays NEC PC-98 PMD music files using the pmdmini library.
# PMD uses YM2608 (OPNA) FM synthesis. The engine has global state so only
# one file can be open at a time (fine since only one song plays at a time).
# Files: .m, .m2

cmake_minimum_required(VERSION 3.16)

# pmdmini has C++ engine code
enable_language(CXX)

set(output_dir "${RETROVERT_PLUGIN_OUTPUT_DIR}")

# Download pmdmini v2.0.0
download_library(
    NAME pmdmini
    URL "https://github.com/mistydemeo/pmdmini/archive/refs/tags/v2.0.0.tar.gz"
    FILENAME "pmdmini-2.0.0.tar.gz"
    HASH "e3288dcf356e83ef4ad48cde44fcb703ca9ce478b9fcac1b44bd9d2d84bf2ba3"
    EXTRACT_DIR "pmdmini-2.0.0"
    PATCHES patches/pmdmini-disable-x86asm.patch patches/pmdmini-packed-clang.patch patches/pmdmini-narrowing-fix.patch patches/pmdmini-pointer-compare-fix.patch patches/scope-capture.patch
)

# C wrapper
set(PMDMINI_C_SOURCES ${pmdmini_SOURCE_DIR}/src/pmdmini.c)

# C++ engine
file(GLOB PMDMINI_FMGEN_SOURCES ${pmdmini_SOURCE_DIR}/src/fmgen/*.cpp)
file(GLOB PMDMINI_PMDWIN_SOURCES ${pmdmini_SOURCE_DIR}/src/pmdwin/*.cpp)

add_library(pmdmini_static STATIC
    ${PMDMINI_C_SOURCES}
    ${PMDMINI_FMGEN_SOURCES}
    ${PMDMINI_PMDWIN_SOURCES}
)

target_include_directories(pmdmini_static PUBLIC
    ${pmdmini_SOURCE_DIR}/src
    ${pmdmini_SOURCE_DIR}/src/pmdwin
    ${pmdmini_SOURCE_DIR}/src/fmgen
)

suppress_external_warnings(pmdmini_static)
set_target_properties(pmdmini_static PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    CXX_STANDARD 14
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
)

# Plugin shared library
add_library(pmdmini_playback SHARED
    pmdmini_plugin.c
)

target_include_directories(pmdmini_playback PRIVATE
    ${RETROVERT_INCLUDE_DIR}
    ${pmdmini_SOURCE_DIR}/src
)

target_link_libraries(pmdmini_playback PRIVATE pmdmini_static)
if(UNIX)
    target_link_libraries(pmdmini_playback PRIVATE stdc++ m)
endif()

target_compile_options(pmdmini_playback PRIVATE
    -Wno-unused-parameter
)

set_target_properties(pmdmini_playback PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${output_dir}"
    RUNTIME_OUTPUT_DIRECTORY "${output_dir}"
    PREFIX ""
    SUFFIX "${PLAYBACK_PLUGIN_SUFFIX}"
    VERSION "1.0.0"
    SOVERSION 1
)

install(TARGETS pmdmini_playback
    LIBRARY DESTINATION plugins
    RUNTIME DESTINATION plugins
)

message(STATUS "PMDmini playback plugin configured")
