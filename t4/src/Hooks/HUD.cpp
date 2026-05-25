#include "StdInclude.hpp"
#include "HUD.hpp"

#include "Utilities/HookManager.hpp"
#include "../Addresses.hpp"
#include "../Structures.hpp"

namespace IWXMVM::T4::Hooks::HUD
{
    bool showScore = true;
    bool showOtherText = true;

    typedef void (*R_AddCmdDrawTextWithEffects_t)(const char*, int, void*, float, float, float, float, float, const float*,
                                                  int, const float*, void*, void*, int, int, int, int);
    R_AddCmdDrawTextWithEffects_t R_AddCmdDrawTextWithEffects_Trampoline;
    void R_AddCmdDrawTextWithEffects_Hook(const char* text, int maxChars, void* font, float x, float y, float xScale,
                                          float yScale, float rotation, const float* color, int style,
                                          const float* glowColor, void* fxMaterial, void* fxMaterialGlow,
                                          int fxBirthTime, int fxLetterTime, int fxDecayStartTime, int fxDecayDuration)
    {
        if (text[0] == '+')
        {
            if (!showScore)
                return;
        }
        else
        {
            if (!showOtherText)
                return;
        }

        R_AddCmdDrawTextWithEffects_Trampoline(text, maxChars, font, x, y, xScale, yScale, rotation, color, style,
                                               glowColor, fxMaterial, fxMaterialGlow, fxBirthTime, fxLetterTime,
                                               fxDecayStartTime, fxDecayDuration);
    }

    void Install()
    {
        if (const auto a = GetGameAddresses().R_AddCmdDrawTextWithEffects(); a)
        {
            try
            {
                IWXMVM::HookManager::CreateHook(a, (std::uintptr_t)R_AddCmdDrawTextWithEffects_Hook,
                                                (uintptr_t*)&R_AddCmdDrawTextWithEffects_Trampoline);
            }
            catch (const std::exception& e)
            {
                LOG_WARN("Hooks::HUD: R_AddCmdDrawTextWithEffects CreateHook failed: {}", e.what());
            }
        }
    }
}  // namespace IWXMVM::T4::Hooks::Playback
