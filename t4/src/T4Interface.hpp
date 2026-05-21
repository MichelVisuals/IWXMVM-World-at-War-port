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
            // T4 port WIP: globals (clientConnection, clientStatic, etc.) are
            // still HardAddr<0>, so any access AVs. Hard-pin to MainMenu so
            // EndScene_Hook never tries to read uninitialized state. This
            // disables demo-aware features (Render()) but lets the overlay
            // attempt to render.
            //
            // Re-enable the real logic once clientConnection is wired and the
            // T4 dvar_s struct layout is fixed (IW3 reads current.enabled at
            // offset 0x0C; T4 needs 0x10 due to alignment pad).
            return Types::GameState::MainMenu;
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
            str += (str.ends_with(".dm_NA")) ? "" : ".dm_NA";
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
            return {".dm_NA"};
        }

        void PlayDemo(std::filesystem::path demoPath) final
        {
            Events::Invoke(EventType::PreDemoLoad);
            
            const auto demoDirectory =
                std::filesystem::path(GetDvar("fs_basepath")->value->string) / "players" / "demos";

            try
            {
                LOG_INFO("Playing demo {0}", demoPath.string());

                if (!std::filesystem::exists(demoPath) || !std::filesystem::is_regular_file(demoPath))
                    return;

                const auto tempDemoDirectory = demoDirectory / DEMO_TEMP_DIRECTORY;
                if (!std::filesystem::exists(tempDemoDirectory))
                    std::filesystem::create_directories(tempDemoDirectory);

                const auto targetPath = tempDemoDirectory / demoPath.filename();
                if (std::filesystem::exists(targetPath) && std::filesystem::is_regular_file(targetPath))
                    std::filesystem::remove(targetPath);

                std::filesystem::copy(demoPath, targetPath);

                Functions::Cbuf_AddText(
                    std::format(R"(demo "{0}/{1}")", DEMO_TEMP_DIRECTORY, targetPath.filename().string()));
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
            #define ICO_STAGE(tag) do { static bool _l=false; if(!_l){_l=true; LOG_DEBUG("IsConsoleOpen stage: " tag);} } while(0)
            ICO_STAGE("I0: enter");
            // T4 port WIP: hard-pinned to false until clientUIActives is wired.
            // Previously this deref'd HardAddr<0> -> SEH AV. Returning false
            // unconditionally is fine: console state polling is only used by
            // Input::KeyDown to suppress input while typing in the in-game
            // console, and we have no way to detect that yet.
            return false;
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
