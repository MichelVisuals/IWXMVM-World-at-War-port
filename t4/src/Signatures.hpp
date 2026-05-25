#pragma once
#include "StdInclude.hpp"

namespace IWXMVM::T4::Signatures
{
    // t4-original. Not derived from core/Signatures.hpp — provides only the
    // HardAddr<> shape that t4's address table needs. Byte-pattern scanning
    // is intentionally NOT supported here: t4 uses externally-verified
    // addresses (Ghidra + t4-rtx + live RE) exclusively. If sig-pattern
    // scanning is wanted later, the right answer is to use core's
    // IWXMVM::Signatures::Signature directly, not to fork it.
    template <std::uintptr_t address>
    struct HardAddr
    {
        constexpr HardAddr() = default;
        constexpr std::uintptr_t operator()() const { return address; }
        constexpr std::uintptr_t GetAddress() const { return address; }
    };

    struct T4Addresses
    {
#define HAddr IWXMVM::T4::Signatures::HardAddr

        // ============================================================
        // T4 (WaW) port: address table — externally verified entries only.
        // ============================================================
        // Slots are HardAddr<addr> when verified (Ghidra / t4-rtx / live
        // RE) and HardAddr<0> until then. Call sites null-guard before
        // hooking/patching, so unwired slots are inert.
        // ============================================================

        // ---- VERIFIED (Ghidra + t4-rtx) ----
        // 0x0055C000 — Ghidra confirmed via "Cbuf_AddText: overflow" string. 73 callers.
        HAddr<0x0055C000> Cbuf_AddText;
        // 0x005C4040 — verified by disassembling running game memory.
        // t4-rtx labels 0x005C4170 as Dvar_FindVar but that's actually a
        // value-accessor wrapper that pushes EAX (the name) and calls 0x5C4040.
        // The real Dvar_FindVar is at 0x005C4040: __cdecl, takes name as
        // stack arg, returns dvar_s*. Spinlock prologue + hash table walk.
        HAddr<0x005C4040> Dvar_FindMalleableVar;
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
        // 0x004E0040 is R_SetViewParmsForScene in T4 MP (Ghidra-confirmed
        // 2026-05-21). Function takes refdef* in EAX (non-standard convention)
        // and r_zfar as a stack arg. Reads [EAX+0x1C..0x24] = vieworg,
        // [EAX+0x2C..0x4C] = viewaxis 3x3, [EAX+0x10/0x14] = tanHalfFovX/Y,
        // [EAX+0x50..0x58] = additional vec3. Writes derived view params to
        // clientActive_s rendering mirror at +0x400..+0x490. Called from
        // 0x00478570 (per-frame), 0x00479A10 (HUD), 0x006D1090 (renderer).
        HAddr<0x004E0040> R_SetViewParmsForScene;
        // 0x009E676C — refdef live address. Captured 2026-05-23 by naked
        // EAX-snapshot hook on R_SetViewParmsForScene. NOT a member of cg_s
        // in T4 MP — see reference_waw_refdef_location memory.
        HAddr<0x009E676C> refdef;
        // SV_Frame — DISABLED 2026-05-24 (0 hits during MP demo playback).
        HAddr<0> SV_Frame;
        // 0x00B71390 — clientConnection. Verified via IW3 sig-pattern match.
        HAddr<0x00B71390> clientConnection;
        // 0x00BD3510 — clientStatic_t base. Derived 2026-05-23 from Caball009.
        HAddr<0x00BD3510> clientStatic;
        // 0x0098B700 — cgs_t base. Derived 2026-05-23 from caball-probe data.
        HAddr<0x0098B700> clientGlobalsStatic;
        // 0xF44780 — clientActive_s base. Verified 2026-05-23 via per-frame probe.
        HAddr<0xF44780> clientActive;
        // 0x98FCE0 — cg_s base. Verified 2026-05-23 via per-frame probe (5 fields).
        HAddr<0x0098FCE0> clientGlobals;
        HAddr<0> mouseVars;
        HAddr<0> fs_searchpaths;
        // 0x005D67E0 — MainWndProc. Confirmed via runtime log 2026-05-21.
        HAddr<0x005D67E0> MainWndProc;
        HAddr<0> CG_RegisterItems;
        // 0x00F44780 — clientUIActives. IW3 sig-pattern match.
        HAddr<0x00F44780> clientUIActives;
        // 0x006493A0 — SL_GetStringOfSize (4-arg T4 signature, not IW3's 3-arg).
        HAddr<0x006493A0> SL_GetStringOfSize;
        // 0x00A90930 — cg_entities array base. Verified 2026-05-24 via live-memory.
        HAddr<0x00A90930> cg_entities;
        // CG_DObjGetWorldBoneMatrix — UNWIRED (see project_iwxmvm_bonecam_blocked).
        HAddr<0> CG_DObjGetWorldBoneMatrix;
        // 0x022D9940 — clientObjMap. Identified 2026-05-24 via IW3 sig-pattern.
        HAddr<0x022D9940> clientObjMap;
        // 0x0228B940 — objBuf. Identified 2026-05-24 via relaxed IW3 sig.
        HAddr<0x0228B940> objBuf;
        // 0x004949D0 — CL_KeyEvent entry. Confirmed via WAWMVM_hooks.csv label.
        HAddr<0x004949D0> CL_KeyEvent;
        // 0x1087DD08 — d3d9 device pointer (DxGlobals + 4 offset, single-deref).
        HAddr<0x1087DD08> d3d9DevicePointer;
        // 0x005B2710 — FS_Read function ENTRY (Caball009's 0x005B2780 was the tail).
        HAddr<0x005B2710> FS_Read;
        // 0x0F369A50 — fileHandleData_t table base (from Caball009 dllmain.cpp:204).
        HAddr<0x0F369A50> fsh;
        HAddr<0> lastValidBasepath;
        HAddr<0> s_compassActors;
        HAddr<0> conGameMsgWindow0;
        // 0x00486AA0 — CL_FirstSnapshot (Caball009 dllmain.cpp:39).
        HAddr<0x00486AA0> CL_FirstSnapshot;
        HAddr<0> Con_TimeJumpedCall;
        HAddr<0> CG_MapRestartSetThirdpersonCall;
        HAddr<0> R_DoesDrawSurfListInfoNeedFloatz;
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

#undef HAddr
    };
}  // namespace IWXMVM::T4::Signatures
