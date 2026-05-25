#include "StdInclude.hpp"
#include "Playback.hpp"

#include "Components/Playback.hpp"
#include "Components/Rewinding.hpp"
#include "Utilities/HookManager.hpp"
#include "Events.hpp"
#include "../Addresses.hpp"
#include "../Structures.hpp"
#include "../Functions.hpp"
#include "../Patches.hpp"

namespace IWXMVM::T4::Hooks::Playback
{
    // MinHook needs a trampoline pointer slot even though our IW3-style
    // replace-function hook doesn't read it (we ret past the original
    // entirely — see SV_Frame_Hook below).
    typedef void (*SV_Frame_t)();
    SV_Frame_t SV_Frame_Trampoline = nullptr;

    void SV_Frame_Internal(std::int32_t& msec)
    {
        msec = Components::Playback::CalculatePlaybackDelta(msec);
    }

    // IW3-style "replace function" hook targeting the per-frame thin
    // dispatcher whose body is:
    //     PUSH ESI; CALL inner; ADD ESP, 4; MOV EAX, ESI; POP ECX; RET
    //
    // The hook discards the return-into-dispatcher address with `pop ecx`,
    // runs msec through CalculatePlaybackDelta (0 if paused, msec otherwise),
    // then `ret` returns PAST the dispatcher to the caller's caller — the
    // original inner CALL is skipped.
    //
    // NOTE: As of 2026-05-24 the only T4 .text address matching this exact
    // shape (0x0057F7E5) is never called during MP demo playback (verified
    // via hit counter — 0/sec). T4 MP pause is instead handled by the
    // cls.realtime-freeze listener in T4Interface::SetupEventListeners.
    // The SV_Frame hook is preserved as a canonical template in case a
    // future T4 MP address turns out to be load-bearing per-frame.
    void __declspec(naked) SV_Frame_Hook()
    {
        static std::int32_t msec;

        __asm
        {
            pop ecx
            pushad
            mov msec, esi
        }

        SV_Frame_Internal(msec);

        __asm
        {
            popad
            mov eax, msec
            ret
        }
    }

    // T4 FS_Read signature is (void* buffer, int f, int len) — `f` is the
    // SECOND arg, not the third (quake3 standard). Verified by disassembling
    // FS_Read's prologue: `MOV EBX, [ESP+0x0C]; IMUL EBX, 0x11C` = handle
    // is at [ESP+0xC] which is the second cdecl arg after PUSH EBX.
    //
    // iw3 binding uses the quake3 (buffer, len, f) order. Do NOT share this
    // signature between t4 and iw3.
    typedef int (*FS_Read_t)(void* buffer, int f, int len);
    FS_Read_t FS_Read_Trampoline;
    int FS_Read_Hook(void* buffer, int f, int len)
    {
        using namespace Structures;
        fileHandleData_t fh =
            *reinterpret_cast<fileHandleData_t*>(GetGameAddresses().fsh() + f * sizeof(fileHandleData_t));

        std::string_view sv = fh.name;
        bool isDemoFile = sv.ends_with(Mod::GetGameInterface()->GetDemoExtension());

        if (!isDemoFile)
            return FS_Read_Trampoline(buffer, f, len);

        auto result = Components::Rewinding::FS_Read(buffer, len);
        if (result == -1)
            return FS_Read_Trampoline(buffer, f, len);

        return result;
    }

    void Install()
    {
        const auto sv_frame_va = GetGameAddresses().SV_Frame();
        if (sv_frame_va)
        {
            HookManager::CreateHook(sv_frame_va, (std::uintptr_t)SV_Frame_Hook,
                                    (uintptr_t*)&SV_Frame_Trampoline);
            LOG_INFO("Hooks::Playback: SV_Frame hook installed at 0x{:08X}", sv_frame_va);
        }

        // core/-cleanup-1:1: Features_Rewinding bit no longer in upstream
        // Features.hpp. FS_Read hook is permanently disabled here — Rewinding
        // state machine wouldn't function correctly anyway without the
        // gating logic that lived in core/Playback.cpp / core/Rewinding.cpp.
        // To re-enable rewind, the upstream Features_Rewinding bit would
        // need to be re-introduced via a different mechanism.
    }
}  // namespace IWXMVM::T4::Hooks::Playback
