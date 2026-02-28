# xSF Playback Plugin
#
# Unified plugin for PSF (Portable Sound Format) files covering multiple platforms.
# Each emulator library is downloaded, built as a static library, and linked into
# a single shared plugin that dispatches by PSF version byte.
#
# Currently supported:
#   PSF/PSF2 (PlayStation 1/2) via highly_experimental + psflib
#   SSF/DSF (Sega Saturn/Dreamcast) via highly_theoretical
#   USF (Nintendo 64) via lazyusf2
#   GSF (Game Boy Advance) via viogsf
#   SNSF (Super Nintendo) via snsf9x
#   2SF (Nintendo DS) via vio2sf
#   QSF (Capcom QSound) via highly_quixotic

cmake_minimum_required(VERSION 3.16)

# vio2sf has C++ sources (SPU.cpp)
enable_language(CXX)

set(output_dir "${RETROVERT_PLUGIN_OUTPUT_DIR}")

find_package(ZLIB REQUIRED)

# =====================================================================================
# psflib - PSF container parser (common to all xSF formats)
# =====================================================================================

download_library(
    NAME psflib
    URL "https://gitlab.com/kode54/psflib/-/archive/3bea757c8b45c5e68da1b5a7b736ad960a06a124/psflib-3bea757c8b45c5e68da1b5a7b736ad960a06a124.tar.gz"
    FILENAME "psflib-3bea757c8b45c5e68da1b5a7b736ad960a06a124.tar.gz"
    HASH "1fc69539c3347c265cf44b92166de9ec93186a09a9cff56030de8cc53b23981c"
    EXTRACT_DIR "psflib-3bea757c8b45c5e68da1b5a7b736ad960a06a124"
)

add_library(psflib_static STATIC
    ${psflib_SOURCE_DIR}/psflib.c
    ${psflib_SOURCE_DIR}/psf2fs.c
)
target_include_directories(psflib_static PUBLIC ${psflib_SOURCE_DIR})
target_link_libraries(psflib_static PRIVATE ZLIB::ZLIB)
suppress_external_warnings(psflib_static)
target_compile_options(psflib_static PRIVATE -fvisibility=hidden)
set_target_properties(psflib_static PROPERTIES POSITION_INDEPENDENT_CODE ON)

# =====================================================================================
# highly_experimental - PSX/PS2 emulation for PSF/PSF2
# =====================================================================================

download_library(
    NAME highly_experimental
    URL "https://gitlab.com/kode54/highly_experimental/-/archive/0fa96d186e3c0437951732d0c50bf1da4e32970e/highly_experimental-0fa96d186e3c0437951732d0c50bf1da4e32970e.tar.gz"
    FILENAME "highly_experimental-0fa96d186e3c0437951732d0c50bf1da4e32970e.tar.gz"
    HASH "038a13b6086949dc040bcbb74cf61414ace21527ee25a761b9e2be36fae75f5a"
    EXTRACT_DIR "highly_experimental-0fa96d186e3c0437951732d0c50bf1da4e32970e"
    PATCHES patches/highly_experimental-scope-capture.patch
)

file(GLOB HE_SOURCES ${highly_experimental_SOURCE_DIR}/Core/*.c)
add_library(he_static STATIC ${HE_SOURCES})
target_include_directories(he_static PUBLIC ${highly_experimental_SOURCE_DIR}/Core)
target_compile_definitions(he_static PRIVATE EMU_COMPILE EMU_LITTLE_ENDIAN)
target_link_libraries(he_static PRIVATE psflib_static ZLIB::ZLIB)
# -w suppresses warnings, but Clang 16+ makes implicit-function-declaration a hard error
# that -w alone doesn't suppress, so we explicitly downgrade it
suppress_external_warnings(he_static)
target_compile_options(he_static PRIVATE -Wno-error=implicit-function-declaration)
# Hide internal symbols to prevent PLT interposition with identically-named symbols
# from the host application (e.g. the app's vfs_init collides with HE's vfs_init).
target_compile_options(he_static PRIVATE -fvisibility=hidden)
set_target_properties(he_static PROPERTIES POSITION_INDEPENDENT_CODE ON)

# =====================================================================================
# vio2sf - Nintendo DS emulation for 2SF
# =====================================================================================

download_library(
    NAME vio2sf
    URL "https://gitlab.com/kode54/vio2sf/-/archive/1d68801f5fd370c3275affd51505353e2b366a7e/vio2sf-1d68801f5fd370c3275affd51505353e2b366a7e.tar.bz2"
    FILENAME "vio2sf-1d68801f5fd370c3275affd51505353e2b366a7e.tar.bz2"
    HASH "471cf5cd1c54f2dfd8c863a6e241a80bcbde7b7a9d419f3dae6ebc20e35b7176"
    EXTRACT_DIR "vio2sf-1d68801f5fd370c3275affd51505353e2b366a7e"
)

file(GLOB VIO2SF_C_SOURCES ${vio2sf_SOURCE_DIR}/src/vio2sf/desmume/*.c)
file(GLOB VIO2SF_CXX_SOURCES ${vio2sf_SOURCE_DIR}/src/vio2sf/desmume/*.cpp)
add_library(vio2sf_static STATIC ${VIO2SF_C_SOURCES} ${VIO2SF_CXX_SOURCES})
target_include_directories(vio2sf_static PUBLIC ${vio2sf_SOURCE_DIR}/src/vio2sf/desmume)
target_compile_definitions(vio2sf_static PRIVATE BARRAY_DECORATE RESAMPLER_DECORATE)
target_link_libraries(vio2sf_static PRIVATE ZLIB::ZLIB)
# SSE4.1 needed for optimized DSP code in the C sources (x86_64 only)
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64|AMD64")
    target_compile_options(vio2sf_static PRIVATE $<$<COMPILE_LANGUAGE:C>:-msse4.1>)
endif()
suppress_external_warnings(vio2sf_static)
target_compile_options(vio2sf_static PRIVATE -fvisibility=hidden)
set_target_properties(vio2sf_static PROPERTIES POSITION_INDEPENDENT_CODE ON)

# =====================================================================================
# lazyusf2 - Nintendo 64 emulation for USF
# =====================================================================================

download_library(
    NAME lazyusf2
    URL "https://gitlab.com/kode54/lazyusf2/-/archive/421f00bcaa1988b8e1825e91780129f24fbd1aa0/lazyusf2-421f00bcaa1988b8e1825e91780129f24fbd1aa0.tar.bz2"
    FILENAME "lazyusf2-421f00bcaa1988b8e1825e91780129f24fbd1aa0.tar.bz2"
    HASH "b4ff5df45572e2c5ac5ecc98a5421701330a655b9971e5c012fe9f42bd12c51f"
    EXTRACT_DIR "lazyusf2-421f00bcaa1988b8e1825e91780129f24fbd1aa0"
    PATCHES patches/lazyusf2-control87-x64.patch
)

file(GLOB USF_SOURCES
    ${lazyusf2_SOURCE_DIR}/ai/*.c
    ${lazyusf2_SOURCE_DIR}/api/*.c
    ${lazyusf2_SOURCE_DIR}/debugger/dbg_decoder.c
    ${lazyusf2_SOURCE_DIR}/main/*.c
    ${lazyusf2_SOURCE_DIR}/memory/*.c
    ${lazyusf2_SOURCE_DIR}/pi/*.c
    ${lazyusf2_SOURCE_DIR}/r4300/*.c
    ${lazyusf2_SOURCE_DIR}/rdp/*.c
    ${lazyusf2_SOURCE_DIR}/ri/*.c
    ${lazyusf2_SOURCE_DIR}/rsp/*.c
    ${lazyusf2_SOURCE_DIR}/rsp_hle/*.c
    ${lazyusf2_SOURCE_DIR}/rsp_lle/rsp.c
    ${lazyusf2_SOURCE_DIR}/si/*.c
    ${lazyusf2_SOURCE_DIR}/usf/*.c
    ${lazyusf2_SOURCE_DIR}/vi/*.c
)
add_library(usf_static STATIC ${USF_SOURCES})
target_include_directories(usf_static PRIVATE
    ${lazyusf2_SOURCE_DIR}
    ${lazyusf2_SOURCE_DIR}/ai
    ${lazyusf2_SOURCE_DIR}/api
    ${lazyusf2_SOURCE_DIR}/debugger
    ${lazyusf2_SOURCE_DIR}/main
    ${lazyusf2_SOURCE_DIR}/memory
    ${lazyusf2_SOURCE_DIR}/osal
    ${lazyusf2_SOURCE_DIR}/pi
    ${lazyusf2_SOURCE_DIR}/r4300
    ${lazyusf2_SOURCE_DIR}/rdp
    ${lazyusf2_SOURCE_DIR}/ri
    ${lazyusf2_SOURCE_DIR}/rsp
    ${lazyusf2_SOURCE_DIR}/rsp_hle
    ${lazyusf2_SOURCE_DIR}/rsp_lle
    ${lazyusf2_SOURCE_DIR}/si
    ${lazyusf2_SOURCE_DIR}/usf
    ${lazyusf2_SOURCE_DIR}/vi
)
target_compile_definitions(usf_static PRIVATE EMU_COMPILE)
# SSE2 intrinsics only available on x86_64
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64|AMD64")
    target_compile_definitions(usf_static PRIVATE ARCH_MIN_SSE2)
endif()
target_link_libraries(usf_static PRIVATE ZLIB::ZLIB)
suppress_external_warnings(usf_static)
target_compile_options(usf_static PRIVATE -Wno-error=return-mismatch -Wno-error=implicit-function-declaration -Wno-error=return-type -fvisibility=hidden)
set_target_properties(usf_static PROPERTIES POSITION_INDEPENDENT_CODE ON)

# =====================================================================================
# highly_theoretical - Sega Saturn/Dreamcast emulation for SSF/DSF
# =====================================================================================

download_library(
    NAME highly_theoretical
    URL "https://gitlab.com/kode54/highly_theoretical/-/archive/0e4c18c5b757b04dbcb68c572c5a4f6fd803283c/highly_theoretical-0e4c18c5b757b04dbcb68c572c5a4f6fd803283c.tar.gz"
    FILENAME "highly_theoretical-0e4c18c5b757b04dbcb68c572c5a4f6fd803283c.tar.gz"
    HASH "8a96b64fa00888ea58e9469abd714cb3ed7ea0a5d1da38c1bac276637e8604da"
    EXTRACT_DIR "highly_theoretical-0e4c18c5b757b04dbcb68c572c5a4f6fd803283c"
    PATCHES patches/highly_theoretical-arm64-fastcall.patch
)

file(GLOB HT_SOURCES
    ${highly_theoretical_SOURCE_DIR}/Core/*.c
    ${highly_theoretical_SOURCE_DIR}/Core/c68k/*.c
)
add_library(ht_static STATIC ${HT_SOURCES})
target_include_directories(ht_static PUBLIC
    ${highly_theoretical_SOURCE_DIR}/Core
    ${highly_theoretical_SOURCE_DIR}/Core/c68k
)
target_compile_definitions(ht_static PRIVATE
    EMU_COMPILE EMU_LITTLE_ENDIAN HAVE_STDINT_H LSB_FIRST C68K_NO_JUMP_TABLE NO_DYNAREC
)
target_link_libraries(ht_static PRIVATE ZLIB::ZLIB)
suppress_external_warnings(ht_static)
target_compile_options(ht_static PRIVATE -Wno-error=int-conversion -fvisibility=hidden)
set_target_properties(ht_static PROPERTIES POSITION_INDEPENDENT_CODE ON)

# =====================================================================================
# highly_quixotic - Capcom QSound emulation for QSF
# =====================================================================================

download_library(
    NAME highly_quixotic
    URL "https://gitlab.com/kode54/highly_quixotic/-/archive/d730174d436e8cabca90fabdb3ad4ddf614d31cc/highly_quixotic-d730174d436e8cabca90fabdb3ad4ddf614d31cc.tar.gz"
    FILENAME "highly_quixotic-d730174d436e8cabca90fabdb3ad4ddf614d31cc.tar.gz"
    HASH "8204b5d2692271a49b00316b5d47e6a2ef96aba1f438160ec404c3619ff58e22"
    EXTRACT_DIR "highly_quixotic-d730174d436e8cabca90fabdb3ad4ddf614d31cc"
)

file(GLOB HQ_SOURCES ${highly_quixotic_SOURCE_DIR}/Core/*.c)
add_library(hq_static STATIC ${HQ_SOURCES})
target_include_directories(hq_static PUBLIC ${highly_quixotic_SOURCE_DIR}/Core)
target_compile_definitions(hq_static PRIVATE EMU_COMPILE EMU_LITTLE_ENDIAN)
target_link_libraries(hq_static PRIVATE ZLIB::ZLIB)
suppress_external_warnings(hq_static)
target_compile_options(hq_static PRIVATE -fvisibility=hidden)
set_target_properties(hq_static PROPERTIES POSITION_INDEPENDENT_CODE ON)

# =====================================================================================
# viogsf - Game Boy Advance emulation for GSF
# =====================================================================================

download_library(
    NAME viogsf
    URL "https://github.com/kode54/viogsf/archive/6c43a9926a6a85fbb736ea8f5f7f6c4f59ed3d64.tar.gz"
    FILENAME "viogsf-6c43a9926a6a85fbb736ea8f5f7f6c4f59ed3d64.tar.gz"
    HASH "bd73cf13af29516b556e807ea91d8b03a3711538078a400fecfe5f961f0576c2"
    EXTRACT_DIR "viogsf-6c43a9926a6a85fbb736ea8f5f7f6c4f59ed3d64"
    PATCHES patches/viogsf-arm64-regparm.patch
)

file(GLOB VIOGSF_SOURCES
    ${viogsf_SOURCE_DIR}/vbam/apu/*.cpp
    ${viogsf_SOURCE_DIR}/vbam/gba/*.cpp
)
add_library(viogsf_static STATIC ${VIOGSF_SOURCES})
target_include_directories(viogsf_static PUBLIC
    ${viogsf_SOURCE_DIR}/vbam/gba
    ${viogsf_SOURCE_DIR}/vbam/apu
    ${viogsf_SOURCE_DIR}/vbam/common
)
suppress_external_warnings(viogsf_static)
target_compile_options(viogsf_static PRIVATE -fvisibility=hidden)
set_target_properties(viogsf_static PROPERTIES POSITION_INDEPENDENT_CODE ON)

# =====================================================================================
# snsf9x - Super Nintendo emulation for SNSF
# =====================================================================================

download_library(
    NAME snsf9x
    URL "https://github.com/loveemu/snsf9x/archive/128cb0d500c98caa815da117eb0339873cf157ae.tar.gz"
    FILENAME "snsf9x-128cb0d500c98caa815da117eb0339873cf157ae.tar.gz"
    HASH "fd85c7185bfd38d57c115af6a790ff98272f339333d8e9a7cd9078e4e1808e05"
    EXTRACT_DIR "snsf9x-128cb0d500c98caa815da117eb0339873cf157ae"
)

file(GLOB SNSF9X_SOURCES
    ${snsf9x_SOURCE_DIR}/snsf9x/*.cpp
    ${snsf9x_SOURCE_DIR}/snsf9x/snes9x/*.cpp
    ${snsf9x_SOURCE_DIR}/snsf9x/snes9x/apu/*.cpp
)
add_library(snsf9x_static STATIC ${SNSF9X_SOURCES})
target_include_directories(snsf9x_static PUBLIC
    ${snsf9x_SOURCE_DIR}/snsf9x
    ${snsf9x_SOURCE_DIR}/snsf9x/snes9x
    ${snsf9x_SOURCE_DIR}/snsf9x/snes9x/apu
)
target_compile_definitions(snsf9x_static PRIVATE HAVE_STDINT_H)
if(NOT WIN32)
    target_compile_definitions(snsf9x_static PRIVATE HAVE_STRINGS_H)
endif()
suppress_external_warnings(snsf9x_static)
target_compile_options(snsf9x_static PRIVATE -fvisibility=hidden)
# snsf9x uses 'register' keyword, removed in C++17
set_target_properties(snsf9x_static PROPERTIES POSITION_INDEPENDENT_CODE ON CXX_STANDARD 14)

# =====================================================================================
# xsf_playback - Shared plugin library
# =====================================================================================

add_library(xsf_playback SHARED
    xsf_plugin.c
    xsf_psflib_bridge.c
    xsf_psf.c
    xsf_psf_bios.c
    xsf_2sf.c
    xsf_usf.c
    xsf_ssf.c
    xsf_qsf.c
    xsf_gsf.cpp
    xsf_snsf.cpp
)

target_include_directories(xsf_playback PRIVATE
    ${RETROVERT_INCLUDE_DIR}
    ${psflib_SOURCE_DIR}
    ${highly_experimental_SOURCE_DIR}/Core
    ${vio2sf_SOURCE_DIR}/src/vio2sf/desmume
    ${lazyusf2_SOURCE_DIR}
    ${lazyusf2_SOURCE_DIR}/usf
    ${highly_theoretical_SOURCE_DIR}/Core
    ${highly_quixotic_SOURCE_DIR}/Core
    ${viogsf_SOURCE_DIR}/vbam/gba
    ${viogsf_SOURCE_DIR}/vbam/apu
    ${viogsf_SOURCE_DIR}/vbam/common
    ${snsf9x_SOURCE_DIR}/snsf9x
    ${snsf9x_SOURCE_DIR}/snsf9x/snes9x
    ${snsf9x_SOURCE_DIR}/snsf9x/snes9x/apu
)

target_link_libraries(xsf_playback PRIVATE
    psflib_static
    he_static
    vio2sf_static
    usf_static
    ht_static
    hq_static
    viogsf_static
    snsf9x_static
    ZLIB::ZLIB
)

# C++ libraries (vio2sf, viogsf, snsf9x) need stdc++ on Unix
if(UNIX)
    target_link_libraries(xsf_playback PRIVATE stdc++)
endif()

target_compile_options(xsf_playback PRIVATE
    -Wno-unused-parameter
    -Wno-sign-compare
    -Wno-macro-redefined
    -Wno-inconsistent-dllimport
)

set_target_properties(xsf_playback PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${output_dir}"
    RUNTIME_OUTPUT_DIRECTORY "${output_dir}"
    PREFIX ""
    SUFFIX "${PLAYBACK_PLUGIN_SUFFIX}"
    VERSION "1.0.0"
    SOVERSION 1
)

install(TARGETS xsf_playback
    LIBRARY DESTINATION plugins
    RUNTIME DESTINATION plugins
)

message(STATUS "xSF playback plugin configured (PSF/PSF2, SSF/DSF, USF, GSF, SNSF, 2SF, QSF)")
