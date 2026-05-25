#pragma once
#include "StdInclude.hpp"
#include "Utilities/HookManager.hpp"

// T4-local null-guarding wrappers around core's HookManager. core/Utilities/
// HookManager.cpp stays 1:1 with upstream, which throws on MH_CreateHook
// failure and unconditionally writes the JMP/CALL bytes. t4 has 28
// HardAddr<0> sites — calling those would either crash on write to address
// 0 or take down init via the throw.
//
// Use IWXMVM::T4::HookManager::CreateHook/WriteJump/WriteCall instead of the
// core/ versions from t4/.

namespace IWXMVM::T4::HookManager
{
    inline bool WriteJump(std::uintptr_t from, std::uintptr_t to)
    {
        if (from == 0 || to == 0)
        {
            LOG_WARN("T4::HookManager::WriteJump skipped: null (from={:X} to={:X})", from, to);
            return false;
        }
        return IWXMVM::HookManager::WriteJump(from, to);
    }

    inline bool WriteCall(std::uintptr_t from, std::uintptr_t to)
    {
        if (from == 0 || to == 0)
        {
            LOG_WARN("T4::HookManager::WriteCall skipped: null (from={:X} to={:X})", from, to);
            return false;
        }
        return IWXMVM::HookManager::WriteCall(from, to);
    }

    inline void CreateHook(std::uintptr_t originalPtr, std::uintptr_t detourPtr, std::uintptr_t* trampolinePtr)
    {
        if (originalPtr == 0)
        {
            LOG_WARN("T4::HookManager::CreateHook skipped: null originalPtr (detour={:X})", detourPtr);
            if (trampolinePtr) *trampolinePtr = 0;
            return;
        }
        try
        {
            IWXMVM::HookManager::CreateHook(originalPtr, detourPtr, trampolinePtr);
        }
        catch (const std::exception& e)
        {
            LOG_WARN("T4::HookManager::CreateHook caught throw for {:X}: {} (continuing)", originalPtr, e.what());
            if (trampolinePtr) *trampolinePtr = 0;
        }
    }
}  // namespace IWXMVM::T4::HookManager
