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
}  // namespace IWXMVM::T4::Hooks::Playback
