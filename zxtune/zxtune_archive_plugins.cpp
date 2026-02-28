/**
 * Custom archive plugins registration for ZXTune playback plugin.
 *
 * This replaces the upstream archives/full/plugins.cpp, archives/plugins_archived.cpp,
 * and archives/plugins_packed.cpp. Only ZX-native archive formats are registered.
 *
 * Excluded (decoders not compiled):
 *   - Generic archives: ZIP, RAR, LHA, UMX, 7ZIP (need external libraries)
 *   - Multitrack containers: AY (decoder not compiled)
 *   - Chiptune packers: GZIP, MUSE (decoders not compiled)
 */

#include "core/plugins/archive_plugins_registrator.h"
#include "core/plugins/archives/archived.h"
#include "core/plugins/archives/packed.h"
#include "core/plugins/archives/plugins.h"
#include "core/plugins/archives/plugins_list.h"
#include "formats/archived/decoders.h"
#include "formats/packed/decoders.h"

#include "core/plugin_attrs.h"

namespace ZXTune {
// === Archived containers (ZX-native only) ===

using CreateArchivedDecoderFunc = Formats::Archived::Decoder::Ptr (*)();

struct ContainerPluginDescription {
    const PluginId Id;
    const CreateArchivedDecoderFunc Create;
    const uint_t Caps;
};

using namespace Formats::Archived;

// Only ZX-specific archives (decoders are compiled in ZXTUNE_FORMATS_ARCHIVED_SOURCES)
const ContainerPluginDescription ZXUNARCHIVES[] = {
    { "TRD"_id, &CreateTRDDecoder, Capabilities::Container::Type::DISKIMAGE | Capabilities::Container::Traits::PLAIN },
    { "SCL"_id, &CreateSCLDecoder, Capabilities::Container::Type::DISKIMAGE | Capabilities::Container::Traits::PLAIN },
    { "HRIP"_id, &CreateHripDecoder, Capabilities::Container::Type::ARCHIVE },
    { "ZXZIP"_id, &CreateZXZipDecoder, Capabilities::Container::Type::ARCHIVE },
    { "ZXSTATE"_id, &CreateZXStateDecoder, Capabilities::Container::Type::SNAPSHOT },
};

static void RegisterPlugin(const ContainerPluginDescription& desc, ArchivePluginsRegistrator& registrator) {
    auto decoder = desc.Create();
    auto plugin = CreateArchivePlugin(desc.Id, desc.Caps, std::move(decoder));
    registrator.RegisterPlugin(std::move(plugin));
}

void RegisterZXArchiveContainers(ArchivePluginsRegistrator& registrator) {
    for (const auto& desc : ZXUNARCHIVES) {
        RegisterPlugin(desc, registrator);
    }
}

// Stubs for excluded functions (referenced by plugins.h but not needed)
void RegisterMultitrackContainers(ArchivePluginsRegistrator& /*registrator*/) {}
void RegisterArchiveContainers(ArchivePluginsRegistrator& /*registrator*/) {}

// === Packed containers (depackers + decompilers only, no gzip/muse) ===

using CreatePackedDecoderFunc = Formats::Packed::Decoder::Ptr (*)();

struct ArchivePluginDescription {
    const PluginId Id;
    const CreatePackedDecoderFunc Create;
    const uint_t Caps;
};

using namespace Formats::Packed;

// clang-format off
  const ArchivePluginDescription DEPACKERS[] =
  {
    {"HOBETA"_id,   &CreateHobetaDecoder,                    Capabilities::Container::Type::ARCHIVE | Capabilities::Container::Traits::PLAIN},
    {"HRUST1"_id,   &CreateHrust1Decoder,                    Capabilities::Container::Type::COMPRESSOR},
    {"HRUST2"_id,   &CreateHrust21Decoder,                   Capabilities::Container::Type::COMPRESSOR},
    {"HRUST23"_id,  &CreateHrust23Decoder,                   Capabilities::Container::Type::COMPRESSOR},
    {"FDI"_id,      &CreateFullDiskImageDecoder,             Capabilities::Container::Type::DISKIMAGE},
    {"DSQ"_id,      &CreateDataSquieezerDecoder,             Capabilities::Container::Type::COMPRESSOR},
    {"MSP"_id,      &CreateMSPackDecoder,                    Capabilities::Container::Type::COMPRESSOR},
    {"TRUSH"_id,    &CreateTRUSHDecoder,                     Capabilities::Container::Type::COMPRESSOR},
    {"LZS"_id,      &CreateLZSDecoder,                       Capabilities::Container::Type::COMPRESSOR},
    {"PCD61"_id,    &CreatePowerfullCodeDecreaser61Decoder,  Capabilities::Container::Type::COMPRESSOR},
    {"PCD61i"_id,   &CreatePowerfullCodeDecreaser61iDecoder, Capabilities::Container::Type::COMPRESSOR},
    {"PCD62"_id,    &CreatePowerfullCodeDecreaser62Decoder,  Capabilities::Container::Type::COMPRESSOR},
    {"HRUM"_id,     &CreateHrumDecoder,                      Capabilities::Container::Type::COMPRESSOR},
    {"CC3"_id,      &CreateCodeCruncher3Decoder,             Capabilities::Container::Type::COMPRESSOR},
    {"CC4"_id,      &CreateCompressorCode4Decoder,           Capabilities::Container::Type::COMPRESSOR},
    {"CC4PLUS"_id,  &CreateCompressorCode4PlusDecoder,       Capabilities::Container::Type::COMPRESSOR},
    {"ESV"_id,      &CreateESVCruncherDecoder,               Capabilities::Container::Type::COMPRESSOR},
    {"GAM"_id,      &CreateGamePackerDecoder,                Capabilities::Container::Type::COMPRESSOR},
    {"GAMPLUS"_id,  &CreateGamePackerPlusDecoder,            Capabilities::Container::Type::COMPRESSOR},
    {"TLZ"_id,      &CreateTurboLZDecoder,                   Capabilities::Container::Type::COMPRESSOR},
    {"TLZP"_id,     &CreateTurboLZProtectedDecoder,          Capabilities::Container::Type::COMPRESSOR},
    {"CHARPRES"_id, &CreateCharPresDecoder,                  Capabilities::Container::Type::COMPRESSOR},
    {"PACK2"_id,    &CreatePack2Decoder,                     Capabilities::Container::Type::COMPRESSOR},
    {"LZH1"_id,     &CreateLZH1Decoder,                      Capabilities::Container::Type::COMPRESSOR},
    {"LZH2"_id,     &CreateLZH2Decoder,                      Capabilities::Container::Type::COMPRESSOR},
    {"SNA128"_id,   &CreateSna128Decoder,                    Capabilities::Container::Type::SNAPSHOT},
    {"TD0"_id,      &CreateTeleDiskImageDecoder,             Capabilities::Container::Type::DISKIMAGE},
    {"Z80V145"_id,  &CreateZ80V145Decoder ,                  Capabilities::Container::Type::SNAPSHOT},
    {"Z80V20"_id,   &CreateZ80V20Decoder,                    Capabilities::Container::Type::SNAPSHOT},
    {"Z80V30"_id,   &CreateZ80V30Decoder,                    Capabilities::Container::Type::SNAPSHOT},
    {"MEGALZ"_id,   &CreateMegaLZDecoder,                    Capabilities::Container::Type::COMPRESSOR},
    {"DSK"_id,      &CreateDSKDecoder,                       Capabilities::Container::Type::DISKIMAGE},
  };

  const ArchivePluginDescription DECOMPILERS[] =
  {
    {"COMPILEDASC0"_id,  &CreateCompiledASC0Decoder,  Capabilities::Container::Type::DECOMPILER},
    {"COMPILEDASC1"_id,  &CreateCompiledASC1Decoder,  Capabilities::Container::Type::DECOMPILER},
    {"COMPILEDASC2"_id,  &CreateCompiledASC2Decoder,  Capabilities::Container::Type::DECOMPILER},
    {"COMPILEDPT24"_id,  &CreateCompiledPT24Decoder,  Capabilities::Container::Type::DECOMPILER},
    {"COMPILEDPTU13"_id, &CreateCompiledPTU13Decoder, Capabilities::Container::Type::DECOMPILER},
    {"COMPILEDST3"_id,   &CreateCompiledST3Decoder,   Capabilities::Container::Type::DECOMPILER},
    {"COMPILEDSTP1"_id,  &CreateCompiledSTP1Decoder,  Capabilities::Container::Type::DECOMPILER},
    {"COMPILEDSTP2"_id,  &CreateCompiledSTP2Decoder,  Capabilities::Container::Type::DECOMPILER},
  };
// clang-format on

static void RegisterPlugin(const ArchivePluginDescription& desc, ArchivePluginsRegistrator& registrator) {
    auto decoder = desc.Create();
    auto plugin = CreateArchivePlugin(desc.Id, desc.Caps, std::move(decoder));
    registrator.RegisterPlugin(std::move(plugin));
}

void RegisterDepackPlugins(ArchivePluginsRegistrator& registrator) {
    for (const auto& desc : DEPACKERS) {
        RegisterPlugin(desc, registrator);
    }
}

// Stub for excluded chiptune packers (gzip, muse decoders not compiled)
void RegisterChiptunePackerPlugins(ArchivePluginsRegistrator& /*registrator*/) {}

void RegisterDecompilePlugins(ArchivePluginsRegistrator& registrator) {
    for (const auto& desc : DECOMPILERS) {
        RegisterPlugin(desc, registrator);
    }
}

// === Main archive registration (replaces full/plugins.cpp) ===

void RegisterArchivePlugins(ArchivePluginsRegistrator& registrator) {
    RegisterRawContainer(registrator);

    RegisterZXArchiveContainers(registrator);
    RegisterZdataContainer(registrator);

    RegisterDepackPlugins(registrator);
    RegisterDecompilePlugins(registrator);
}
} // namespace ZXTune
