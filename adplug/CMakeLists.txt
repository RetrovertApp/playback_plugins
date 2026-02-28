# AdPlug Playback Plugin
#
# This plugin plays OPL/AdLib music formats using the AdPlug library:
# CMF, DRO, IMF, RAW, ROL, HSC, and 50+ other formats.

cmake_minimum_required(VERSION 3.16)

enable_language(CXX)

set(output_dir "${RETROVERT_PLUGIN_OUTPUT_DIR}")

# Download AdPlug 2.4 (with null pointer fix patch for a2m-v2.cpp)
download_library(
    NAME adplug
    URL "https://github.com/adplug/adplug/archive/refs/tags/adplug-2.4.tar.gz"
    FILENAME "adplug-2.4.tar.gz"
    HASH "ebeaf8e2aacd3d19d756d4034cccaccfd2da5be465e633d1f463e3fc55baa065"
    EXTRACT_DIR "adplug-adplug-2.4"
    PATCHES patches/a2m-v2-null-pointer-fix.patch patches/scope-capture.patch
)

# Download libbinio 1.5 (required by AdPlug)
download_library(
    NAME libbinio
    URL "https://github.com/adplug/libbinio/archive/refs/tags/libbinio-1.5.tar.gz"
    FILENAME "libbinio-1.5.tar.gz"
    HASH "b05e3ceebe059903b21357bb7fe3cbbabeb2fa9b857a7257c9b507f027437ea7"
    EXTRACT_DIR "libbinio-libbinio-1.5"
)

# Generate binio.h from template (normally done by autoconf)
set(ENABLE_STRING 1)
set(ENABLE_IOSTREAM 1)
set(ISO_STDLIB 1)
set(WITH_MATH 1)
set(TYPE_INT "long long")
set(TYPE_FLOAT "long double")
configure_file(
    "${libbinio_SOURCE_DIR}/src/binio.h.in"
    "${libbinio_SOURCE_DIR}/src/binio.h"
)

# Generate version.h from template (normally done by autoconf)
set(VERSION "2.4")
configure_file(
    "${adplug_SOURCE_DIR}/src/version.h.in"
    "${adplug_SOURCE_DIR}/src/version.h"
)

# libbinio sources
set(LIBBINIO_SOURCES
    ${libbinio_SOURCE_DIR}/src/binio.cpp
    ${libbinio_SOURCE_DIR}/src/binfile.cpp
    ${libbinio_SOURCE_DIR}/src/binstr.cpp
    ${libbinio_SOURCE_DIR}/src/binwrap.cpp
)

# Build libbinio as static library
add_library(binio_static STATIC ${LIBBINIO_SOURCES})
target_include_directories(binio_static PUBLIC
    ${libbinio_SOURCE_DIR}/src
)
set_target_properties(binio_static PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    CXX_STANDARD 14
    CXX_STANDARD_REQUIRED ON
)
suppress_external_warnings(binio_static)

# AdPlug sources - all format players and emulators
# Note: We exclude realopl.cpp (real hardware), analopl.cpp (analog output), diskopl.cpp (disk logging)
set(ADPLUG_SOURCES
    ${adplug_SOURCE_DIR}/src/adplug.cpp
    ${adplug_SOURCE_DIR}/src/player.cpp
    ${adplug_SOURCE_DIR}/src/players.cpp
    ${adplug_SOURCE_DIR}/src/fprovide.cpp
    ${adplug_SOURCE_DIR}/src/database.cpp
    ${adplug_SOURCE_DIR}/src/emuopl.cpp
    ${adplug_SOURCE_DIR}/src/woodyopl.cpp
    ${adplug_SOURCE_DIR}/src/fmopl.c
    ${adplug_SOURCE_DIR}/src/nukedopl.c
    ${adplug_SOURCE_DIR}/src/debug.c
    ${adplug_SOURCE_DIR}/src/unlzw.c
    ${adplug_SOURCE_DIR}/src/unlzh.c
    ${adplug_SOURCE_DIR}/src/unlzss.c
    ${adplug_SOURCE_DIR}/src/depack.c
    ${adplug_SOURCE_DIR}/src/sixdepack.cpp
    # Format players
    ${adplug_SOURCE_DIR}/src/a2m.cpp
    ${adplug_SOURCE_DIR}/src/a2m-v2.cpp
    ${adplug_SOURCE_DIR}/src/adl.cpp
    ${adplug_SOURCE_DIR}/src/adtrack.cpp
    ${adplug_SOURCE_DIR}/src/amd.cpp
    ${adplug_SOURCE_DIR}/src/bam.cpp
    ${adplug_SOURCE_DIR}/src/bmf.cpp
    ${adplug_SOURCE_DIR}/src/cff.cpp
    ${adplug_SOURCE_DIR}/src/cmf.cpp
    ${adplug_SOURCE_DIR}/src/cmfmcsop.cpp
    ${adplug_SOURCE_DIR}/src/coktel.cpp
    ${adplug_SOURCE_DIR}/src/composer.cpp
    ${adplug_SOURCE_DIR}/src/d00.cpp
    ${adplug_SOURCE_DIR}/src/dfm.cpp
    ${adplug_SOURCE_DIR}/src/dmo.cpp
    ${adplug_SOURCE_DIR}/src/dro.cpp
    ${adplug_SOURCE_DIR}/src/dro2.cpp
    ${adplug_SOURCE_DIR}/src/dtm.cpp
    ${adplug_SOURCE_DIR}/src/flash.cpp
    ${adplug_SOURCE_DIR}/src/fmc.cpp
    ${adplug_SOURCE_DIR}/src/got.cpp
    ${adplug_SOURCE_DIR}/src/herad.cpp
    ${adplug_SOURCE_DIR}/src/hsc.cpp
    ${adplug_SOURCE_DIR}/src/hsp.cpp
    ${adplug_SOURCE_DIR}/src/hybrid.cpp
    ${adplug_SOURCE_DIR}/src/hyp.cpp
    ${adplug_SOURCE_DIR}/src/imf.cpp
    ${adplug_SOURCE_DIR}/src/jbm.cpp
    ${adplug_SOURCE_DIR}/src/ksm.cpp
    ${adplug_SOURCE_DIR}/src/lds.cpp
    ${adplug_SOURCE_DIR}/src/mad.cpp
    ${adplug_SOURCE_DIR}/src/mdi.cpp
    ${adplug_SOURCE_DIR}/src/mid.cpp
    ${adplug_SOURCE_DIR}/src/mkj.cpp
    ${adplug_SOURCE_DIR}/src/msc.cpp
    ${adplug_SOURCE_DIR}/src/mtk.cpp
    ${adplug_SOURCE_DIR}/src/mtr.cpp
    ${adplug_SOURCE_DIR}/src/mus.cpp
    ${adplug_SOURCE_DIR}/src/pis.cpp
    ${adplug_SOURCE_DIR}/src/plx.cpp
    ${adplug_SOURCE_DIR}/src/protrack.cpp
    ${adplug_SOURCE_DIR}/src/psi.cpp
    ${adplug_SOURCE_DIR}/src/rad2.cpp
    ${adplug_SOURCE_DIR}/src/rat.cpp
    ${adplug_SOURCE_DIR}/src/raw.cpp
    ${adplug_SOURCE_DIR}/src/rix.cpp
    ${adplug_SOURCE_DIR}/src/rol.cpp
    ${adplug_SOURCE_DIR}/src/s3m.cpp
    ${adplug_SOURCE_DIR}/src/sa2.cpp
    ${adplug_SOURCE_DIR}/src/sng.cpp
    ${adplug_SOURCE_DIR}/src/sop.cpp
    ${adplug_SOURCE_DIR}/src/surroundopl.cpp
    ${adplug_SOURCE_DIR}/src/u6m.cpp
    ${adplug_SOURCE_DIR}/src/vgm.cpp
    ${adplug_SOURCE_DIR}/src/xad.cpp
    ${adplug_SOURCE_DIR}/src/xsm.cpp
    # Additional emulators (optional, but provide better quality)
    ${adplug_SOURCE_DIR}/src/temuopl.cpp
    ${adplug_SOURCE_DIR}/src/kemuopl.cpp
    ${adplug_SOURCE_DIR}/src/nemuopl.cpp
)

# Build AdPlug as static library
add_library(adplug_static STATIC ${ADPLUG_SOURCES})
target_include_directories(adplug_static PUBLIC
    ${adplug_SOURCE_DIR}/src
    ${libbinio_SOURCE_DIR}/src
)
target_link_libraries(adplug_static PRIVATE binio_static)
set_target_properties(adplug_static PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    CXX_STANDARD 14
    CXX_STANDARD_REQUIRED ON
)

# Define stricmp as strcasecmp on non-Windows platforms (POSIX compatibility)
if(NOT WIN32)
    target_compile_definitions(adplug_static PRIVATE stricmp=strcasecmp)
endif()

suppress_external_warnings(adplug_static)

# Build the plugin
add_library(adplug_playback SHARED
    adplug_plugin.cpp
)

target_include_directories(adplug_playback PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${adplug_SOURCE_DIR}/src
    ${libbinio_SOURCE_DIR}/src
    ${RETROVERT_INCLUDE_DIR}
)

target_link_libraries(adplug_playback PRIVATE adplug_static binio_static)

if(NOT WIN32)
    target_link_libraries(adplug_playback PRIVATE m)
endif()

target_compile_options(adplug_playback PRIVATE
    -Wno-unused-parameter
)

set_target_properties(adplug_playback PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${output_dir}"
    RUNTIME_OUTPUT_DIRECTORY "${output_dir}"
    PREFIX ""
    SUFFIX "${PLAYBACK_PLUGIN_SUFFIX}"
    VERSION "1.0.0"
    SOVERSION 1
    CXX_STANDARD 14
    CXX_STANDARD_REQUIRED ON
)

install(TARGETS adplug_playback
    LIBRARY DESTINATION plugins
    RUNTIME DESTINATION plugins
)

message(STATUS "AdPlug playback plugin configured")
