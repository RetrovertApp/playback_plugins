/**
 * Custom plugins list for ZXTune playback plugin.
 *
 * This replaces the upstream plugins_list.cpp which registers ALL plugins
 * including external library backends (GME, SID, VGM, OpenMPT, xSF, etc.)
 * that we handle via separate dedicated playback plugins.
 *
 * Only native ZXTune formats (AY chip family, DAC, SAA, TFM) are registered here.
 */

#include "core/plugins/players/plugins_list.h"

namespace ZXTune {
// AY chip family formats (from players/ay/)
void RegisterTSSupport(PlayerPluginsRegistrator& players);
void RegisterPSGSupport(PlayerPluginsRegistrator& players);
void RegisterSTCSupport(PlayerPluginsRegistrator& players);
void RegisterST1Support(PlayerPluginsRegistrator& players);
void RegisterST3Support(PlayerPluginsRegistrator& players);
void RegisterPT2Support(PlayerPluginsRegistrator& players);
void RegisterPT3Support(PlayerPluginsRegistrator& players);
void RegisterASCSupport(PlayerPluginsRegistrator& players);
void RegisterSTPSupport(PlayerPluginsRegistrator& players);
void RegisterTXTSupport(PlayerPluginsRegistrator& players); // defined in pt3_supp.cpp
void RegisterPSMSupport(PlayerPluginsRegistrator& players);
void RegisterGTRSupport(PlayerPluginsRegistrator& players);
void RegisterPT1Support(PlayerPluginsRegistrator& players);
void RegisterVTXSupport(PlayerPluginsRegistrator& players);
void RegisterYMSupport(PlayerPluginsRegistrator& players); // defined in vtx_supp.cpp
void RegisterSQTSupport(PlayerPluginsRegistrator& players);
void RegisterPSCSupport(PlayerPluginsRegistrator& players);
void RegisterFTCSupport(PlayerPluginsRegistrator& players);
void RegisterAYCSupport(PlayerPluginsRegistrator& players);
void RegisterAYSupport(PlayerPluginsRegistrator& players);

// DAC formats (from players/dac/, excluding ahx_supp and v2m_supp)
void RegisterPDTSupport(PlayerPluginsRegistrator& players);
void RegisterCHISupport(PlayerPluginsRegistrator& players);
void RegisterSTRSupport(PlayerPluginsRegistrator& players);
void RegisterDSTSupport(PlayerPluginsRegistrator& players);
void RegisterSQDSupport(PlayerPluginsRegistrator& players);
void RegisterDMMSupport(PlayerPluginsRegistrator& players);
void RegisterET1Support(PlayerPluginsRegistrator& players);

// Multi-device formats (from players/multi/)
void RegisterMTCSupport(PlayerPluginsRegistrator& players);

// SAA formats (from players/saa/)
void RegisterCOPSupport(PlayerPluginsRegistrator& players);

// TFM formats (from players/tfm/)
void RegisterTFDSupport(PlayerPluginsRegistrator& players);
void RegisterTFCSupport(PlayerPluginsRegistrator& players);
void RegisterTFESupport(PlayerPluginsRegistrator& players);

void RegisterPlayerPlugins(PlayerPluginsRegistrator& players, ArchivePluginsRegistrator& /*archives*/) {
    // AY chip family
    RegisterTSSupport(players);
    RegisterPT3Support(players);
    RegisterPT2Support(players);
    RegisterSTCSupport(players);
    RegisterST1Support(players);
    RegisterST3Support(players);
    RegisterASCSupport(players);
    RegisterSTPSupport(players);
    RegisterTXTSupport(players);
    RegisterPSGSupport(players);
    RegisterPSMSupport(players);
    RegisterGTRSupport(players);
    RegisterPT1Support(players);
    RegisterVTXSupport(players);
    RegisterYMSupport(players);
    RegisterSQTSupport(players);
    RegisterPSCSupport(players);
    RegisterFTCSupport(players);
    RegisterAYCSupport(players);
    RegisterAYSupport(players);

    // DAC
    RegisterPDTSupport(players);
    RegisterCHISupport(players);
    RegisterSTRSupport(players);
    RegisterDSTSupport(players);
    RegisterSQDSupport(players);
    RegisterDMMSupport(players);
    RegisterET1Support(players);

    // Multi-device
    RegisterMTCSupport(players);

    // SAA
    RegisterCOPSupport(players);

    // TFM
    RegisterTFDSupport(players);
    RegisterTFCSupport(players);
    RegisterTFESupport(players);
}
} // namespace ZXTune
