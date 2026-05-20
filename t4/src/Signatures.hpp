#pragma once
#include "StdInclude.hpp"
#include "Utilities/Signatures.hpp"

namespace IWXMVM::T4::Signatures
{
    struct T4Addresses
    {
#define Sig IWXMVM::Signatures::Signature < IWXMVM::Signatures::SignatureImpl
#define HAddr IWXMVM::Signatures::HardAddr
#define Lambda IWXMVM::Signatures::Lambdas

        using GAType = IWXMVM::Signatures::GameAddressType;

        // ============================================================
        // T4 (WaW) port: signature table
        // ============================================================
        // Strategy: short IW3 byte signatures (5-12 bytes) frequently
        // produce false positives in T4's 4 MB .text section, which then
        // causes hooks to install at garbage addresses and crash the
        // game with an access violation downstream. So until each entry
        // is properly verified for T4, we point it at HardAddr<0> — the
        // resilient framework (Patch / HookManager / D3D9::Initialize)
        // skips null addresses cleanly and logs a warning.
        //
        // Verified T4 entries use HardAddr<address>. As we identify more,
        // they get moved into the verified section.
        // ============================================================

        // ---- VERIFIED (Ghidra + t4-rtx) ----
        // 0x0055C000 — Ghidra confirmed via "Cbuf_AddText: overflow" string. 73 callers.
        HAddr<0x0055C000> Cbuf_AddText;
        // 0x005C4170 — t4-rtx confirmed, Ghidra-verified function entry (41 bytes).
        HAddr<0x005C4170> Dvar_FindMalleableVar;
        // 0x006BBA90 — t4-rtx confirmed, Ghidra-verified function entry (15 bytes).
        HAddr<0x006BBA90> Material_RegisterHandle;

        // ---- TO BE WIRED UP (zero until reversed) ----
        HAddr<0> fopen;
        HAddr<0> AnglesToAxis;
        HAddr<0> CG_AddPlayerSpriteDrawSurfs;
        HAddr<0> CL_CGameRendering;
        HAddr<0> CG_CalcViewValues;
        HAddr<0> CG_DObjGetWorldTagMatrix;
        HAddr<0> CG_DrawDisconnect;
        HAddr<0> CG_OffsetThirdPersonView;
        HAddr<0> FX_SetupCamera;
        HAddr<0> R_SetViewParmsForScene;
        HAddr<0> SV_Frame;
        HAddr<0> clientConnection;
        HAddr<0> clientStatic;
        HAddr<0> clientActive;
        HAddr<0> clientGlobalsStatic;
        HAddr<0> clientGlobals;
        HAddr<0> mouseVars;
        HAddr<0> fs_searchpaths;
        HAddr<0> MainWndProc;
        HAddr<0> CG_RegisterItems;
        HAddr<0> clientUIActives;
        HAddr<0> SL_GetStringOfSize;
        HAddr<0> cg_entities;
        HAddr<0> CG_DObjGetWorldBoneMatrix;
        HAddr<0> clientObjMap;
        HAddr<0> objBuf;
        HAddr<0> CL_KeyEvent;
        HAddr<0> d3d9DevicePointer;
        // for rewinding
        HAddr<0> FS_Read;
        HAddr<0> fsh;
        HAddr<0> lastValidBasepath;
        HAddr<0> s_compassActors;
        HAddr<0> conGameMsgWindow0;
        HAddr<0> CL_FirstSnapshot;
        HAddr<0> Con_TimeJumpedCall;
        HAddr<0> CG_MapRestartSetThirdpersonCall;
        // for depth patch
        HAddr<0> R_DoesDrawSurfListInfoNeedFloatz;
        // for changing (death) animations
        HAddr<0> CG_ProcessEntity;
        HAddr<0> R_AddCmdDrawTextWithEffects;
        HAddr<0> IN_Frame;
        HAddr<0> R_SetupMaterial;
        HAddr<0> rgp;
        HAddr<0> CG_DrawPlayerLowHealthOverlay;
        HAddr<0> CG_DrawFlashDamage;
        HAddr<0> CG_DrawDamageDirectionIndicators;
        HAddr<0> Dvar_SetStringByName;
        HAddr<0> CG_ExecuteNewServerCommands;

#undef Sig
#undef HAddr
#undef Lambda
    };
}  // namespace IWXMVM::T4::Signatures
