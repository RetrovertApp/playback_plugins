# ZXTune Playback Plugin
#
# This plugin uses ZXTune to decode ZX Spectrum and other chiptune formats
# (PT3, PT2, AY, STC, STP, VTX, etc.)
# Based on ZXTune r5100 by Vitamin/CAIG

cmake_minimum_required(VERSION 3.16)

enable_language(CXX)

# Download ZXTune r5100
download_library(
    NAME zxtune
    URL "https://github.com/vitamin-caig/zxtune/archive/refs/tags/r5100.tar.gz"
    FILENAME "zxtune-r5100.tar.gz"
    HASH "2123cc21bc422eaec3ae358246ebe518bc7b9066d6f0726d2eb7a264ff9359fb"
    EXTRACT_DIR "zxtune-r5100"
    PATCHES patches/fix-shared-ptr-unique-cxx20.patch patches/fix-string-view-iterator.patch patches/fix-lexic-analysis-iterator.patch patches/fix-msvc-iterator-types.patch patches/fix-parameters-types-self-include.patch patches/scope-capture.patch
)

set(ZXTUNE_ROOT "${zxtune_SOURCE_DIR}")

find_package(ZLIB REQUIRED)

# Collect source files following BZRPlayer's pattern
file(GLOB ZXTUNE_FMT_SOURCES "${ZXTUNE_ROOT}/3rdparty/fmt/src/format.cc")

file(GLOB ZXTUNE_LHASA_SOURCES
    "${ZXTUNE_ROOT}/3rdparty/lhasa/lib/crc16.c"
    "${ZXTUNE_ROOT}/3rdparty/lhasa/lib/ext_header.c"
    "${ZXTUNE_ROOT}/3rdparty/lhasa/lib/lh1_decoder.c"
    "${ZXTUNE_ROOT}/3rdparty/lhasa/lib/lh5_decoder.c"
    "${ZXTUNE_ROOT}/3rdparty/lhasa/lib/lh6_decoder.c"
    "${ZXTUNE_ROOT}/3rdparty/lhasa/lib/lh7_decoder.c"
    "${ZXTUNE_ROOT}/3rdparty/lhasa/lib/lha_arch_unix.c"
    "${ZXTUNE_ROOT}/3rdparty/lhasa/lib/lha_arch_win32.c"
    "${ZXTUNE_ROOT}/3rdparty/lhasa/lib/lha_basic_reader.c"
    "${ZXTUNE_ROOT}/3rdparty/lhasa/lib/lha_decoder.c"
    "${ZXTUNE_ROOT}/3rdparty/lhasa/lib/lha_endian.c"
    "${ZXTUNE_ROOT}/3rdparty/lhasa/lib/lha_file_header.c"
    "${ZXTUNE_ROOT}/3rdparty/lhasa/lib/lha_input_stream.c"
    "${ZXTUNE_ROOT}/3rdparty/lhasa/lib/lha_reader.c"
    "${ZXTUNE_ROOT}/3rdparty/lhasa/lib/lz5_decoder.c"
    "${ZXTUNE_ROOT}/3rdparty/lhasa/lib/lzs_decoder.c"
    "${ZXTUNE_ROOT}/3rdparty/lhasa/lib/macbinary.c"
    "${ZXTUNE_ROOT}/3rdparty/lhasa/lib/null_decoder.c"
    "${ZXTUNE_ROOT}/3rdparty/lhasa/lib/pm2_decoder.c"
)

file(GLOB ZXTUNE_ANALYSIS_SOURCES "${ZXTUNE_ROOT}/src/analysis/src/*.cpp")
file(GLOB ZXTUNE_BINARY_COMPRESSION_SOURCES "${ZXTUNE_ROOT}/src/binary/compression/src/*.cpp")
file(GLOB ZXTUNE_BINARY_FORMAT_SOURCES "${ZXTUNE_ROOT}/src/binary/format/*.cpp")
file(GLOB ZXTUNE_BINARY_SRC_SOURCES "${ZXTUNE_ROOT}/src/binary/src/*.cpp")

file(GLOB ZXTUNE_CORE_PLUGINS_SOURCES "${ZXTUNE_ROOT}/src/core/plugins/*.cpp")
file(GLOB ZXTUNE_CORE_PLUGINS_ARCHIVES_SOURCES "${ZXTUNE_ROOT}/src/core/plugins/archives/*.cpp")
file(GLOB ZXTUNE_CORE_PLUGINS_ARCHIVES_FULL_SOURCES "${ZXTUNE_ROOT}/src/core/plugins/archives/full/*.cpp")
file(GLOB ZXTUNE_CORE_PLUGINS_PLAYERS_SOURCES "${ZXTUNE_ROOT}/src/core/plugins/players/*.cpp")
file(GLOB ZXTUNE_CORE_PLUGINS_PLAYERS_AY_SOURCES "${ZXTUNE_ROOT}/src/core/plugins/players/ay/*.cpp")
file(GLOB ZXTUNE_CORE_PLUGINS_PLAYERS_DAC_SOURCES "${ZXTUNE_ROOT}/src/core/plugins/players/dac/*.cpp")
file(GLOB ZXTUNE_CORE_PLUGINS_PLAYERS_MULTI_SOURCES "${ZXTUNE_ROOT}/src/core/plugins/players/multi/*.cpp")
file(GLOB ZXTUNE_CORE_PLUGINS_PLAYERS_SAA_SOURCES "${ZXTUNE_ROOT}/src/core/plugins/players/saa/*.cpp")
file(GLOB ZXTUNE_CORE_PLUGINS_PLAYERS_TFM_SOURCES "${ZXTUNE_ROOT}/src/core/plugins/players/tfm/*.cpp")
file(GLOB ZXTUNE_CORE_SRC_SOURCES "${ZXTUNE_ROOT}/src/core/src/*.cpp")

file(GLOB ZXTUNE_DEVICES_AYM_SOURCES "${ZXTUNE_ROOT}/src/devices/aym/src/*.cpp")
file(GLOB ZXTUNE_DEVICES_DAC_SOURCES "${ZXTUNE_ROOT}/src/devices/dac/*.cpp")
file(GLOB ZXTUNE_DEVICES_FM_SOURCES "${ZXTUNE_ROOT}/src/devices/fm/*.cpp")
file(GLOB ZXTUNE_DEVICES_SAA_SOURCES "${ZXTUNE_ROOT}/src/devices/saa/*.cpp")
file(GLOB ZXTUNE_DEVICES_BEEPER_SOURCES "${ZXTUNE_ROOT}/src/devices/beeper/*.cpp")

set(ZXTUNE_DEVICES_Z80_SOURCES
    "${ZXTUNE_ROOT}/src/devices/z80/z80.cpp"
)

# Z80 emulator library (C code, used by devices/z80)
set(ZXTUNE_3RDPARTY_Z80EX_SOURCES
    "${ZXTUNE_ROOT}/3rdparty/z80ex/z80ex.c"
)

set(ZXTUNE_FORMATS_ARCHIVED_SOURCES
    "${ZXTUNE_ROOT}/src/formats/archived/hrip.cpp"
    "${ZXTUNE_ROOT}/src/formats/archived/scl.cpp"
    "${ZXTUNE_ROOT}/src/formats/archived/trd.cpp"
    "${ZXTUNE_ROOT}/src/formats/archived/trdos_catalogue.cpp"
    "${ZXTUNE_ROOT}/src/formats/archived/trdos_utils.cpp"
    "${ZXTUNE_ROOT}/src/formats/archived/zxstate.cpp"
    "${ZXTUNE_ROOT}/src/formats/archived/zxzip.cpp"
)

file(GLOB ZXTUNE_FORMATS_CHIPTUNE_SOURCES "${ZXTUNE_ROOT}/src/formats/chiptune/*.cpp")
file(GLOB ZXTUNE_FORMATS_CHIPTUNE_AYM_SOURCES "${ZXTUNE_ROOT}/src/formats/chiptune/aym/*.cpp")
file(GLOB ZXTUNE_FORMATS_CHIPTUNE_DIGITAL_SOURCES "${ZXTUNE_ROOT}/src/formats/chiptune/digital/*.cpp")
file(GLOB ZXTUNE_FORMATS_CHIPTUNE_FM_SOURCES "${ZXTUNE_ROOT}/src/formats/chiptune/fm/*.cpp")
file(GLOB ZXTUNE_FORMATS_CHIPTUNE_MULTIDEVICE_SOURCES "${ZXTUNE_ROOT}/src/formats/chiptune/multidevice/*.cpp")
file(GLOB ZXTUNE_FORMATS_CHIPTUNE_MULTITRACK_SOURCES "${ZXTUNE_ROOT}/src/formats/chiptune/multitrack/*.cpp")
file(GLOB ZXTUNE_FORMATS_CHIPTUNE_SAA_SOURCES "${ZXTUNE_ROOT}/src/formats/chiptune/saa/*.cpp")

# AY Emul format parser (only ay.cpp from emulation/ — other files are for xSF/SPC/KSS handled by other plugins)
set(ZXTUNE_FORMATS_CHIPTUNE_EMULATION_SOURCES
    "${ZXTUNE_ROOT}/src/formats/chiptune/emulation/ay.cpp"
)
file(GLOB ZXTUNE_FORMATS_MULTITRACK_SOURCES "${ZXTUNE_ROOT}/src/formats/multitrack/*.cpp")

set(ZXTUNE_FORMATS_PACKED_SOURCES
    "${ZXTUNE_ROOT}/src/formats/packed/charpres.cpp"
    "${ZXTUNE_ROOT}/src/formats/packed/codecruncher3.cpp"
    "${ZXTUNE_ROOT}/src/formats/packed/compiledasc.cpp"
    "${ZXTUNE_ROOT}/src/formats/packed/compiledpt2.cpp"
    "${ZXTUNE_ROOT}/src/formats/packed/compiledptu13.cpp"
    "${ZXTUNE_ROOT}/src/formats/packed/compiledst3.cpp"
    "${ZXTUNE_ROOT}/src/formats/packed/compiledstp.cpp"
    "${ZXTUNE_ROOT}/src/formats/packed/compressorcode4.cpp"
    "${ZXTUNE_ROOT}/src/formats/packed/container.cpp"
    "${ZXTUNE_ROOT}/src/formats/packed/datasquieezer.cpp"
    "${ZXTUNE_ROOT}/src/formats/packed/dsk.cpp"
    "${ZXTUNE_ROOT}/src/formats/packed/esvcruncher.cpp"
    "${ZXTUNE_ROOT}/src/formats/packed/fulldiskimage.cpp"
    "${ZXTUNE_ROOT}/src/formats/packed/gamepacker.cpp"
    "${ZXTUNE_ROOT}/src/formats/packed/hobeta.cpp"
    "${ZXTUNE_ROOT}/src/formats/packed/hrum.cpp"
    "${ZXTUNE_ROOT}/src/formats/packed/hrust1.cpp"
    "${ZXTUNE_ROOT}/src/formats/packed/hrust2.cpp"
    "${ZXTUNE_ROOT}/src/formats/packed/image_utils.cpp"
    "${ZXTUNE_ROOT}/src/formats/packed/lha_file.cpp"
    "${ZXTUNE_ROOT}/src/formats/packed/lzh.cpp"
    "${ZXTUNE_ROOT}/src/formats/packed/lzs.cpp"
    "${ZXTUNE_ROOT}/src/formats/packed/megalz.cpp"
    "${ZXTUNE_ROOT}/src/formats/packed/mspack.cpp"
    "${ZXTUNE_ROOT}/src/formats/packed/pack2.cpp"
    "${ZXTUNE_ROOT}/src/formats/packed/powerfullcodedecreaser6.cpp"
    "${ZXTUNE_ROOT}/src/formats/packed/sna128.cpp"
    "${ZXTUNE_ROOT}/src/formats/packed/teledisk.cpp"
    "${ZXTUNE_ROOT}/src/formats/packed/trush.cpp"
    "${ZXTUNE_ROOT}/src/formats/packed/turbolz.cpp"
    "${ZXTUNE_ROOT}/src/formats/packed/z80.cpp"
    "${ZXTUNE_ROOT}/src/formats/packed/zxzip_file.cpp"
)

file(GLOB ZXTUNE_MODULE_PLAYERS_SOURCES "${ZXTUNE_ROOT}/src/module/players/*.cpp")
file(GLOB ZXTUNE_MODULE_PLAYERS_AYM_SOURCES "${ZXTUNE_ROOT}/src/module/players/aym/*.cpp")
file(GLOB ZXTUNE_MODULE_PLAYERS_DAC_SOURCES "${ZXTUNE_ROOT}/src/module/players/dac/*.cpp")
file(GLOB ZXTUNE_MODULE_PLAYERS_SAA_SOURCES "${ZXTUNE_ROOT}/src/module/players/saa/*.cpp")
file(GLOB ZXTUNE_MODULE_PLAYERS_TFM_SOURCES "${ZXTUNE_ROOT}/src/module/players/tfm/*.cpp")
file(GLOB ZXTUNE_MODULE_SRC_SOURCES "${ZXTUNE_ROOT}/src/module/src/*.cpp")

file(GLOB ZXTUNE_PARAMETERS_SOURCES "${ZXTUNE_ROOT}/src/parameters/src/*.cpp")
file(GLOB ZXTUNE_SOUND_IMPL_SOURCES "${ZXTUNE_ROOT}/src/sound/impl/*.cpp")
file(GLOB ZXTUNE_STRINGS_SOURCES "${ZXTUNE_ROOT}/src/strings/src/*.cpp")
file(GLOB ZXTUNE_TOOLS_SOURCES "${ZXTUNE_ROOT}/src/tools/src/*.cpp")

set(ZXTUNE_ALL_SOURCES
    ${ZXTUNE_FMT_SOURCES}
    ${ZXTUNE_LHASA_SOURCES}
    ${ZXTUNE_ANALYSIS_SOURCES}
    ${ZXTUNE_BINARY_COMPRESSION_SOURCES}
    ${ZXTUNE_BINARY_FORMAT_SOURCES}
    ${ZXTUNE_BINARY_SRC_SOURCES}
    ${ZXTUNE_CORE_PLUGINS_SOURCES}
    ${ZXTUNE_CORE_PLUGINS_ARCHIVES_SOURCES}
    ${ZXTUNE_CORE_PLUGINS_ARCHIVES_FULL_SOURCES}
    ${ZXTUNE_CORE_PLUGINS_PLAYERS_SOURCES}
    ${ZXTUNE_CORE_PLUGINS_PLAYERS_AY_SOURCES}
    ${ZXTUNE_CORE_PLUGINS_PLAYERS_DAC_SOURCES}
    ${ZXTUNE_CORE_PLUGINS_PLAYERS_MULTI_SOURCES}
    ${ZXTUNE_CORE_PLUGINS_PLAYERS_SAA_SOURCES}
    ${ZXTUNE_CORE_PLUGINS_PLAYERS_TFM_SOURCES}
    ${ZXTUNE_CORE_SRC_SOURCES}
    ${ZXTUNE_DEVICES_AYM_SOURCES}
    ${ZXTUNE_DEVICES_BEEPER_SOURCES}
    ${ZXTUNE_DEVICES_DAC_SOURCES}
    ${ZXTUNE_DEVICES_FM_SOURCES}
    ${ZXTUNE_DEVICES_SAA_SOURCES}
    ${ZXTUNE_DEVICES_Z80_SOURCES}
    ${ZXTUNE_3RDPARTY_Z80EX_SOURCES}
    ${ZXTUNE_FORMATS_ARCHIVED_SOURCES}
    ${ZXTUNE_FORMATS_CHIPTUNE_SOURCES}
    ${ZXTUNE_FORMATS_CHIPTUNE_AYM_SOURCES}
    ${ZXTUNE_FORMATS_CHIPTUNE_DIGITAL_SOURCES}
    ${ZXTUNE_FORMATS_CHIPTUNE_FM_SOURCES}
    ${ZXTUNE_FORMATS_CHIPTUNE_MULTIDEVICE_SOURCES}
    ${ZXTUNE_FORMATS_CHIPTUNE_MULTITRACK_SOURCES}
    ${ZXTUNE_FORMATS_CHIPTUNE_SAA_SOURCES}
    ${ZXTUNE_FORMATS_CHIPTUNE_EMULATION_SOURCES}
    ${ZXTUNE_FORMATS_MULTITRACK_SOURCES}
    ${ZXTUNE_FORMATS_PACKED_SOURCES}
    ${ZXTUNE_MODULE_PLAYERS_SOURCES}
    ${ZXTUNE_MODULE_PLAYERS_AYM_SOURCES}
    ${ZXTUNE_MODULE_PLAYERS_DAC_SOURCES}
    ${ZXTUNE_MODULE_PLAYERS_SAA_SOURCES}
    ${ZXTUNE_MODULE_PLAYERS_TFM_SOURCES}
    ${ZXTUNE_MODULE_SRC_SOURCES}
    ${ZXTUNE_PARAMETERS_SOURCES}
    ${ZXTUNE_SOUND_IMPL_SOURCES}
    ${ZXTUNE_STRINGS_SOURCES}
    ${ZXTUNE_TOOLS_SOURCES}
)

# Exclude files we don't need or that conflict with our local replacements
# Player formats handled by other dedicated plugins (not ZXTune-native)
list(FILTER ZXTUNE_ALL_SOURCES EXCLUDE REGEX "ahx_supp\\.cpp$")
list(FILTER ZXTUNE_ALL_SOURCES EXCLUDE REGEX "v2m_supp\\.cpp$")
list(FILTER ZXTUNE_ALL_SOURCES EXCLUDE REGEX "multitrack_plugin\\.cpp$")
list(FILTER ZXTUNE_ALL_SOURCES EXCLUDE REGEX "abysshighestexperience\\.cpp$")
list(FILTER ZXTUNE_ALL_SOURCES EXCLUDE REGEX "v2m\\.cpp$")
list(FILTER ZXTUNE_ALL_SOURCES EXCLUDE REGEX "resampler\\.cpp$")
# Replace upstream plugin registration with local versions that only register native ZXTune formats
# (upstream registers ALL plugins including GME, SID, VGM, OpenMPT, xSF, etc.)
list(FILTER ZXTUNE_ALL_SOURCES EXCLUDE REGEX "plugins_list\\.cpp$")
list(FILTER ZXTUNE_ALL_SOURCES EXCLUDE REGEX "plugins_archived\\.cpp$")
list(FILTER ZXTUNE_ALL_SOURCES EXCLUDE REGEX "plugins_packed\\.cpp$")
list(FILTER ZXTUNE_ALL_SOURCES EXCLUDE REGEX "archives/full/plugins\\.cpp$")

set(output_dir "${RETROVERT_PLUGIN_OUTPUT_DIR}")

add_library(zxtune_playback SHARED
    zxtune_plugin.cpp
    zxtune_plugins_list.cpp
    zxtune_archive_plugins.cpp
    ${ZXTUNE_ALL_SOURCES}
)

set_target_properties(zxtune_playback PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)

target_include_directories(zxtune_playback PRIVATE
    ${RETROVERT_INCLUDE_DIR}
    ${ZXTUNE_ROOT}
    ${ZXTUNE_ROOT}/3rdparty/fmt/include
    ${ZXTUNE_ROOT}/3rdparty/lhasa/lib
    ${ZXTUNE_ROOT}/include
    ${ZXTUNE_ROOT}/src
    ${ZXTUNE_ROOT}/src/module
    ${ZXTUNE_ROOT}/3rdparty/z80ex
    ${ZXTUNE_ROOT}/3rdparty/z80ex/include
)

target_compile_definitions(zxtune_playback PRIVATE
    NO_L10N
    NO_DEBUG_LOGS
    SOURCES_ROOT="${ZXTUNE_ROOT}"
    FMT_CONSTEVAL=  # Disable fmt consteval for Clang compatibility
    # z80ex library definitions
    WORDS_LITTLE_ENDIAN
    Z80EX_VERSION_STR="1.1.19pre1"
    Z80EX_API_REVISION=1
    Z80EX_VERSION_MAJOR=1
    Z80EX_VERSION_MINOR=19
    Z80EX_RELEASE_TYPE="pre1"
)

# ZXTune requires unsigned char for correct byte handling
if(NOT MSVC)
    target_compile_options(zxtune_playback PRIVATE -funsigned-char)
endif()
suppress_external_warnings(zxtune_playback)

target_link_libraries(zxtune_playback PRIVATE ZLIB::ZLIB)

set_target_properties(zxtune_playback PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${output_dir}"
    RUNTIME_OUTPUT_DIRECTORY "${output_dir}"
    PREFIX ""
    SUFFIX "${PLAYBACK_PLUGIN_SUFFIX}"
    VERSION "1.0.0"
    SOVERSION 1
)

install(TARGETS zxtune_playback
    LIBRARY DESTINATION plugins
    RUNTIME DESTINATION plugins
)

message(STATUS "ZXTune playback plugin configured (ZXTune r5100)")
