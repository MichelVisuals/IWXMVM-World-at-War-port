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
    volatile std::uint32_t g_sv_frame_hits = 0;
    volatile std::uint32_t g_sv_frame_last_msec = 0;

    void SV_Frame_Internal(std::int32_t& msec)
    {
        g_sv_frame_hits++;
        g_sv_frame_last_msec = (std::uint32_t)msec;
        msec = Components::Playback::CalculatePlaybackDelta(msec);
    }

    // T4 port: simplified single-branch SV_Frame hook. The verified T4 function
    // at 0x0057F7E5 takes msec in ESI and returns it via EAX (PUSH ESI; CALL
    // inner; ADD ESP, 4; MOV EAX, ESI; POP ECX; RET). We replace the whole
    // body: pull msec from ESI, run it through CalculatePlaybackDelta (returns
    // 0 when paused, msec unchanged otherwise), return via EAX. The original
    // function's inner-CALL side effects are skipped — matches IW3 behavior.
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

    typedef int (*FS_Read_t)(void* buffer, int len, int f);
    FS_Read_t FS_Read_Trampoline;
    int FS_Read_Hook(void* buffer, int len, int f)
    {
        using namespace Structures;
        fileHandleData_t fh =
            *reinterpret_cast<fileHandleData_t*>(GetGameAddresses().fsh() + f * sizeof(fileHandleData_t));

        std::string_view sv = fh.name;
        bool isDemoFile = sv.ends_with(Mod::GetGameInterface()->GetDemoExtension());

        if (!isDemoFile)
        {
            return FS_Read_Trampoline(buffer, len, f);
        }

        auto result = Components::Rewinding::FS_Read(buffer, len);
        if (result == -1)
        {
            return FS_Read_Trampoline(buffer, len, f);
        }

        return result;
    }

    void Install()
    {
        const auto sv_frame_va = GetGameAddresses().SV_Frame();
        if (sv_frame_va)
            HookManager::CreateHook(sv_frame_va, (std::uintptr_t)SV_Frame_Hook, nullptr);

        // T4 port: FS_Read hook needs `fsh` global (clientStatic file-handle
        // array) which is still HardAddr<0>. Without it, rewinding can't
        // intercept demo-file reads. Skip until fsh is wired.
        const auto fs_read_va = GetGameAddresses().FS_Read();
        const auto fsh_va = GetGameAddresses().fsh();
        if (fs_read_va && fsh_va)
            HookManager::CreateHook(fs_read_va, (std::uintptr_t)FS_Read_Hook, (uintptr_t*)&FS_Read_Trampoline);
    }

    // ====================================================================
    // CG_CalcViewValues candidate hunt: install per-frame call counters at
    // each of the 4 FP-heavy unidentified WaWMVM hook addresses, count
    // invocations. The one called ~60Hz during demo playback is the
    // per-frame view-calc function we need to hook for free camera.
    // Naked hooks preserve ALL registers/flags/stack and just increment a
    // counter, then tail-jump to MinHook's trampoline (orig function +
    // continuation).
    // ====================================================================
    volatile std::uint32_t g_candA_hits = 0;  // 0x00430670 - per-frame helper (confirmed not CG_CalcViewValues itself)
    volatile std::uint32_t g_candB_hits = 0;  // 0x004345D0
    volatile std::uint32_t g_candC_hits = 0;  // 0x0058AB44
    volatile std::uint32_t g_candD_hits = 0;  // 0x0070CA98 - per-frame, 1.5x rate
    volatile std::uint32_t g_candE_hits = 0;  // 0x0043E3B0 - SUB ESP 0x1C; CMP global
    volatile std::uint32_t g_candF_hits = 0;  // 0x00446440 - function call setup
    volatile std::uint32_t g_candG_hits = 0;  // 0x006B6CE0 - SUB ESP 0xC

    std::uintptr_t g_candA_orig = 0;
    std::uintptr_t g_candB_orig = 0;
    std::uintptr_t g_candC_orig = 0;
    std::uintptr_t g_candD_orig = 0;
    std::uintptr_t g_candE_orig = 0;
    std::uintptr_t g_candF_orig = 0;
    std::uintptr_t g_candG_orig = 0;

    __declspec(naked) void candA_hook()
    {
        __asm
        {
            pushfd
            push eax
            mov eax, [g_candA_hits]
            inc eax
            mov [g_candA_hits], eax
            pop eax
            popfd
            jmp [g_candA_orig]
        }
    }
    __declspec(naked) void candB_hook()
    {
        __asm
        {
            pushfd
            push eax
            mov eax, [g_candB_hits]
            inc eax
            mov [g_candB_hits], eax
            pop eax
            popfd
            jmp [g_candB_orig]
        }
    }
    __declspec(naked) void candC_hook()
    {
        __asm
        {
            pushfd
            push eax
            mov eax, [g_candC_hits]
            inc eax
            mov [g_candC_hits], eax
            pop eax
            popfd
            jmp [g_candC_orig]
        }
    }
    __declspec(naked) void candD_hook()
    {
        __asm
        {
            pushfd
            push eax
            mov eax, [g_candD_hits]
            inc eax
            mov [g_candD_hits], eax
            pop eax
            popfd
            jmp [g_candD_orig]
        }
    }
    __declspec(naked) void candE_hook()
    {
        __asm
        {
            pushfd; push eax
            mov eax, [g_candE_hits]; inc eax; mov [g_candE_hits], eax
            pop eax; popfd
            jmp [g_candE_orig]
        }
    }
    __declspec(naked) void candF_hook()
    {
        __asm
        {
            pushfd; push eax
            mov eax, [g_candF_hits]; inc eax; mov [g_candF_hits], eax
            pop eax; popfd
            jmp [g_candF_orig]
        }
    }
    __declspec(naked) void candG_hook()
    {
        __asm
        {
            pushfd; push eax
            mov eax, [g_candG_hits]; inc eax; mov [g_candG_hits], eax
            pop eax; popfd
            jmp [g_candG_orig]
        }
    }

    void InstallCandidateHunt()
    {
        // Candidate hunt DISABLED — adding E/F/G hooks (0x0043E3B0, 0x00446440,
        // 0x006B6CE0) caused an unhandled exception when loading a demo. One of
        // those three is not a valid function entry. We previously had a stable
        // 4-hook run; reverting to no hooks for safety. The data we got was
        // sufficient: A (0x00430670) is a per-frame helper called 120Hz during
        // demo, D (0x0070CA98) similar at 180Hz, B/C inactive.
        // Future: re-enable each problematic candidate ONE AT A TIME to bisect.
    }
}  // namespace IWXMVM::T4::Hooks::Playback
