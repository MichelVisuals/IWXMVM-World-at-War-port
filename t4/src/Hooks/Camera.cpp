#include "StdInclude.hpp"
#include "Camera.hpp"

#include "Utilities/T4HookManager.hpp"
#include "Utilities/MathUtils.hpp"
#include "../Structures.hpp"
#include "../Functions.hpp"
#include "../Addresses.hpp"
#include "Mod.hpp"

namespace IWXMVM::T4::Hooks::Camera
{

    // CoD/Quake-convention AnglesToAxis: writes the 3x3 viewaxis matrix from
    // Euler angles (pitch, yaw, roll) in degrees. Matches t6's / iw3's /
    // quake3's `AngleVectors` then negates `right` to form viewaxis[1].
    static void WriteViewaxisFromAngles(float (&viewaxis)[3][3], const glm::vec3& anglesDeg)
    {
        const float p = glm::radians(anglesDeg[0]);
        const float y = glm::radians(anglesDeg[1]);
        const float r = glm::radians(anglesDeg[2]);
        const float cp = std::cos(p), sp = std::sin(p);
        const float cy = std::cos(y), sy = std::sin(y);
        const float cr = std::cos(r), sr = std::sin(r);

        // forward
        viewaxis[0][0] = cp * cy;
        viewaxis[0][1] = cp * sy;
        viewaxis[0][2] = -sp;
        // -right (viewaxis[1] = vec3_origin - right per quake3 AnglesToAxis)
        viewaxis[1][0] = sr * sp * cy - cr * sy;
        viewaxis[1][1] = sr * sp * sy + cr * cy;
        viewaxis[1][2] = sr * cp;
        // up
        viewaxis[2][0] = cr * sp * cy + sr * sy;
        viewaxis[2][1] = cr * sp * sy - sr * cy;
        viewaxis[2][2] = cr * cp;
    }

    void R_SetViewParmsForScene()
    {
        // T4 port (2026-05-23): refdef is a SEPARATE BSS global at 0x009E676C,
        // NOT a member of cg_s as in IW3. See reference_waw_refdef_location memory.
        // Structures::GetRefdef() returns the wired pointer with the corrected
        // T4 layout (vieworg at +0x1C, viewaxis at +0x2C, stored_fov at +0x18).
        auto& refdef = *Structures::GetRefdef();

        auto& camera = Components::CameraManager::Get().GetActiveCamera();

        if (!camera->IsModControlledCameraMode())
        {
            // Sync game state into IWXMVM Camera so when user toggles into
            // freecam, position/rotation/FOV all start from the player's
            // current view.
            camera->GetPosition() = *reinterpret_cast<glm::vec3*>(refdef.vieworg);
            // Recover pitch/yaw from viewaxis[0] (= forward direction).
            // Roll is left at 0 since gameplay viewaxis is rarely rolled.
            const glm::vec3 forward(refdef.viewaxis[0][0], refdef.viewaxis[0][1], refdef.viewaxis[0][2]);
            camera->GetRotation() = MathUtils::AnglesFromForwardVector(forward);
            camera->GetFov() = glm::degrees(std::atan(refdef.tanHalfFovX) * 2.0f);
            return;
        }

        refdef.vieworg[0] = camera->GetPosition()[0];
        refdef.vieworg[1] = camera->GetPosition()[1];
        refdef.vieworg[2] = camera->GetPosition()[2];

        refdef.tanHalfFovX = std::tan(glm::radians(camera->GetFov()) * 0.5f);
        refdef.tanHalfFovY = refdef.tanHalfFovX * ((float)refdef.height / (float)refdef.width);

        // 2026-05-23: write viewaxis directly into refdef. With AnglesToAxis
        // not yet hooked, this is the only path to control where the freecam
        // looks. The engine reads refdef.viewaxis later in R_SetViewParmsForScene
        // for the derived view params at clientActive +0x400..+0x490, so our
        // override at hook entry takes effect for this frame's render.
        WriteViewaxisFromAngles(refdef.viewaxis, camera->GetRotation());
    }

    uintptr_t R_SetViewParmsForScene_Trampoline;
    void __declspec(naked) R_SetViewParmsForScene_Hook()
    {
        __asm pushad

        R_SetViewParmsForScene();

        __asm popad
        __asm jmp R_SetViewParmsForScene_Trampoline
    }

    void AnglesToAxis(float* angles)
    {
        auto& camera = Components::CameraManager::Get().GetActiveCamera();

        if (!camera->IsModControlledCameraMode())
        {
            camera->GetRotation() = *reinterpret_cast<glm::vec3*>(angles);
            return;
        }

        angles[0] = camera->GetRotation()[0];
        angles[1] = camera->GetRotation()[1];
        angles[2] = camera->GetRotation()[2];
    }

    uintptr_t AnglesToAxis_Address;
    void __declspec(naked) AnglesToAxis_Hook()
    {
        static float* angles;

        __asm pushad 
        __asm mov angles, esi

        AnglesToAxis(angles);

        __asm popad 
        __asm jmp AnglesToAxis_Address
    }

    void FX_SetupCamera()
    {
        auto& camera = Components::CameraManager::Get().GetActiveCamera();

        if (!camera->IsModControlledCameraMode())
            return;

        // T4 port: same refdef relocation fix as R_SetViewParmsForScene above.
        auto& refdef = *Structures::GetRefdef();
        refdef.vieworg[0] = camera->GetPosition()[0];
        refdef.vieworg[1] = camera->GetPosition()[1];
        refdef.vieworg[2] = camera->GetPosition()[2];
    }

    uintptr_t FX_SetupCamera_Trampoline;
    void __declspec(naked) FX_SetupCamera_Hook()
    {
        __asm pushad

        FX_SetupCamera();

        __asm popad
        __asm jmp FX_SetupCamera_Trampoline
    }

    uint32_t CG_DObjGetWorldTagMatrix_Trampoline;
    void __declspec(naked) CG_DObjGetWorldTagMatrix_Hook()
    {
        static float* tempEDI;
        static float dummyViewAxis[9];

        __asm mov tempEDI, edi 
        __asm pushad

        {
            if (Components::CameraManager::Get().GetActiveCamera()->IsModControlledCameraMode() &&
                Components::CameraManager::Get().GetActiveCamera()->GetMode() != Components::Camera::Mode::Bone)
                tempEDI = dummyViewAxis;
        }

        __asm popad
        __asm mov edi, tempEDI 
        __asm jmp CG_DObjGetWorldTagMatrix_Trampoline
    }

    // T4 port (2026-05-23): partial install path — hooks ONLY
    // R_SetViewParmsForScene. The full Install() above calls WriteCall against
    // AnglesToAxis / CG_CalcViewValues / CG_OffsetThirdPersonView /
    // FX_SetupCamera / CG_DObjGetWorldTagMatrix, all of which are still
    // HardAddr<0> in T4 Signatures.hpp — calling them would WriteCall to
    // address 0 and crash. This variant gives us position + FOV freecam
    // control NOW; angles + FX + bone-tag come online as those addresses
    // are wired in later cycles.
    void InstallRefdefOnly()
    {
        const auto rsvp_va = GetGameAddresses().R_SetViewParmsForScene();
        if (!rsvp_va)
        {
            LOG_WARN("Hooks::Camera::InstallRefdefOnly: R_SetViewParmsForScene address is 0, skipping");
            return;
        }
        T4::HookManager::CreateHook(rsvp_va, (uintptr_t)R_SetViewParmsForScene_Hook,
                                &R_SetViewParmsForScene_Trampoline);
        LOG_INFO("Hooks::Camera::InstallRefdefOnly: installed at 0x{:08X}", rsvp_va);
    }

    void Install()
    {
        // rewrite the camera position and fov
        T4::HookManager::CreateHook(GetGameAddresses().R_SetViewParmsForScene(), (uintptr_t)R_SetViewParmsForScene_Hook,
                                &R_SetViewParmsForScene_Trampoline);

        // rewrite the camera angles
        AnglesToAxis_Address = GetGameAddresses().AnglesToAxis();
        T4::HookManager::WriteCall(GetGameAddresses().CG_CalcViewValues(), (uintptr_t)AnglesToAxis_Hook);
        T4::HookManager::WriteCall(GetGameAddresses().CG_OffsetThirdPersonView(), (uintptr_t)AnglesToAxis_Hook);

        // update position of world-space effects (such as smoke) with our new position
        T4::HookManager::CreateHook(GetGameAddresses().FX_SetupCamera(), (uintptr_t)FX_SetupCamera_Hook,
                                &FX_SetupCamera_Trampoline);

        // TODO: CG_CalcFov
        // TODO: bypass connection interrupted (CI) image / message by placing a return statement at 0x42F930

        // ignore writes to camera angles (this fixes things like the player knifing affecting the freecam)
        T4::HookManager::CreateHook(GetGameAddresses().CG_DObjGetWorldTagMatrix(), (uintptr_t)CG_DObjGetWorldTagMatrix_Hook,
                                &CG_DObjGetWorldTagMatrix_Trampoline);
    }

    void OnCameraChanged()
    {
        auto& camera = Components::CameraManager::Get().GetActiveCamera();
        auto isFreeCamera = camera->IsModControlledCameraMode();

        Functions::FindDvar("cg_thirdperson")->current.enabled =
            (camera->GetMode() == Components::Camera::Mode::ThirdPerson || isFreeCamera) ? 1 : 0;
        Functions::FindDvar("cg_draw2d")->current.enabled = (isFreeCamera) ? 0 : 1;
        Functions::FindDvar("cg_drawShellshock")->current.enabled = (isFreeCamera) ? 0 : 1;

        constexpr int32_t LODBIAS = -40000;
        Functions::FindDvar("r_lodBiasRigid")->current.value = LODBIAS;
        Functions::FindDvar("r_lodBiasSkinned")->current.value = LODBIAS;
    }
}  // namespace IWXMVM::T4::Hooks::Camera
