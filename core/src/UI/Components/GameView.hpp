#pragma once
#include "UI/UIComponent.hpp"
#include <atomic>

namespace IWXMVM::UI
{
    // Set to true by GameView::LockMouse exactly across its SetCursorPos call.
    // Consumed by D3D9.cpp's SetCursorPos hook to differentiate the legitimate
    // LockMouse caller (which MUST be allowed through for ImGui's MouseDelta
    // compensation to work) from WaW's IN_Frame re-centering (which must be
    // suppressed during demos so the cursor stays on the IWXMVM overlay).
    extern std::atomic<bool> g_setCursorPosFromLockMouse;

    class GameView : public UIComponent
    {
       public:
        void Render() final;
        void Release() final;

        ImVec2 GetViewportPosition()
        {
            return viewportPosition;
        }

        ImVec2 GetViewportSize()
        {
            return viewportSize;
        }

       private:
        void Initialize() final;
        void DrawTopBar();

        void LockMouse();

        void DrawGizmoControls();
        void DrawKeybinds();

        IDirect3DTexture9* texture = NULL;
        ImVec2 textureSize = ImVec2(0, 0);

        ImVec2 viewportPosition = ImVec2(0, 0);
        ImVec2 viewportSize = ImVec2(0, 0);
    };
}  // namespace IWXMVM::UI