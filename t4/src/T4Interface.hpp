#pragma once
#include "StdInclude.hpp"
#include "GameInterface.hpp"

#include "Structures.hpp"
#include "Functions.hpp"
#include "Hooks.hpp"
#include "Events.hpp"
#include "DemoParser.hpp"
#include "Hooks/Camera.hpp"
#include "Hooks/Playback.hpp"
#include "Hooks/HUD.hpp"
#include "Addresses.hpp"
#include "Patches.hpp"
#include "Components/Rewinding.hpp"

#include "glm/vec3.hpp"
#include "glm/gtc/type_ptr.hpp"

namespace IWXMVM::T4
{
    class T4Interface : public GameInterface
    {
       public:
        T4Interface() : GameInterface(Types::Game::T4)
        {
        }

        void ExecuteNewServerCommands() final
        {
            const auto clc = Structures::GetClientConnection();
            const auto cgs = Structures::GetClientGlobalsStatic();

            const auto oldServerCommandSequence = cgs->serverCommandSequence;
            const auto newServerCommandSequence = clc->serverCommandSequence;

            // check if the command string backlog is equal to or greater than half the size of command string buffer (128 / 2 = 64)
            if (oldServerCommandSequence > 0 && oldServerCommandSequence + std::ssize(clc->serverCommands) / 2 <= newServerCommandSequence)
            {
                for (auto i = oldServerCommandSequence + 1; i <= newServerCommandSequence; ++i)
                {
                    if (static constexpr auto dvar = 'd'; clc->serverCommands[i & 127][0] != dvar)
                    {
                        // erase server commands that do not modify the gamestate strings
                        clc->serverCommands[i & 127][0] = '\0';
                    }
                }

                const auto CG_ExecuteNewServerCommands = GetGameAddresses().CG_ExecuteNewServerCommands();
                __asm
                {
                    pushad
                    mov edi, newServerCommandSequence
                    xor esi, esi // localClientNum
                    push edi
                    push esi
                    call CG_ExecuteNewServerCommands
                    add esp, 8
                    popad
                }

                for (auto i = oldServerCommandSequence + 1; i <= newServerCommandSequence; ++i)
                {
                    // erase server commands to prevent double processing; shouldn't be necessary but just to be sure
                    clc->serverCommands[i & 127][0] = '\0';
                }
            }
        }

        void InstallHooksAndPatches() final
        {
            // T4 port work-in-progress: most signatures are still unverified, so
            // installing hooks/patches against zero addresses crashes the game.
            // Hooks::Commands::Install in particular dereferences hardcoded IW3
            // addresses (0x1410B3C, 0x14099DC) which are garbage in T4 memory.
            //
            // Until each install path has a verified T4 address (or its own null
            // guard), the entire pass is disabled. D3D9 hooks from D3D9::Initialize
            // and the ImGui overlay still come up via the dummy-device vtable,
            // which is enough to confirm the injection model end-to-end.
            //
            // Re-enable each call as its dependencies are wired up.
            // Hooks::Install();
            // Patches::GetGamePatches();
        }

        void DisableRawInput()
        {
            // disable raw_input because it messes with our IN_Frame patch
            // on cod4x
            auto raw_input = Functions::FindDvar("raw_input");
            if (raw_input)
            {
                raw_input->current.enabled = false;
            }
        }

        void SetupEventListeners() final
        {
            LOG_DEBUG("SetupEventListeners: entered");
            // T4 port diagnostic: probe a couple of well-known dvars to
            // verify the new dvar_s layout reads real values, not garbage
            // pad bytes. cg_fov default is ~65 on T4, sv_cheats default 0.
            // Log nullness explicitly so we can distinguish "FindDvar
            // broken" from "layout broken".
            {
                auto d = Functions::FindDvar("cg_fov");
                LOG_DEBUG("dvar probe: cg_fov ptr={} value={}", (void*)d, d ? d->current.value : -1.0f);
            }
            {
                auto d = Functions::FindDvar("sv_cheats");
                LOG_DEBUG("dvar probe: sv_cheats ptr={} enabled={}", (void*)d, d ? d->current.enabled : false);
            }
            {
                auto d = Functions::FindDvar("fs_basepath");
                LOG_DEBUG("dvar probe: fs_basepath ptr={} string={}", (void*)d, (d && d->current.string) ? d->current.string : "(null)");
            }

            DisableRawInput();

            Events::RegisterListener(EventType::PostDemoLoad, DemoParser::Run);

            Events::RegisterListener(EventType::OnCameraChanged, Hooks::Camera::OnCameraChanged);

            Events::RegisterListener(EventType::PostDemoLoad, [&]() { 
                Functions::FindDvar("sv_cheats")->current.enabled = true; 
                DisableRawInput();
                    
                // ensure these are set to their defaults, so our killfeed toggle works properly
                Functions::FindDvar("con_gamemsgwindow0msgtime")->current.value = 5;
                Functions::FindDvar("con_gamemsgwindow0linecount")->current.integer = 4;
            });
        }

        IDirect3DDevice9* GetGameDevicePtr() const final
        {
            const auto addr = GetGameAddresses().d3d9DevicePointer();
            if (!addr)
            {
                LOG_DEBUG("GetGameDevicePtr: d3d9DevicePointer address is 0");
                return nullptr;
            }
            auto dev = *(IDirect3DDevice9**)addr;
            LOG_DEBUG("GetGameDevicePtr: addr=0x{:X} -> device=0x{:X}", addr, (std::uintptr_t)dev);
            return dev;
        }

        uintptr_t GetWndProc() final
        {
            return (uintptr_t)GetGameAddresses().MainWndProc();
        }

        void SetMouseMode(Types::MouseMode mode) final
        {
            if (mode == Types::MouseMode::Capture)
                Patches::GetGamePatches().IN_Frame.Apply();
            else 
                Patches::GetGamePatches().IN_Frame.Revert();
        }

        Types::GameState GetGameState() final
        {
            // T4 port: real logic restored now that clientConnection is wired
            // (0x00B71390). T4's clientConnection_t may have a different
            // demoplaying offset than IW3 — if GetGameState never transitions
            // to InDemo, search for the field by comparing memory before/after
            // demo load (look for a dword that flips from 0 to nonzero).
            const auto addr = GetGameAddresses().clientConnection();
            if (!addr) return Types::GameState::MainMenu;

            // cl_ingame dvar reflects whether we're in any active session.
            // Now that FindDvar works, this gates MainMenu vs the rest.
            auto cl_ingame = Functions::FindDvar("cl_ingame");
            if (!cl_ingame || !cl_ingame->current.enabled)
                return Types::GameState::MainMenu;

            // T4 port diagnostic: log the first 32 dwords of clc once per
            // session, plus every time GameState changes, so we can spot
            // the demoplaying-offset shift if any. Keep low-frequency.
            static int last_state = -1;
            const auto clc = Structures::GetClientConnection();
            int cur_state;
            if (clc->demoplaying)
                cur_state = (int)Types::GameState::InDemo;
            else
                cur_state = (int)Types::GameState::InGame;
            if (cur_state != last_state)
            {
                last_state = cur_state;
                const auto p = reinterpret_cast<const std::uint32_t*>(clc);
                std::string s;
                for (int i = 0; i < 16; ++i) s += std::format("[{:02}]=0x{:08X} ", i, p[i]);
                LOG_DEBUG("GameState transition: cl_ingame=1, demoplaying={}, clc dwords: {}",
                          (int)clc->demoplaying, s);
            }

            if (clc->demoplaying)
                return Types::GameState::InDemo;
            return Types::GameState::InGame;
        }

        Types::Features GetSupportedFeatures() final
        {
            return Types::Features_ChangeAnimations;
        }

        void InitializeGameAddresses() final
        {
            GetGameAddresses();
        }

        std::optional<std::span<HMODULE>> GetModuleHandles(Types::ModuleType type = Types::ModuleType::BaseModule) final
        {
            static std::vector<HMODULE> modules = {::GetModuleHandle(nullptr)};

            if (type == Types::ModuleType::BaseModule)
                return std::span{modules.begin(), modules.end()};
            else
                return std::nullopt;
        }

        Types::DemoInfo demoInfo;

        Types::DemoInfo GetDemoInfo() final
        {
            demoInfo.name = Structures::GetClientStatic()->servername;
            demoInfo.name = demoInfo.name.starts_with(DEMO_TEMP_DIRECTORY)
                                ? demoInfo.name.substr(strlen(DEMO_TEMP_DIRECTORY) + 1)
                                : demoInfo.name;

            std::string str = static_cast<std::string>(Structures::GetClientStatic()->servername);
            str += (str.ends_with(".dm_6")) ? "" : ".dm_6";
            demoInfo.path = Functions::GetFilePath(std::move(str));

            auto [demoStartTick, demoEndTick] = DemoParser::GetDemoTickRange();

            const auto serverTime = Structures::GetClientActive()->serverTime;
            if (serverTime > demoStartTick && serverTime < demoEndTick && !Components::Rewinding::IsRewinding())
            {
                demoInfo.gameTick = serverTime - demoStartTick;
            }
            demoInfo.endTick = demoEndTick - demoStartTick;

            return demoInfo;
        }

        std::string_view GetDemoExtension() final
        {
            return {".dm_6"};
        }

        void PlayDemo(std::filesystem::path demoPath) final
        {
            Events::Invoke(EventType::PreDemoLoad);

            // T4 port: WaW always reads demos from
            // %LocalAppData%\Activision\CoDWaW\demos\, regardless of how
            // Steam configures fs_homepath. On a Steam install the
            // fs_homepath dvar is rewritten to the Steam game directory
            // (e.g. D:\SteamLibrary\...\Call of Duty World at War\) so we
            // can't trust it as a demo-write target. Use the Windows
            // LOCALAPPDATA env var which is what WaW actually consults
            // internally for its profile/demos folder.
            char localAppData[MAX_PATH];
            DWORD ladLen = ::GetEnvironmentVariableA("LOCALAPPDATA", localAppData, MAX_PATH);
            if (ladLen == 0 || ladLen >= MAX_PATH)
            {
                LOG_ERROR("PlayDemo: LOCALAPPDATA env var not resolvable (len={})", ladLen);
                return;
            }
            const auto demoDirectory =
                std::filesystem::path(localAppData) / "Activision" / "CoDWaW" / "demos";

            try
            {
                LOG_INFO("Playing demo {0}", demoPath.string());

                if (!std::filesystem::exists(demoPath) || !std::filesystem::is_regular_file(demoPath))
                    return;

                if (!std::filesystem::exists(demoDirectory))
                    std::filesystem::create_directories(demoDirectory);

                // WaW's `demo` command does NOT support subdirectories — even
                // when the file is at <homepath>/demos/IWXTMP/<name>.dm_6 the
                // game's filesystem reports "demos/IWXTMP/<name>.dm_6 not
                // found". So we flatten: if the demo already lives in the
                // standard demos dir, play it in place; otherwise copy it
                // there with its original filename and play that.
                //
                // TODO (rewinding): when rewinding lands, copy with an
                // iwxmvm_ prefix to avoid shadowing user recordings.
                std::filesystem::path resolvedPath = demoPath;
                const bool alreadyInDemos =
                    std::filesystem::equivalent(demoPath.parent_path(), demoDirectory);
                if (!alreadyInDemos)
                {
                    resolvedPath = demoDirectory / demoPath.filename();
                    if (std::filesystem::exists(resolvedPath) && std::filesystem::is_regular_file(resolvedPath))
                        std::filesystem::remove(resolvedPath);
                    std::filesystem::copy(demoPath, resolvedPath);
                    LOG_DEBUG("PlayDemo: copied to {0}", resolvedPath.string());
                }
                else
                {
                    LOG_DEBUG("PlayDemo: source already in demos dir, skipping copy");
                }

                // WaW expects the demo name WITHOUT extension — it appends
                // .dm_<protocol> itself.
                const auto demoArg = resolvedPath.stem().string();
                LOG_DEBUG("PlayDemo: issuing `demo \"{0}\"`", demoArg);
                Functions::Cbuf_AddText(std::format(R"(demo "{0}")", demoArg));
            }
            catch (std::filesystem::filesystem_error& e)
            {
                LOG_ERROR("Failed to play demo file {0}: {1}", demoPath.string(), e.what());
            }
        }

        void Disconnect()
        {
            Functions::Cbuf_AddText("disconnect");
        }

        void Vid_Restart()
        {
            Functions::Cbuf_AddText("vid_restart");
        }

        bool IsConsoleOpen() final
        {
            // T4 port: clientUIActives wired to 0x00F44780 via IW3 sig
            // pattern match. Null-guard in case the address ever resolves
            // to 0 (e.g. signature-scan fallback fails).
            const auto addr = GetGameAddresses().clientUIActives();
            if (!addr) return false;
            return (Structures::GetClientUIActives()->keyCatchers & 1) != 0;
        }

        std::optional<Types::Dvar> GetDvar(const std::string_view name) final
        {
            const auto iw3Dvar = Functions::FindDvar(name);

            if (!iw3Dvar)
                return std::nullopt;

            Types::Dvar dvar;
            dvar.name = iw3Dvar->name;
            dvar.value = (Types::Dvar::Value*)&iw3Dvar->current;

            return dvar;
        }

        void SetFov(float fov) final
        {
            Functions::FindDvar("cg_fov")->current.value = fov;
        }

        Types::Sun GetSun() final
        {
            const auto& r_lightTweakSunDirection = Functions::FindDvar("r_lightTweakSunDirection");
            const auto& r_lightTweakSunColor = Functions::FindDvar("r_lightTweakSunColor");
            const auto& r_lightTweakSunLight = Functions::FindDvar("r_lightTweakSunLight");

            auto unpackedColor = glm::unpackUint4x8(r_lightTweakSunColor->current.integer);

            Types::Sun sun;
            sun.color = glm::vec3(unpackedColor.x / 255.0f, unpackedColor.y / 255.0f, unpackedColor.z / 255.0f);
            sun.direction = glm::vec3(
                r_lightTweakSunDirection->current.vector[0], 
                r_lightTweakSunDirection->current.vector[1],
                r_lightTweakSunDirection->current.vector[2]
            );
            sun.brightness = Functions::FindDvar("r_lightTweakSunLight")->current.value;
            return sun;
        }

        Types::DoF GetDof()
        {
            Types::DoF dof = 
            {
                Functions::FindDvar("r_dof_tweak")->current.enabled &&
                    Functions::FindDvar("r_dof_enable")->current.enabled,
                Functions::FindDvar("r_dof_farBlur")->current.value,
                Functions::FindDvar("r_dof_farStart")->current.value,
                Functions::FindDvar("r_dof_farEnd")->current.value,
                Functions::FindDvar("r_dof_nearBlur")->current.value,
                Functions::FindDvar("r_dof_nearStart")->current.value,
                Functions::FindDvar("r_dof_nearEnd")->current.value,
                Functions::FindDvar("r_dof_bias")->current.value
            };

            return dof;
        }

        Types::Filmtweaks GetFilmtweaks()
        {
            Types::Filmtweaks filmtweaks = {
                Functions::FindDvar("r_filmUseTweaks")->current.enabled &&
                    Functions::FindDvar("r_filmTweakEnable")->current.enabled,
                Functions::FindDvar("r_filmTweakBrightness")->current.value,
                Functions::FindDvar("r_filmTweakContrast")->current.value,
                Functions::FindDvar("r_filmTweakDesaturation")->current.value,
                glm::make_vec3(Functions::FindDvar("r_filmTweakLightTint")->current.vector),
                glm::make_vec3(Functions::FindDvar("r_filmTweakDarkTint")->current.vector),
                Functions::FindDvar("r_filmTweakInvert")->current.enabled
            };

            return filmtweaks;
        }

        Types::HudInfo GetHudInfo()
        {
            glm::vec3 teamColorAllies;
            auto ss = std::stringstream(Functions::FindDvar("g_TeamColor_Allies")->current.string);
            ss >> teamColorAllies[0] >> teamColorAllies[1] >> teamColorAllies[2];
            
            glm::vec3 teamColorAxis;
            ss = std::stringstream(Functions::FindDvar("g_TeamColor_Axis")->current.string);
            ss >> teamColorAxis[0] >> teamColorAxis[1] >> teamColorAxis[2];

            Types::HudInfo hudInfo = {
                Functions::FindDvar("cg_draw2D")->current.enabled,
                !Functions::FindDvar("ui_hud_hardcore")->current.enabled,
                Functions::FindDvar("cg_drawShellshock")->current.enabled,
                Functions::FindDvar("ui_drawCrosshair")->current.enabled, 
                Hooks::HUD::showScore,
                Hooks::HUD::showOtherText, 
                !Patches::GetGamePatches().CG_DrawPlayerLowHealthOverlay.IsApplied(),
                Functions::FindDvar("ui_hud_obituaries")->current.string[0] == '1',
                teamColorAllies,   
                teamColorAxis
            };

            return hudInfo;
        }

        void SetSun(Types::Sun sun) final
        {
            const auto& r_lightTweakSunDirection = Functions::FindDvar("r_lightTweakSunDirection");
            const auto& r_lightTweakSunColor = Functions::FindDvar("r_lightTweakSunColor");
            const auto& r_lightTweakSunLight = Functions::FindDvar("r_lightTweakSunLight");
            auto packedColor = glm::packUint4x8(glm::i8vec4(static_cast<uint8_t>(sun.color.x * 255),
                                                           static_cast<uint8_t>(sun.color.y * 255),
                                                           static_cast<uint8_t>(sun.color.z * 255), 1));
            for (int i = 0; i < 3; ++i)
            {
                r_lightTweakSunDirection->current.vector[i] = sun.direction[i];
            }
            r_lightTweakSunColor->current.integer = packedColor;
            r_lightTweakSunLight->current.value = sun.brightness;

            r_lightTweakSunDirection->modified = true;
            r_lightTweakSunColor->modified = true;
            r_lightTweakSunLight->modified = true;
        }

        void SetDof(Types::DoF dof) final
        {
            Functions::FindDvar("r_dof_tweak")->current.enabled = dof.enabled;
            Functions::FindDvar("r_dof_enable")->current.enabled = dof.enabled;
            
            Functions::FindDvar("r_dof_farBlur")->current.value = dof.farBlur;
            Functions::FindDvar("r_dof_farStart")->current.value = dof.farStart;
            Functions::FindDvar("r_dof_farEnd")->current.value = dof.farEnd;
            
            // hacky workaround because nearblur works weirdly in this game
            if (dof.nearBlur < 1.3f)
            {
                dof.nearBlur = 5;
                dof.nearStart = 0;
                dof.nearEnd = 0;
            }

            Functions::FindDvar("r_dof_nearBlur")->current.value = dof.nearBlur;
            Functions::FindDvar("r_dof_nearStart")->current.value = dof.nearStart;
            Functions::FindDvar("r_dof_nearEnd")->current.value = dof.nearEnd;

            Functions::FindDvar("r_dof_bias")->current.value = dof.bias;
        }

        void SetFilmtweaks(Types::Filmtweaks filmtweaks) final
        {
            Functions::FindDvar("r_filmUseTweaks")->current.enabled = filmtweaks.enabled;
            Functions::FindDvar("r_filmTweakEnable")->current.enabled = filmtweaks.enabled;
            Functions::FindDvar("r_filmTweakBrightness")->current.value = filmtweaks.brightness;
            Functions::FindDvar("r_filmTweakContrast")->current.value = filmtweaks.contrast;
            Functions::FindDvar("r_filmTweakDesaturation")->current.value = filmtweaks.desaturation;
            for (int i = 0; i < 3; ++i)
            {
                Functions::FindDvar("r_filmTweakLightTint")->current.vector[i] =
                    glm::value_ptr(filmtweaks.tintLight)[i];
                Functions::FindDvar("r_filmTweakDarkTint")->current.vector[i] = glm::value_ptr(filmtweaks.tintDark)[i];
            }
            Functions::FindDvar("r_filmTweakInvert")->current.enabled = filmtweaks.invert;
        }

        void SetHudInfo(Types::HudInfo hudInfo) final
        {
            Functions::FindDvar("con_gamemsgwindow0msgtime")->current.value = 5;
            Functions::FindDvar("con_gamemsgwindow0linecount")->current.integer = 4;

            Functions::FindDvar("cg_draw2D")->current.enabled = hudInfo.show2DElements;

            Functions::FindDvar("ui_hud_hardcore")->current.enabled = !hudInfo.showPlayerHUD;
            Functions::FindDvar("cg_centertime")->current.value = hudInfo.showPlayerHUD ? 5.0f : 0.0f;
            Functions::FindDvar("cg_overheadranksize")->current.value = hudInfo.showPlayerHUD ? 0.5f : 0;
            Functions::FindDvar("cg_overheadnamessize")->current.value = hudInfo.showPlayerHUD ? 0.5f : 0;
            Functions::FindDvar("cg_overheadiconsize")->current.value = hudInfo.showPlayerHUD ? 0.7f : 0;

            Functions::FindDvar("cg_drawShellshock")->current.enabled = hudInfo.showShellshock;
            Functions::FindDvar("ui_hud_obituaries")->current.string = hudInfo.showKillfeed ? "1" : "0";
            Functions::FindDvar("ui_drawCrosshair")->current.enabled = hudInfo.showCrosshair;
            Hooks::HUD::showScore = hudInfo.showScore;
            Hooks::HUD::showOtherText = hudInfo.showOtherText;
            if (hudInfo.showBloodOverlay)
            {
                Patches::GetGamePatches().CG_DrawPlayerLowHealthOverlay.Revert();
                Patches::GetGamePatches().CG_DrawFlashDamage.Revert();
                Patches::GetGamePatches().CG_DrawDamageDirectionIndicators.Revert();
            }
            else
            {
                Patches::GetGamePatches().CG_DrawPlayerLowHealthOverlay.Apply();
                Patches::GetGamePatches().CG_DrawFlashDamage.Apply();
                Patches::GetGamePatches().CG_DrawDamageDirectionIndicators.Apply();
            }

            std::stringstream teamColorAllies;
            teamColorAllies << hudInfo.killfeedTeam1Color[0] << " " << hudInfo.killfeedTeam1Color[1] << " "
                            << hudInfo.killfeedTeam1Color[2] << " 1\0";

            Functions::Dvar_SetStringByName("g_TeamColor_Allies", teamColorAllies.str().c_str());

            std::stringstream teamColorAxis;
            teamColorAxis << hudInfo.killfeedTeam2Color[0] << " " << hudInfo.killfeedTeam2Color[1] << " " 
                          << hudInfo.killfeedTeam2Color[2] << " 1\0";
            Functions::Dvar_SetStringByName("g_TeamColor_Axis", teamColorAxis.str().c_str());
        }
        
        std::vector<Types::Entity> GetEntities() final
        {
            std::vector<Types::Entity> entities;
            
            auto cg_entities = Structures::GetEntities();

            auto ToEntityType = [](char eType) -> Types::EntityType {
                switch (eType)
                {
                    case Structures::entityType_t::ET_PLAYER:
                        return Types::EntityType::Player;
                    case Structures::entityType_t::ET_PLAYER_CORPSE:
                        return Types::EntityType::Corpse;
                    case Structures::entityType_t::ET_ITEM:
                        return Types::EntityType::Item;
                    case Structures::entityType_t::ET_MISSILE:
                        return Types::EntityType::Missile;
                    case Structures::entityType_t::ET_HELICOPTER:
                        return Types::EntityType::Helicopter;
                    default:
                        return Types::EntityType::Unsupported;
                }
            };

            for (int i = 0; i < 256; i++)
            {
                auto entity = cg_entities[i];
                entities.push_back(
                    Types::Entity
                    {
                        .id = i, 
                        .type = ToEntityType(entity.pose.eType),
                        .clientNum = entity.nextState.clientNum,
                        .isValid = entity.nextValid
                    }
                );
            }

            return entities;
        }

        auto FindBoneIndex(Structures::DObj_s* dobj, uint16_t boneName)
        {
            if (!dobj->models || !dobj->numModels)
                return -1;

            auto boneIndex = -1;

            auto totalBones = 0;
            for (int m = 0; m < dobj->numModels; m++)
            {
                auto model = dobj->models[m];
                if (!model || !model->numBones)
                    return -1;
                for (int b = 0; b < model->numBones; b++)
                {
                    auto bone = model->boneNames[b];
                    if (bone == boneName)
                    {
                        boneIndex = totalBones + b;
                    }
                }
                totalBones += model->numBones;
            }

            return boneIndex;
        }

        Types::BoneData GetBoneData(int32_t entityId, const std::string& name) final
        {
            uint16_t* clientObjMap = Structures::GetClientObjectMap();
            Structures::DObj_s* objBuf = Structures::GetObjBuf();

            uint16_t dobjIndex = clientObjMap[entityId];
            Structures::DObj_s* dobj = &objBuf[dobjIndex];

            auto entities = Structures::GetEntities();
            auto entity = &entities[entityId];
            auto boneName = Functions::SL_GetStringOfSize(name.c_str(), 1, name.size() + 1);

            const auto orgTimeStamp = std::exchange(dobj->skel.timeStamp, Structures::GetClientActive()->skelTimeStamp);

            auto boneIndex = FindBoneIndex(dobj, boneName);
            if (boneIndex == -1)
            {
                // LOG_ERROR("Bone {0} was not found in {1} models", boneName, (int)dobj->numModels);
                dobj->skel.timeStamp = orgTimeStamp;
                return {.id = -1};
            }

            float rotationMatrix[3 * 3];
            float origin[3];
            auto result = Functions::CG_DObjGetWorldBoneMatrix(entity, boneIndex, (float*)rotationMatrix, dobj, origin);

            dobj->skel.timeStamp = orgTimeStamp;
            if (!result)
            {
                LOG_ERROR("Call to CG_DObjGetWorldTagMatrix failed");
                return {.id = -1};
            }

            Types::BoneData boneData;
            boneData.id = boneIndex;
            boneData.position = glm::make_vec3(origin);
            boneData.rotation = glm::make_mat3(rotationMatrix);
            return boneData;
        }

        constexpr std::vector<std::string> GetSupportedBoneNames()
        {
            return
            {
                "tag_weapon", 
                "tag_flash",     
                "tag_clip",      
                "tag_brass",
                "j_head",
                "j_mainroot",
                "j_wrist_le",
                "j_wrist_ri",
                "j_shoulder_le",
                "j_shoulder_ri",
                "j_ankle_le",
                "j_ankle_ri",
                "tag_origin"
            };
        }



        void CL_FirstSnapshot()
        {
            uintptr_t CL_FirstSnapshot = GetGameAddresses().CL_FirstSnapshot();

            Patches::GetGamePatches().Con_TimeJumped.Apply();

            _asm
            {
                pushad
                xor eax, eax
                call CL_FirstSnapshot
                popad
            }

            Patches::GetGamePatches().Con_TimeJumped.Revert();
        }

        void ResetClientData(int serverTime)
        {
            auto cl = Structures::GetClientActive();
            for (auto& snapshot : std::span{ cl->snapshots }) 
                snapshot.valid = 0;

            cl->snap.serverTime = serverTime;
            cl->serverTime = 0;
            cl->oldServerTime = 0;
            cl->oldFrameServerTime = 0;
            cl->serverTimeDelta = 0;
            cl->oldSnapServerTime = 0;

            auto clc = Structures::GetClientConnection();
            clc->timeDemoFrames = 0;
            clc->timeDemoStart = 0;
            clc->timeDemoPrev = 0;
            clc->timeDemoBaseTime = 0;

            auto cls = Structures::GetClientStatic();
            cls->realtime = 0;
            cls->realFrametime = 0;

            auto cgs = Structures::GetClientGlobalsStatic();
            cgs->processedSnapshotNum = 0;

            auto cg = Structures::GetClientGlobals();
            cg->latestSnapshotNum = 0;
            cg->latestSnapshotTime = 0;
            cg->snap = 0;
            cg->nextSnap = 0;
            cg->landTime = 0;
        }

        Types::PlaybackData GetPlaybackDataAddresses() const
        {
            auto cl = Structures::GetClientActive();
            auto clc = Structures::GetClientConnection();
            auto cgs = Structures::GetClientGlobalsStatic();
            auto cls = Structures::GetClientStatic();

            return Types::PlaybackData
            {
                .cl = 
                {
                    .snap_serverTime = reinterpret_cast<uintptr_t>(&cl->snap.serverTime),
                    .serverTime = reinterpret_cast<uintptr_t>(&cl->serverTime),
                    .parseEntitiesNum = reinterpret_cast<uintptr_t>(&cl->parseEntitiesNum),
                    .parseClientsNum = reinterpret_cast<uintptr_t>(&cl->parseClientsNum),
                },
                .clc =
                {
                    .serverCommandSequence = reinterpret_cast<uintptr_t>(&clc->serverCommandSequence),
                    .lastExecutedServerCommand = reinterpret_cast<uintptr_t>(&clc->lastExecutedServerCommand),
                    .serverCommands = 
                    {
                        .address = reinterpret_cast<uintptr_t>(&clc->serverCommands),
                        .size = 128 * 1024
                    },
                    .serverConfigDataSequence = reinterpret_cast<uintptr_t>(clc->statPacketSendTime), // CoD4X
                },
                .cgs = 
                {
                    .serverCommandSequence = reinterpret_cast<uintptr_t>(&cgs->serverCommandSequence),
                },
                .cls = 
                {
                    .realtime = reinterpret_cast<uintptr_t>(&cls->realtime),
                },
                .s_compassActors = {.address = GetGameAddresses().s_compassActors(), .size = 64 * 48},
                .teamChatMsgs = 
                {
                    .address = reinterpret_cast<uintptr_t>(Structures::GetClientGlobalsStatic()->teamChatMsgs),
                    .size = 8 * 160 + 4 * 8 + 4 + 4
                },
                .cg_entities = {.address = reinterpret_cast<uintptr_t>(Structures::GetEntities()), .size = 72 * 476},
                .clientInfo = 
                {
                    .address = reinterpret_cast<uintptr_t>(Structures::GetClientGlobals()->bgs.clientinfo),
                    .size = 64 * sizeof(Structures::clientInfo_t)
                },
                .gameState = 
                {
                    .address = reinterpret_cast<uintptr_t>(&Structures::GetClientActive()->gameState),
                    .size = sizeof(Structures::gameState_t)
                },
                .killfeed = GetGameAddresses().conGameMsgWindow0()
            };
        }
    };
}  // namespace IWXMVM::T4
