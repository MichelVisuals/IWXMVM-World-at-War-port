#include "StdInclude.hpp"
#include "PathUtils.hpp"

#include "MinHook.h"

#include "UI/UIManager.hpp"

namespace IWXMVM::HookManager
{
    constexpr std::uint8_t JUMP_OPCODE = 0xE9;
    constexpr std::uint8_t CALL_OPCODE = 0xE8;
    constexpr std::size_t JUMP_LENGTH = 5;

    bool WriteJumpInternal(std::uintptr_t from, std::uintptr_t to, std::size_t size = JUMP_LENGTH,
                           std::uint8_t jumpOpcode = JUMP_OPCODE)
    {
        DWORD curProtection;

        if (!VirtualProtect(reinterpret_cast<LPVOID>(from), size, PAGE_EXECUTE_READWRITE, &curProtection))
        {
            throw std::runtime_error("VirtualProtect failure. Address: " + std::to_string(from) +
                                     ", size: " + std::to_string(size));
            return false;
        }

        std::uintptr_t relativeAddress = (to - from) - 5;
        *reinterpret_cast<uint8_t*>(from) = jumpOpcode;
        *reinterpret_cast<std::uintptr_t*>(from + 1) = relativeAddress;

        if (*reinterpret_cast<uint8_t*>(from) != jumpOpcode ||
            *reinterpret_cast<std::uintptr_t*>(from + 1) != relativeAddress)
        {
            throw std::runtime_error("Memory access failure. Addresses: " + std::to_string(from) + ", " +
                                     std::to_string(from + 1));
            return false;
        }

        if (!VirtualProtect(reinterpret_cast<LPVOID>(from), size, curProtection, &curProtection))
        {
            throw std::runtime_error("VirtualProtect failure. Address: " + std::to_string(from) +
                                     ", size: " + std::to_string(size));
            return false;
        }

        return true;
    }

    bool WriteJump(std::uintptr_t from, std::uintptr_t to)
    {
        if (from == 0 || to == 0)
        {
            LOG_WARN("WriteJump skipped: null address (from={:X} to={:X})", from, to);
            return false;
        }
        return WriteJumpInternal(from, to, JUMP_LENGTH, JUMP_OPCODE);
    }

    bool WriteCall(std::uintptr_t from, std::uintptr_t to)
    {
        if (from == 0 || to == 0)
        {
            LOG_WARN("WriteCall skipped: null address (from={:X} to={:X})", from, to);
            return false;
        }
        return WriteJumpInternal(from, to, JUMP_LENGTH, CALL_OPCODE);
    }

    void CreateHook(std::uintptr_t originalPtr, std::uintptr_t detourPtr, std::uintptr_t* trampolinePtr)
    {
        // Resilient: null originalPtr means the underlying sig didn't resolve. Skip
        // silently — the trampoline stays null so any code that uses it will get
        // a fast-fail rather than a corrupted trampoline.
        if (originalPtr == 0)
        {
            LOG_WARN("CreateHook skipped: null originalPtr (detour={:X})", detourPtr);
            if (trampolinePtr) *trampolinePtr = 0;
            return;
        }

        auto result = MH_CreateHook((void*)originalPtr, (void*)detourPtr, (void**)trampolinePtr);
        if (result != MH_OK)
        {
            LOG_WARN("MH_CreateHook failed for {:X}: {} (continuing)", originalPtr, magic_enum::enum_name(result));
            if (trampolinePtr) *trampolinePtr = 0;
            return;
        }

        result = MH_EnableHook((void*)originalPtr);
        if (result != MH_OK)
        {
            LOG_WARN("MH_EnableHook failed for {:X}: {} (continuing)", originalPtr, magic_enum::enum_name(result));
        }
    }

    void Unhook()
    {
        if (MH_DisableHook(MH_ALL_HOOKS) != MH_OK)
            throw std::runtime_error("Failed to disable hooks");
        if (MH_Uninitialize() != MH_OK)
            throw std::runtime_error("Failed to uninitialize MinHook");
    }
}  // namespace IWXMVM::HookManager