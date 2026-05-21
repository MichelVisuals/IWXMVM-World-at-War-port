#include "StdInclude.hpp"
#include "MenuBar.hpp"

#include "Version.hpp"
#include "Mod.hpp"
#include "Utilities/PathUtils.hpp"
#include "UI/UIManager.hpp"

namespace IWXMVM::UI
{
    void MenuBar::Initialize()
    {
        SetPosition(0, 0);
        SetSize(ImGui::GetIO().DisplaySize.x, ImGui::GetFrameHeight());
    }

    void MenuBar::Render()
    {
        if (ImGui::BeginMainMenuBar())
        {
            if (ImGui::BeginMenu("File"))
            {
                if (ImGui::MenuItem("Preferences"))
                {
                    UIManager::Get().GetUIComponent<Preferences>(Component::Preferences)->ToggleVisibility();
                }
                if (ImGui::MenuItem("Controls"))
                {
                    UIManager::Get().GetUIComponent<ControlsMenu>(Component::ControlsMenu)->ToggleVisibility();
                }
                if (ImGui::MenuItem("Exit"))
                {
                    Mod::RequestEject();
                }
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Debug"))
            {
                if (ImGui::MenuItem("Toggle IWXMVM UI", "F1"))
                {
                    UIManager::Get().ToggleOverlay();
                }

                if (ImGui::MenuItem("Toggle ImGui Demo", "F3"))
                {
                    UIManager::Get().ToggleImGuiDemo();
                }

                if (ImGui::MenuItem("Toggle Debug Panel", "F4"))
                {
                    UIManager::Get().ToggleDebugPanel();
                }

                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Tools"))
            {
                ImGui::BeginDisabled(
                    !(Mod::GetGameInterface()->GetSupportedFeatures() & Types::Features_ChangeAnimations)
                );
                
                if (ImGui::MenuItem("Player Death Animations##0"))
                {
                    UIManager::Get().GetUIComponent<PlayerAnimation>(Component::PlayerAnimation)->ToggleVisibility();
                }

                ImGui::EndDisabled();

                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("About"))
            {
                if (ImGui::MenuItem("Credits", ""))
                {
                    UIManager::Get().GetUIComponent<Credits>(Component::Credits)->ToggleVisibility();
                }
                ImGui::EndMenu();
            }

            auto windowSize = ImGui::GetIO().DisplaySize;
            std::string iwxmvmText = std::format(
                "IWXMVM {0} | {1}", IWXMVM_VERSION, magic_enum::enum_name(Mod::GetGameInterface()->GetGame())
            );

            // T4 port: clickable input-ownership toggle, sits just left of
            // the version label. Hotkey is INSERT (handled in ImGuiWndProc).
            const bool captured = UIManager::Get().IsInputCaptured();
            const char* label = captured ? "Input: IWXMVM" : "Input: Game";
            // Color the button differently per state so the current mode is
            // unmissable: greenish when game owns input, orangeish when
            // IWXMVM does.
            const ImVec4 normal  = ImVec4(0.20f, 0.55f, 0.25f, 1.0f);
            const ImVec4 normalH = ImVec4(0.30f, 0.70f, 0.35f, 1.0f);
            const ImVec4 normalA = ImVec4(0.15f, 0.45f, 0.20f, 1.0f);
            const ImVec4 active  = ImVec4(0.80f, 0.50f, 0.10f, 1.0f);
            const ImVec4 activeH = ImVec4(0.95f, 0.65f, 0.20f, 1.0f);
            const ImVec4 activeA = ImVec4(0.70f, 0.40f, 0.05f, 1.0f);
            ImGui::PushStyleColor(ImGuiCol_Button,        captured ? active  : normal);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, captured ? activeH : normalH);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  captured ? activeA : normalA);

            const auto buttonText = std::string(label) + " (Insert)";
            const float buttonW = ImGui::CalcTextSize(buttonText.c_str()).x + ImGui::GetStyle().FramePadding.x * 2.0f;
            const float versionW = ImGui::CalcTextSize(iwxmvmText.c_str()).x + ImGui::CalcTextSize(" ").x;
            ImGui::SetCursorPosX(windowSize.x - versionW - buttonW - ImGui::GetStyle().ItemSpacing.x * 2.0f);
            if (ImGui::Button(buttonText.c_str()))
            {
                UIManager::Get().ToggleInputCaptured();
            }
            ImGui::PopStyleColor(3);

            ImGui::SetCursorPosX(windowSize.x - versionW);
            ImGui::Text(iwxmvmText.c_str());

            ImGui::EndMainMenuBar();
        }
    }

    void MenuBar::Release()
    {
    }
}  // namespace IWXMVM::UI