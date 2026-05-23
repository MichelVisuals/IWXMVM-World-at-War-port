#pragma once
#include "StdInclude.hpp"
#include "Addresses.hpp"
#include "../src/Utilities/Patches.hpp"

namespace IWXMVM::T4::Patches
{
    using namespace IWXMVM::Patches;

    struct T4Patches
    {
        static constexpr std::array CG_RegisterItemsBytes{
            "33 F6 2B D0 8A 08 88 0C 02 40 46 81 FE 80 00 00 00 7D 04 84 C9 75 ED BE 01 00 00 00 8D A4 24 00 00 00 "
            "00 8B C6 C1 E8 02 0F BE 44 04 08 83 F8 39 7F 05 83 E8 30 EB 03 83 E8 57 8B CE 83 E1 03 BA 01 00 00 00 "
            "D3 E2 85 D0 74 09 56 E8 1A F7 FF FF 83 C4 04 83 C6 01 81 FE 80 00 00 00 7C C5 5E 81 C4 88 00 00 00 C3 "_bytes};

        // T4 port (2026-05-21): all auto-apply patches changed from
        // PatchApplySetting::Immediately -> ::Deferred. These bytes were copied
        // verbatim from iw3 and are unverified (and in some cases incorrect)
        // for T4 MP. Leaving them Immediately means that as soon as the
        // matching address is wired in Signatures.hpp, GetGamePatches() will
        // apply iw3 bytes at the t4 address - corrupting game code and
        // crashing on next call. Already happened with CL_KeyEvent on 2026-05-21
        // (iw3's 0x4E byte at offset 0 overwrote `sub esp, 0x40c`). Re-arm
        // each patch only after verifying its T4 bytes/offset and calling
        // .Apply() explicitly from a known-good code path.

        Patch<std::size(CG_RegisterItemsBytes)> CG_RegisterItems{GetGameAddresses().CG_RegisterItems(),
                                                                 CG_RegisterItemsBytes, PatchApplySetting::Deferred};
        ReturnPatch CG_DrawDisconnect{GetGameAddresses().CG_DrawDisconnect(), PatchApplySetting::Deferred};
        JumpPatch CG_AddPlayerSpriteDrawSurfs{GetGameAddresses().CG_AddPlayerSpriteDrawSurfs(),
                                              PatchApplySetting::Deferred};
        JumpPatch CL_CGameRendering{GetGameAddresses().CL_CGameRendering(), PatchApplySetting::Deferred};

        // Ensure the depth buffer is always filled
        ReturnValuePatch<1> R_DoesDrawSurfListInfoNeedFloatz{GetGameAddresses().R_DoesDrawSurfListInfoNeedFloatz(),
                                                             PatchApplySetting::Deferred};

        // Modify CL_KeyEvent to prevent the game hud from disappearing on left mouse click.
        // iw3 byte 0x4E is WRONG for T4 - T4 needs 0x2F written at offset 0x2BE.
        // T4Interface::InstallHooksAndPatches already applies the correct patch inline.
        Patch<1> CL_KeyEvent{GetGameAddresses().CL_KeyEvent(), std::array<std::uint8_t, 1>{0x4E},
                             PatchApplySetting::Deferred};

        // For rewinding (not sure if all of these are actually necessary)
        NopPatch<5> Con_TimeJumped{GetGameAddresses().Con_TimeJumpedCall(), PatchApplySetting::Deferred};

        // Prevents cg_thirdperson from being reset
        NopPatch<5> CG_MapRestart{GetGameAddresses().CG_MapRestartSetThirdpersonCall(),
                                  PatchApplySetting::Deferred};

        // to remove the blood overlay when player gets hit
        NopPatch<5> CG_DrawPlayerLowHealthOverlay{GetGameAddresses().CG_DrawPlayerLowHealthOverlay(), PatchApplySetting::Deferred};
        ReturnPatch CG_DrawFlashDamage{GetGameAddresses().CG_DrawFlashDamage(), PatchApplySetting::Deferred};
        ReturnPatch CG_DrawDamageDirectionIndicators{GetGameAddresses().CG_DrawDamageDirectionIndicators(), PatchApplySetting::Deferred};
         
        ReturnPatch IN_Frame{GetGameAddresses().IN_Frame(), PatchApplySetting::Deferred};
    };

    inline T4Patches& GetGamePatches()
    {
        static T4Patches patches;
        return patches;
    }
}  // namespace IWXMVM::T4::Patches
