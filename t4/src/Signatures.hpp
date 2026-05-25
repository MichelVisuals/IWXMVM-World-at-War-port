#pragma once
#include "StdInclude.hpp"
#include "Utilities/T4Sig.hpp"

namespace IWXMVM::T4::Signatures
{
    struct T4Addresses
    {
        // T4-local scanner (see T4Sig.hpp). core/Utilities/Signatures.hpp
        // stays 1:1 with upstream — t4 can't use it because the upstream
        // scanner crashes on CoDWaWmp.exe's sparse .data section and throws
        // on every unverified signature (and HardAddr doesn't exist there).
#define Sig IWXMVM::T4::Signatures::Signature < IWXMVM::T4::Signatures::SignatureImpl
#define HAddr IWXMVM::T4::Signatures::HardAddr
#define Lambda IWXMVM::T4::Signatures::Lambdas

        using GAType = IWXMVM::T4::Signatures::GameAddressType;

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
        //
        // Wired but UNUSED at runtime: Hooks::Camera::Install() is still
        // commented out in T4Interface.hpp so nothing reads this slot yet.
        // Wiring it on its own is a no-op runtime change — verified safe.
        // Re-enabling Hooks::Camera::Install() needs the rest of the camera
        // address set (AnglesToAxis, CG_CalcViewValues, CG_OffsetThirdPersonView,
        // FX_SetupCamera, CG_DObjGetWorldTagMatrix) AND a Camera.cpp refactor
        // to capture refdef* from EAX (because T4 MP cg_s doesn't exist as
        // a packed struct — see reference_waw_cgs_heap_allocated memory).
        HAddr<0x004E0040> R_SetViewParmsForScene;
        // 0x009E676C — refdef live address. Captured 2026-05-23 by naked
        // EAX-snapshot hook on R_SetViewParmsForScene (which Ghidra confirmed
        // takes refdef* in EAX). The value is stable across the whole demo
        // session and sits in static BSS between cg_s (0x0098FCE0) and
        // cls.realtime (0x00BD3628). NOT a member of cg_s in T4 MP — unlike
        // IW3, where refdef is reached via cg->refdef member access. The
        // separate-global location is documented in
        // reference_waw_refdef_location memory.
        //
        // Layout note: T4 refdef has a 4-byte shift vs IW3 — viewport/FOV
        // fields are at IW3 offsets but vieworg is at +0x1C (not +0x18).
        // The new 4-byte field at +0x18 holds cg_fov in degrees (verified
        // 65.0 constant during playback). refdef_s struct in Structures.hpp
        // has the corresponding `stored_fov` pad inserted.
        //
        // No consumer yet — this cycle is declarations only. Once wired
        // and the struct is shaped, the next cycle adds a Structures::GetRefdef()
        // call from a diagnostic OnFrame listener to verify vieworg matches
        // the EAX-captured one frame-by-frame, before any freecam writes.
        HAddr<0x009E676C> refdef;
        // SV_Frame — DISABLED 2026-05-24 after confirming via hit counter
        // that 0x0057F7E5 fires 0 times during T4 MP demo playback (it's
        // a vestigial dispatcher only used in SP). The IW3 byte-pattern
        // sig hunt has no other matches in T4 .text. True pause now works
        // via cls.realtime freeze in T4Interface OnFrame listener instead.
        HAddr<0> SV_Frame;
        // 0x00B71390 — verified via IW3 sig-pattern match against our memory
        // dump (BA imm32 E8 rel32 80 3D imm32 00 at VA 0x00497C40). The
        // pattern resolves once and the target is 128+ bytes of zero on
        // main menu, consistent with an unconnected clientConnection_t.
        // Field-layout note: the IW3-style demoplaying offset may not match
        // T4 (per audit table); we'll discover the right offset by playing
        // a demo and comparing memory if GetGameState doesn't transition.
        HAddr<0x00B71390> clientConnection;
        // 0x00BD3510 — clientStatic_t base. Derived 2026-05-23 session 4 from
        // Caball009: cls.realtime is at VA 0x00BD3628 and IW3 clientStatic_t
        // has realtime at offset 0x118. So base = 0x00BD3628 - 0x118 = 0x00BD3510.
        // cls.realFrametime at 0xBD362C (= base + 0x11C) matches IW3 layout.
        // Other clientStatic_t fields (quit, servername, etc.) live in the
        // first 0x118 bytes — exact layout unverified for T4, treated as
        // byte-pad prefix in Structures.hpp.
        HAddr<0x00BD3510> clientStatic;
        // 0x0098B700 — cgs_t (clientGlobalsStatic) base. Derived 2026-05-23
        // from caball-probe data: 0x0098B714 reads as serverCommandSequence
        // (matches clc.serverCommandSequence echo), and IWXMVM cgs_t has
        // serverCommandSequence at offset +0x14. So base = 0x0098B714 - 0x14
        // = 0x0098B700. processedSnapshotNum at +0x18 (= 0x0098B718) also
        // verified — matches cg_s.latestSnapshotNum each frame.
        //
        // Caball009's repo labeled 0x98B714 as `cg_t->serverCommandSequence`
        // — that's wrong (cg_t/cg_s base is at 0x0098FCE0). cgs and cg_s
        // are two separate structs.
        //
        // Unblocks ExecuteNewServerCommands path and part of the
        // Rewinding state-machine deps (RestoreOldGamestate reads/writes
        // cgs->serverCommandSequence).
        HAddr<0x0098B700> clientGlobalsStatic;
        // 0xF44780 — clientActive_s base. Verified 2026-05-23 via per-frame
        // probe of Caball009-derived offsets against live demo playback:
        //   +0x118  = cl.snap.serverTime         (leads cl.serverTime by ~50ms)
        //   +0x3AF4 = cl.serverTime              (same value as our prior 0x01F00DD0 mirror)
        //   +0x3AF8 = cl.oldServerTime           (== cl.serverTime)
        //   +0x3AFC = cl.oldFrameServerTime      (tracks correctly)
        //   +0x3B00 = cl.serverTimeDelta         (cl.serverTime − cls.realtime)
        //   +0x3B10 = gameState                  (first dword)
        //   +0x26B14 = cl.parseEntitiesNum       (increments per parse)
        //   +0x26B18 = cl.parseClientsNum        (increments per parse)
        //   +0x6B33C = cl.snapshots[32]          (per Caball009 rewind PoC)
        //
        // ALIAS WITH clientUIActives below: that slot also resolves to
        // 0xF44780 because its IW3 sig (C6 05 imm32 01) actually matched
        // the clientActive_s.loaded byte, not a clientUIActives loaded
        // byte. To be cleaned up in a follow-up cycle (find real
        // clientUIActives_t base; demote that slot until then). For now
        // both point at the same memory; only `clientActive` carries the
        // verified semantic offsets.
        //
        // NOTE: Structures::clientActive_t struct layout is IW3-derived
        // and the field offsets there DO NOT match T4's reality. Consumers
        // of Structures::GetClientActive() (GetBoneData, ResetClientData,
        // GetPlaybackDataAddresses) will read garbage until that struct is
        // reshaped to match the verified offsets above. None of those
        // consumers currently fires during normal demo playback, so wiring
        // the address alone is a no-op runtime change. Struct fix is a
        // separate cycle.
        HAddr<0xF44780> clientActive;
        // 0x98FCE0 — cg_s (clientGlobals) base. Verified 2026-05-23 via the
        // same per-frame probe used to lock down clientActive. Five
        // consecutive IW3-layout offsets all check out at live values:
        //   +0x00 = clientNum            (=3, stable local player slot)
        //   +0x18 = latestSnapshotNum    (21962→21985, monotonic snapshot counter)
        //   +0x1C = latestSnapshotTime   (tracks cl.snap.serverTime)
        //   +0x20 = snap (snapshot_s*)   (alternates 0x0098FD08 / 0x009B93D0)
        //   +0x24 = nextSnap (snapshot_s*) (swaps with snap each parse)
        //
        // Overrides earlier reference_waw_cgs_heap_allocated finding that
        // "cg_s is not a packed struct in T4 MP". The prologue IS packed at
        // 0x98FCE0; the heap allocation at 0x2E100000 we found in prior
        // sessions was just where cg_s.refdef.vieworg (deep inside the
        // struct at offset ~0xAD080) happens to fall — that field is past
        // the 16 MB code-dump boundary so it never showed up in static
        // analysis of CoDWaWmp.exe's .text.
        //
        // Like clientActive, this only matters at runtime if something
        // calls Structures::GetClientGlobals(). Currently disabled
        // Hooks::Camera::Install() is the main consumer; reads still
        // require the IW3-derived cg_s struct layout to be correct for T4
        // (TBD — verify field-by-field as features come online).
        HAddr<0x0098FCE0> clientGlobals;
        HAddr<0> mouseVars;
        HAddr<0> fs_searchpaths;
        // 0x005D67E0 — game's MainWndProc, confirmed via IWXMVM runtime log
        // ("Original WndProc captured at 5d67e0") across multiple injections
        // 2026-05-21. Consumed by T4Interface::GetWndProc() which IWXMVM's
        // window-hooking code calls. Stable across game launches.
        HAddr<0x005D67E0> MainWndProc;
        HAddr<0> CG_RegisterItems;
        // 0x00F44780 — verified via IW3 sig-pattern match (C6 05 imm32 01)
        // against our memory dump (at VA 0x0049CF2D). This sig finds a
        // `MOV [clientUIActive_s_addr], 1` byte-write — the deref target
        // is the struct start. IsConsoleOpen will read keyCatchers
        // (offset 0 in IW3 layout) — adjust if needed for T4.
        HAddr<0x00F44780> clientUIActives;
        // 0x006493A0 — SL_GetStringOfSize. Identified 2026-05-24 by bone-name
        // PUSH+CALL xref hunt: all 3 PUSH sites of "tag_origin"/"tag_weapon"/
        // "j_mainroot" funnel into a CALL of this address.
        // T4 MP SIGNATURE IS 4-ARG (matches T4SP, NOT IW3's 3-arg):
        //   uint16_t SL_GetStringOfSize(int inst, const char* string,
        //                                unsigned int user, unsigned int len);
        // First arg = scriptInstance (0 = server-side / default).
        HAddr<0x006493A0> SL_GetStringOfSize;
        // 0x00A90930 — cg_entities array base in T4 MP. Identified 2026-05-24
        // via live-memory verification: the previously-wired 0x00865828 was
        // actually the cmd_function table (contains ASCII command strings
        // like "sendranks", "setpicture"). The IMUL*0x304 scan found that
        // 0x00865828 had 78 refs (cmd_function entries are 0x304 bytes
        // including a name string) while 0x00A90930 had 58 refs — the latter
        // sits in zero-init BSS in the EXE but holds populated centity_s
        // data during demo playback (slot[0].eType=0x01 = ET_PLAYER).
        HAddr<0x00A90930> cg_entities;
        // 0x0054E790 — CG_DObjGetWorldBoneMatrix. Identified 2026-05-24 by
        // prologue-shape sig `83 EC ?? 53 55 56 57 8B F9` (SUB ESP,imm8;
        // PUSH EBX/EBP/ESI/EDI; MOV EDI, ECX — captures the non-standard
        // boneIndex-in-ECX ABI). 4 candidates fit the prologue; this one
        // is the only one with heavy XMM/FP math (171 vector-prefix insns
        // across 1537 bytes + 15 helper calls) which matches the
        // quaternion/matrix work CG_DObjGetWorldBoneMatrix is known to do.
        // Non-standard ABI: entity@<eax>, boneIndex@<ecx>, rotMatrix/dobj/
        // origin on stack — already encoded in Functions::CG_DObjGetWorldBoneMatrix.
        // No consumer until bonecam runs (GetBoneData), so wiring is a no-op
        // runtime change until SL_GetStringOfSize also lands.
        HAddr<0x0054E790> CG_DObjGetWorldBoneMatrix;
        // 0x022D9940 — clientObjMap. Identified 2026-05-24 by IW3 sig
        // `03 44 24 04 0F B7 04 45` (ADD EAX,[ESP+4]; MOVZX EAX, WORD PTR
        // [EAX*2+imm32]) — matched 4 unique caller sites in T4 .text
        // (0x0044C016, 0x0045707A, 0x005F297E, 0x005F6B40), all converging
        // on the same data VA 0x022D9940.
        //
        // T4 CAVEAT — 2-LEVEL INDEXING: The 4 caller sites all access the
        // map as `clientObjMap[clientNum * 0x700 + entityIndex]` (vs IW3's
        // flat `clientObjMap[entityId]`). The IW3-flat indexing IWXMVM core
        // uses happens to work for clientNum=0 — which is the local viewer
        // during demo playback, the only case bonecam needs. If multi-client
        // demos ever come into scope, this slot needs a wrapper accessor.
        HAddr<0x022D9940> clientObjMap;
        // 0x0228B940 — objBuf. Identified 2026-05-24 by relaxed IW3 sig
        // `0F BF F0 6B F6 ?? 81 C6` (MOVSX ESI, AX; IMUL ESI, ESI, imm8;
        // ADD ESI, imm32). 15 unique caller sites all converge on same
        // base VA 0x0228B940 with stride imm8 = 0x68 (104).
        //
        // T4 CAVEAT — STRIDE: T4 DObj_s is 0x68 (104) bytes vs IW3's 0x64
        // (100). DObj_s struct in Structures.hpp may need a 4-byte pad or
        // a verified field-by-field rebuild before GetBoneData reads
        // dobj->models/numModels/skel correctly. To verify: probe reads
        // objBuf[0..3].numModels and .models; if they look plausible
        // (small int + heap-pointer), layout is close enough.
        HAddr<0x0228B940> objBuf;
        // 0x004949D0 — confirmed CL_KeyEvent entry point. Identified by
        // (a) WAWMVM_hooks.csv labels this address as the CL_KeyEvent hook
        // target, (b) function prologue shows per-localClient input table at
        // 0x00B6E064/0x00B6E06C with stride 0x1128 — matches CL_KeyEvent's
        // role as the input dispatcher. The inline demo-disconnect patch at
        // 0x00494C8E sits at CL_KeyEvent + 0x2BE, well within this function.
        // Safe to wire now: Patches.hpp:42 (was the auto-apply trap with iw3
        // byte 0x4E) is now PatchApplySetting::Deferred. The correct T4
        // demo-disconnect patch (0x2F at offset 0x2BE) is applied inline in
        // T4Interface::InstallHooksAndPatches and is unrelated to this slot.
        HAddr<0x004949D0> CL_KeyEvent;
        // T4 MP DxGlobals struct at 0x1087DD04. Device field is at offset 4.
        // So the address-of-device-pointer is 0x1087DD08. Note: unlike IW3,
        // T4 stores the device pointer directly (single level of indirection)
        // — see GetGameDevicePtr in T4Interface for the corresponding read.
        HAddr<0x1087DD08> d3d9DevicePointer;
        // for rewinding
        // 0x005B2710 — FS_Read function ENTRY. Disassembly 2026-05-23 cycle D
        // identified this as the real entry; Caball009's 0x005B2780 was the
        // function TAIL (function tails don't work with MinHook's trampolining
        // model). The function's prologue is:
        //   53                  PUSH EBX
        //   8B 5C 24 0C         MOV EBX, [ESP+0x0C]
        //   85 DB               TEST EBX, EBX
        //   75 04               JNZ +4 (skip null-handle early-return)
        //   33 C0 5B C3         XOR EAX,EAX; POP EBX; RET
        //   69 DB 1C 01 00 00   IMUL EBX, 0x11C       (* sizeof fileHandleData_t)
        //   55                  PUSH EBP
        //   8B AB 50 9A 36 0F   MOV EBP, [EBX + 0xF369A50]  (fsh[handle])
        //
        // Important calling-convention note: EBX is loaded from [ESP+0xC],
        // which after the initial PUSH EBX is the SECOND stack arg, not the
        // third. So T4's FS_Read signature is (buffer, f, len) — f as the
        // SECOND arg — NOT quake3's standard (buffer, len, f). The t4
        // FS_Read_Hook signature must match.
        HAddr<0x005B2710> FS_Read;
        // 0x0F369A50 — fileHandleData_t table base. Per-file-handle metadata
        // array indexed by file handle int. Source: Caball009 dllmain.cpp:204
        // (`*(fileHandleData_t*)(0xF369A50 + f * sizeof(fileHandleData_t))`).
        // Used by FS_Read_Hook to extract the filename for each handle and
        // detect demo-file reads.
        HAddr<0x0F369A50> fsh;
        HAddr<0> lastValidBasepath;
        HAddr<0> s_compassActors;
        HAddr<0> conGameMsgWindow0;
        // 0x00486AA0 — CL_FirstSnapshot. Called by Rewinding::RestoreOldGamestate
        // (via Mod::GetGameInterface()->CL_FirstSnapshot()) to re-initialize
        // client state after seeking back to a stored snapshot. Source:
        // Caball009 dllmain.cpp:39 (CL_FirstSnapshotWrapper invokes 0x486AA0
        // with clientNum in EAX). Caball009 also NOPs 5 bytes at 0x486AF2
        // around the call to bypass a divide-by-zero — that patch isn't
        // wired here yet; if rewind crashes after this address is consumed,
        // that's the next thing to add.
        HAddr<0x00486AA0> CL_FirstSnapshot;
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
