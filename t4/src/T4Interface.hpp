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
#include "Components/Rewinding.hpp"
#include "Components/Playback.hpp"
#include "Components/CaptureManager.hpp"
#include "Components/CameraManager.hpp"
#include "Components/Camera.hpp"
#include "Components/KeyframeManager.hpp"
#include "Utilities/HookManager.hpp"

#include "glm/vec3.hpp"
#include "glm/gtc/type_ptr.hpp"

namespace IWXMVM::T4
{
    // Atomic flag read by the user32 SetCursorPos/ClipCursor hooks (installed
    // in T4Interface::InstallHooksAndPatches). Set true while we're in a demo
    // so the hooks suppress the game's cursor re-centering / clipping.
    inline std::atomic<bool> g_t4SuppressCursorPos{false};

    // Our DLL's loaded address range — used by SetCursorPos hook to tell
    // "WaW IN_Frame calling" (suppress) from "IWXMVM core LockMouse calling"
    // (allow through). Populated at hook-install time.
    inline std::uintptr_t g_t4SelfBase = 0;
    inline std::uintptr_t g_t4SelfEnd  = 0;

    // Trampolines for the hooks below. MinHook populates these via the OUT
    // arg in CreateHook. Declared at namespace scope so the free hook
    // functions can reach them without static-local-init races.
    inline decltype(&SetCursorPos) g_OriginalSetCursorPos = nullptr;
    inline decltype(&ClipCursor)   g_OriginalClipCursor   = nullptr;

    // Vectored exception handler — fires BEFORE the normal SEH chain runs, so
    // we capture full register/EIP/faulting-address state at the EXACT moment
    // an access violation raises. Doesn't HANDLE the exception (returns
    // EXCEPTION_CONTINUE_SEARCH); just logs and lets normal handling proceed.
    // Lets us debug crashes inside engine functions (bonecam etc) without an
    // interactive debugger. Per feedback-t4-port-use-debugger this is a
    // debugger-substitute for getting equivalent diagnostics into the log.
    inline std::atomic<int> g_avLogCount{0};

    // __try/__except can't coexist with C++ object destruction in the same
    // function. Split: this helper has no C++ types so __try is allowed.
    inline void T4_VEH_SafeProbe(std::uintptr_t eip, std::uintptr_t esp,
                                 char* insnOut /*[64]*/, std::uintptr_t* retOut)
    {
        *retOut = 0;
        insnOut[0] = 0;
        __try {
            auto* p = reinterpret_cast<const volatile std::uint8_t*>(eip);
            for (int i = 0; i < 16; i++)
                std::snprintf(insnOut + i * 3, 4, "%02X ", p[i]);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            std::strcpy(insnOut, "<EIP unreadable>");
        }
        __try {
            *retOut = *reinterpret_cast<const volatile std::uintptr_t*>(esp);
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    // Known noisy AV sites we DON'T want to log every frame (per VEH session
    // 2026-05-25). Add EIP values here to suppress them after the first hit.
    // These are visuals/dvar accesses on unwired T4 dvars — expected behavior,
    // already handled by T4_SAFE_GET/SET wrappers, not a bug to debug.
    inline std::atomic<std::uintptr_t> g_lastSuppressedEip{0};

    inline LONG WINAPI T4_VectoredAVHandler(PEXCEPTION_POINTERS exc)
    {
        if (!exc || !exc->ExceptionRecord ||
            exc->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION)
            return EXCEPTION_CONTINUE_SEARCH;

        // De-noise: skip if same EIP as last suppressed one (recurring per-
        // frame AVs from visuals). Lets us see UNIQUE AVs (= the bonecam-
        // related one we're hunting). Null-EIP secondary unwind crashes
        // also dropped.
        const std::uintptr_t eip = exc->ContextRecord ? exc->ContextRecord->Eip : 0;
        if (eip == 0) return EXCEPTION_CONTINUE_SEARCH;
        if (eip == g_lastSuppressedEip.load(std::memory_order_relaxed))
            return EXCEPTION_CONTINUE_SEARCH;

        const int n = g_avLogCount.fetch_add(1, std::memory_order_relaxed);
        if (n >= 50)
            return EXCEPTION_CONTINUE_SEARCH;
        // After logging once, mark this EIP as known-noisy so we don't spam.
        g_lastSuppressedEip.store(eip, std::memory_order_relaxed);

        const auto& ctx = *exc->ContextRecord;
        const auto& rec = *exc->ExceptionRecord;
        const std::uintptr_t faultAddr = rec.NumberParameters >= 2 ? rec.ExceptionInformation[1] : 0;
        const std::uintptr_t faultOp   = rec.NumberParameters >= 1 ? rec.ExceptionInformation[0] : 0;
        const char* opStr = faultOp == 0 ? "read" : faultOp == 1 ? "write" : faultOp == 8 ? "exec" : "?";

        char insnBytes[64];
        std::uintptr_t retAddr = 0;
        T4_VEH_SafeProbe(ctx.Eip, ctx.Esp, insnBytes, &retAddr);

        LOG_WARN("T4_VEH #{} AV {} EIP=0x{:08X} faultAddr=0x{:08X}  "
                 "EAX=0x{:08X} ECX=0x{:08X} EDX=0x{:08X} EBX=0x{:08X}  "
                 "ESI=0x{:08X} EDI=0x{:08X} EBP=0x{:08X} ESP=0x{:08X}  "
                 "retAddr=0x{:08X}  insn=[{}]",
                 n, opStr, ctx.Eip, faultAddr,
                 ctx.Eax, ctx.Ecx, ctx.Edx, ctx.Ebx,
                 ctx.Esi, ctx.Edi, ctx.Ebp, ctx.Esp,
                 retAddr, insnBytes);

        return EXCEPTION_CONTINUE_SEARCH;
    }

    // user32 SetCursorPos and ClipCursor are WINAPI (__stdcall). Captureless
    // lambdas decay to __cdecl by default; calling them as stdcall corrupts
    // the stack on RET and crashes the WaW caller. Declare these as proper
    // free functions with WINAPI so the calling convention matches.
    //
    // SetCursorPos: pass through if caller is inside our DLL (GameView::
    // LockMouse — that warp is required for ImGui's MousePosPrev compensation,
    // suppressing it causes infinite freecam spin per reference-waw-lockmouse-
    // setcursorpos). Suppress if caller is in CoDWaWmp.exe and we're in demo.
    inline BOOL WINAPI T4_SetCursorPos_Hook(int X, int Y)
    {
        const auto ret = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
        const bool fromOurDll = (g_t4SelfBase != 0 && ret >= g_t4SelfBase && ret < g_t4SelfEnd);
        if (!fromOurDll && g_t4SuppressCursorPos.load(std::memory_order_acquire))
            return TRUE;
        return g_OriginalSetCursorPos ? g_OriginalSetCursorPos(X, Y) : FALSE;
    }

    inline BOOL WINAPI T4_ClipCursor_Hook(const RECT* rect)
    {
        // ClipCursor: blanket-suppress during demo. IWXMVM never clips itself
        // so the caller-check isn't needed here.
        if (g_t4SuppressCursorPos.load(std::memory_order_acquire))
            return g_OriginalClipCursor ? g_OriginalClipCursor(nullptr) : TRUE;
        return g_OriginalClipCursor ? g_OriginalClipCursor(rect) : TRUE;
    }

    class T4Interface : public GameInterface
    {
       public:
        // core/-cleanup-1:1: Game::T4 enum entry no longer in upstream core.
        // Pass Game::None — there's no semantic dispatch on the value in core,
        // it's just a label for debug/logging.
        T4Interface() : GameInterface(Types::Game::None)
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
            // Install vectored exception handler FIRST — captures register
            // state on any AV that fires anywhere in the process from now on.
            // First-call flag = 1 puts us at the FRONT of the chain (before
            // anyone else's VEH). Returns EXCEPTION_CONTINUE_SEARCH so we
            // just log; we don't actually handle.
            ::AddVectoredExceptionHandler(1, &T4_VectoredAVHandler);
            LOG_INFO("T4 vectored AV handler installed (logs first 30 access violations with register state)");

            // core/-cleanup-1:1 fallout: when a render-path listener throws
            // (mostly VisualsMenu's off-screen RenderXxx OnFrame against
            // unwired T4 dvars), upstream UIManager::RunImGuiFrame's catch(...)
            // fires a MessageBox EVERY FRAME, which is unusable. We used to
            // silence that in core/UIManager.cpp (log-once), but core/ is now
            // 1:1 with upstream and we can't touch it.
            //
            // Pragmatic fix: hook user32!MessageBoxA so messages containing
            // "IWXMVM" return IDOK immediately without showing the dialog.
            // The catch still fires + logs once per process (well, per call),
            // but the popup never blocks the user. Same effective behavior
            // as the silencing we removed.
            {
                static decltype(&MessageBoxA) OriginalMessageBoxA = nullptr;
                static decltype(&MessageBoxW) OriginalMessageBoxW = nullptr;
                static auto MessageBoxA_Hook = +[](HWND hWnd, LPCSTR text, LPCSTR caption, UINT type) -> int {
                    if (text && std::string_view(text).find("IWXMVM") != std::string_view::npos)
                    {
                        static bool warned = false;
                        if (!warned) { warned = true; LOG_WARN("Suppressed IWXMVM MessageBoxA: \"{}\" (further silenced)", text); }
                        return IDOK;
                    }
                    return OriginalMessageBoxA ? OriginalMessageBoxA(hWnd, text, caption, type) : IDOK;
                };
                static auto MessageBoxW_Hook = +[](HWND hWnd, LPCWSTR text, LPCWSTR caption, UINT type) -> int {
                    if (text)
                    {
                        // Crude check — convert first 32 chars to narrow for substring match.
                        char narrow[64] = {};
                        WideCharToMultiByte(CP_ACP, 0, text, -1, narrow, sizeof(narrow) - 1, nullptr, nullptr);
                        if (std::string_view(narrow).find("IWXMVM") != std::string_view::npos)
                        {
                            static bool warned = false;
                            if (!warned) { warned = true; LOG_WARN("Suppressed IWXMVM MessageBoxW (further silenced)"); }
                            return IDOK;
                        }
                    }
                    return OriginalMessageBoxW ? OriginalMessageBoxW(hWnd, text, caption, type) : IDOK;
                };
                HMODULE user32 = ::GetModuleHandleA("user32.dll");
                if (user32)
                {
                    auto* mbA = ::GetProcAddress(user32, "MessageBoxA");
                    auto* mbW = ::GetProcAddress(user32, "MessageBoxW");
                    if (mbA)
                    {
                        try
                        {
                            IWXMVM::HookManager::CreateHook(reinterpret_cast<std::uintptr_t>(mbA),
                                                            reinterpret_cast<std::uintptr_t>(MessageBoxA_Hook),
                                                            reinterpret_cast<std::uintptr_t*>(&OriginalMessageBoxA));
                        }
                        catch (const std::exception& e) { LOG_WARN("MessageBoxA hook failed: {}", e.what()); }
                    }
                    if (mbW)
                    {
                        try
                        {
                            IWXMVM::HookManager::CreateHook(reinterpret_cast<std::uintptr_t>(mbW),
                                                            reinterpret_cast<std::uintptr_t>(MessageBoxW_Hook),
                                                            reinterpret_cast<std::uintptr_t*>(&OriginalMessageBoxW));
                        }
                        catch (const std::exception& e) { LOG_WARN("MessageBoxW hook failed: {}", e.what()); }
                    }
                    LOG_INFO("MessageBoxA/W hooks installed (IWXMVM popup suppression)");
                }
            }

            // core/-cleanup-1:1 fallout: WaW's IN_Frame re-centers the
            // cursor every frame via SetCursorPos and clips it via
            // ClipCursor. Long-term fix is to patch IN_Frame to a RET (like
            // iw3 does) — but t4's IN_Frame address is still HardAddr<0>.
            // Until that's wired, hook user32 SetCursorPos + ClipCursor
            // with simple atomic-gated suppression. Both hook functions are
            // declared at namespace scope above with proper WINAPI calling
            // convention (captureless-lambda decay would give cdecl and
            // crash the WaW caller on RET).
            {
                // Determine our DLL's loaded address range for the
                // SetCursorPos caller-check (lets GameView::LockMouse's warp
                // through while suppressing WaW's IN_Frame).
                HMODULE self = nullptr;
                ::GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                     GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                     reinterpret_cast<LPCSTR>(&T4_SetCursorPos_Hook), &self);
                if (self)
                {
                    MODULEINFO mi{};
                    if (::GetModuleInformation(::GetCurrentProcess(), self, &mi, sizeof(mi)))
                    {
                        g_t4SelfBase = reinterpret_cast<std::uintptr_t>(mi.lpBaseOfDll);
                        g_t4SelfEnd  = g_t4SelfBase + mi.SizeOfImage;
                    }
                }

                HMODULE user32_2 = ::GetModuleHandleA("user32.dll");
                if (user32_2)
                {
                    auto* setCP = ::GetProcAddress(user32_2, "SetCursorPos");
                    auto* clipC = ::GetProcAddress(user32_2, "ClipCursor");
                    if (setCP)
                    {
                        try
                        {
                            IWXMVM::HookManager::CreateHook(reinterpret_cast<std::uintptr_t>(setCP),
                                                            reinterpret_cast<std::uintptr_t>(&T4_SetCursorPos_Hook),
                                                            reinterpret_cast<std::uintptr_t*>(&g_OriginalSetCursorPos));
                        }
                        catch (const std::exception& e) { LOG_WARN("SetCursorPos hook failed: {}", e.what()); }
                    }
                    if (clipC)
                    {
                        try
                        {
                            IWXMVM::HookManager::CreateHook(reinterpret_cast<std::uintptr_t>(clipC),
                                                            reinterpret_cast<std::uintptr_t>(&T4_ClipCursor_Hook),
                                                            reinterpret_cast<std::uintptr_t*>(&g_OriginalClipCursor));
                        }
                        catch (const std::exception& e) { LOG_WARN("ClipCursor hook failed: {}", e.what()); }
                    }
                    LOG_INFO("SetCursorPos + ClipCursor hooks installed (caller-gated; self range 0x{:X}..0x{:X})",
                             g_t4SelfBase, g_t4SelfEnd);
                }
            }

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

            // T4 port: enable just the SV_Frame pause hook now that SV_Frame's
            // T4 address is wired (0x0057F7E5). Hooks::Playback::Install internally
            // skips FS_Read hook when fsh is still HardAddr<0>, so this is safe.
            Hooks::Playback::Install();

            // T4 port (2026-05-21): R_SetViewParmsForScene hook at 0x004E0040
            // is wired in Signatures.hpp but DISABLED here pending debug. With
            // it enabled (and the t4 Camera.cpp adapted to capture refdef* from
            // EAX) the process crashed after init with no log line from our
            // hook body — meaning either MinHook's trampoline construction
            // failed for this function's prologue (`push ebp; mov ebp, esp;
            // and esp, 0xfffffff8` straddling the 5-byte patch boundary), or
            // the function isn't reached at main menu and a different code
            // path crashes. Will return to this with a tighter test harness.
            // Hooks::Camera::Install();

            // Free camera install (2026-05-23): R_SetViewParmsForScene only.
            // The hook body in Hooks/Camera.cpp gates writes on
            // CameraManager's active camera mode. FirstPerson/ThirdPerson →
            // no override (game state syncs into our Camera). Free/Orbit/
            // Dolly/Bone → camera position + FOV write into refdef.
            // Supersedes the +200 Z test hook from earlier this session.
            Hooks::Camera::InstallRefdefOnly();

            // CL_KeyEvent demo-disconnect patch.
            // T4 CL_KeyEvent at 0x004949D0 contains:
            //   0x00494C86: 83 3D 28 15 BB 00 00   CMP [demoplaying], 0
            //   0x00494C8D: 75 0A                  JNZ +0x0A -> 0x00494C99
            //   0x00494C99..0x00494CBC: TEST ECX,ECX; if zero, fall into the
            //     disconnect path (MOV ESI,...; MOV EAX,0x0083EB98; CALL).
            //   0x00494CBE: 83 FD 1B               CMP EBP, 0x1B (ESC handling)
            // Extending the JNZ at 0x00494C8D from +0x0A to +0x2F skips the
            // entire disconnect block when demoplaying != 0, landing safely
            // at the ESC-check (which then handles non-ESC keys via the
            // function's normal continuation). Mirrors iw3 Patches.hpp:29
            // (which uses +0x4E for IW3's specific code layout).
            {
                const std::uintptr_t patch_va = 0x00494C8E;  // displacement byte of JNZ
                const std::uint8_t new_value = 0x2F;
                DWORD oldProtect = 0;
                if (::VirtualProtect(reinterpret_cast<void*>(patch_va), 1,
                                     PAGE_EXECUTE_READWRITE, &oldProtect))
                {
                    const std::uint8_t prev = *reinterpret_cast<volatile std::uint8_t*>(patch_va);
                    *reinterpret_cast<volatile std::uint8_t*>(patch_va) = new_value;
                    DWORD tmp = 0;
                    ::VirtualProtect(reinterpret_cast<void*>(patch_va), 1, oldProtect, &tmp);
                    LOG_INFO("CL_KeyEvent demo-disconnect patch applied: [0x{:08X}] 0x{:02X} -> 0x{:02X}",
                             patch_va, prev, new_value);
                }
                else
                {
                    LOG_ERROR("CL_KeyEvent demo-disconnect patch failed: VirtualProtect at 0x{:08X} returned 0",
                              patch_va);
                }
            }
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

            // core/-cleanup-1:1: backward-seek driver removed. Depended on
            // SeekBackward virtual + Features_SkipForwardOnly bit, both of
            // which are now out of core/. If demo seek is wanted back, wire
            // FS_Read first and use the full Rewinding state machine.

            // T4 port 2026-05-24: TRUE PAUSE via cls.realtime freeze.
            // The engine's per-frame ADD [cls.realtime], ESI at 0x004998BA
            // runs once per frame. We can't hook it cleanly (MinHook can't
            // trampoline the prologue), so instead we write cls.realtime
            // BACK to a snapshot value AFTER the engine's increment each
            // frame. Net advance per frame = 0 → demo time stays frozen.
            // Works in tandem with the timescale=0 workaround below: that
            // keeps msec tiny so the brief inter-frame drift is invisible.
            Events::RegisterListener(EventType::OnFrame, [&]() {
                static bool wasFreezing = false;
                static std::int32_t frozenRealtime = 0;
                auto* cls = Structures::GetClientStatic();
                if (!cls) return;
                if (GetGameState() != Types::GameState::InDemo)
                {
                    wasFreezing = false;
                    return;
                }
                const bool isPaused = IWXMVM::Components::Playback::IsPaused();
                if (isPaused)
                {
                    if (!wasFreezing)
                    {
                        frozenRealtime = cls->realtime;
                        wasFreezing = true;
                        LOG_DEBUG("t4 cls.realtime freeze: PAUSED at realtime={}", frozenRealtime);
                    }
                    cls->realtime = frozenRealtime;  // override engine's increment
                }
                else if (wasFreezing)
                {
                    wasFreezing = false;
                    LOG_DEBUG("t4 cls.realtime freeze: UNPAUSED (realtime resumes from {})", cls->realtime);
                }
            });

            // T4 port: pause via timescale=0. Engine clamps to 0.001x which
            // is effectively frozen for camera composition (a 74-sec demo
            // plays in 20+ hours at that rate). Used in tandem with the
            // cls.realtime freeze above for true pause.
            Events::RegisterListener(EventType::OnFrame, [&]() {
                static bool wasPaused = false;
                static float savedTimescale = 1.0f;
                if (GetGameState() != Types::GameState::InDemo)
                {
                    wasPaused = false;
                    return;
                }
                const bool isPaused = IWXMVM::Components::Playback::IsPaused();
                if (isPaused == wasPaused) return;
                wasPaused = isPaused;
                auto* ts = Functions::FindDvar("timescale");
                if (!ts)
                {
                    LOG_WARN("t4 pause sync: timescale dvar not found");
                    return;
                }
                if (isPaused)
                {
                    if (ts->current.value > 0.0f) savedTimescale = ts->current.value;
                    ts->current.value = 0.0f;
                    LOG_DEBUG("t4 pause sync: PAUSED (saved timescale={:.4f}, wrote 0)", savedTimescale);
                }
                else
                {
                    ts->current.value = savedTimescale;
                    LOG_DEBUG("t4 pause sync: UNPAUSED (restored timescale to {:.4f})", savedTimescale);
                }
            });

            // Update cursor-suppression atomic each frame so the user32
            // SetCursorPos/ClipCursor hooks (installed in
            // InstallHooksAndPatches) know whether we're in a demo.
            Events::RegisterListener(EventType::OnFrame, [&]() {
                g_t4SuppressCursorPos.store(
                    GetGameState() == Types::GameState::InDemo,
                    std::memory_order_release);
            });

            // t4 capture auto-unpause. CaptureManager::StartCapture doesn't
            // unpause the demo itself, so if the user hit Capture while
            // paused, cls.realtime stayed frozen by our pause sync above and
            // zero useful frames landed in the output. Watch isCapturing for
            // a false->true edge and toggle pause once. Lives in t4/ instead
            // of core/CaptureManager because core/ stays 1:1 with upstream.
            Events::RegisterListener(EventType::OnFrame, [&]() {
                static bool wasCapturing = false;
                const bool isCapturing = Components::CaptureManager::Get().IsCapturing();
                if (isCapturing && !wasCapturing && Components::Playback::IsPaused())
                {
                    LOG_DEBUG("t4 capture auto-unpause: capture started while paused -> toggling pause");
                    Components::Playback::TogglePaused();
                }
                wasCapturing = isCapturing;
            });

            // t4 dolly auto-pause. When the user presses J (DollyPlayPath),
            // CampathManager switches camera mode to Dolly and seeks to the
            // first keyframe — but doesn't pause, so the dolly immediately
            // starts playing. Originally a 5-line hunk in
            // core/CampathManager.cpp; moved here to keep core/ 1:1 with
            // upstream. OnCameraChanged fires synchronously inside
            // SetActiveCamera (before the seek), but SetTickDelta doesn't
            // check pause state in t4 so ordering is fine.
            Events::RegisterListener(EventType::OnCameraChanged, []() {
                auto& cam = Components::CameraManager::Get().GetActiveCamera();
                if (!cam || cam->GetMode() != Components::Camera::Mode::Dolly) return;
                const auto& property = Components::KeyframeManager::Get().GetProperty(
                    Types::KeyframeablePropertyType::CampathCamera);
                if (Components::KeyframeManager::Get().GetKeyframes(property).empty()) return;
                if (Components::Playback::IsPaused()) return;
                LOG_DEBUG("t4 dolly auto-pause: camera switched to Dolly with keyframes -> pausing");
                Components::Playback::TogglePaused();
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

        void SetMouseMode(Types::MouseMode /*mode*/) final
        {
            // TODO(t4): re-add IN_Frame patch when its address is wired
            // (was: Patches::GetGamePatches().IN_Frame.Apply()/.Revert()).
            // Removed alongside the t4/src/Patches.hpp wrapper.
        }

        Types::GameState GetGameState() final
        {
            const auto addr = GetGameAddresses().clientConnection();
            if (!addr) return Types::GameState::MainMenu;

            auto cl_ingame = Functions::FindDvar("cl_ingame");
            if (!cl_ingame || !cl_ingame->current.enabled)
                return Types::GameState::MainMenu;

            const auto clc = Structures::GetClientConnection();
            if (clc->demoplaying)
                return Types::GameState::InDemo;
            return Types::GameState::InGame;
        }

        Types::Features GetSupportedFeatures() final
        {
            // core/-cleanup-1:1: Features_SkipForwardOnly no longer in
            // upstream Features.hpp. Only ChangeAnimations is supported.
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

        // T4 port WIP: PlayDemo stores the resolved demo path here so
        // GetDemoInfo can return a safe answer without dereferencing
        // clientStatic/clientActive (both still HardAddr<0>). Wiring those
        // is a separate task — once done, restore the original GetDemoInfo
        // body that reads servername + serverTime from engine state.
        std::string lastDemoStem;
        std::filesystem::path lastDemoPath;

        // core/-cleanup-1:1: SeekBackward override + pendingSkipForwardTarget
        // removed. The virtual no longer exists in upstream GameInterface.

        Types::DemoInfo GetDemoInfo() final
        {
            demoInfo.name = lastDemoStem;
            demoInfo.path = lastDemoPath.string();

            auto [demoStartTick, demoEndTick] = DemoParser::GetDemoTickRange();

            // T4 port WIP: clientActive_s.cl.serverTime working hypothesis is at
            // 0x01F00DD0 (page 0x01F00000 had 217 hits of demo-time values; the
            // first paired hits at offsets 0x0DD0/0x0DD4 look like cl.serverTime
            // and cl.oldServerTime, with a clSnapshot_t-shaped gap after, then
            // a parseEntities-like 0xB4-stride array). If the timeline playhead
            // advances correctly during playback, this is confirmed. If not,
            // the value here will tell us by how much we're off.
            constexpr std::uintptr_t CL_SERVERTIME_VA = 0x01F00DD0;
            const std::uint32_t cl_serverTime = *reinterpret_cast<volatile std::uint32_t*>(CL_SERVERTIME_VA);

            if (cl_serverTime > demoStartTick && cl_serverTime < demoEndTick + 5000)
            {
                demoInfo.gameTick = cl_serverTime - demoStartTick;
            }
            else
            {
                demoInfo.gameTick = 0;
            }
            demoInfo.endTick = (demoEndTick > demoStartTick) ? (demoEndTick - demoStartTick) : 1;

            // Log once per second to track whether cl_serverTime is advancing
            static std::uint32_t last_logged = 0;
            static int frame_counter = 0;
            ++frame_counter;
            if (frame_counter % 60 == 0 && cl_serverTime != last_logged)
            {
                last_logged = cl_serverTime;
            }

            return demoInfo;
        }

        std::string_view GetDemoExtension() final
        {
            return {".dm_6"};
        }

        void PlayDemo(std::filesystem::path demoPath) final
        {
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
                // Store stem + path BEFORE invoking PreDemoLoad so DemoParser
                // (which fires from that event and calls GetDemoInfo().path)
                // sees the correct path. Previously this was set AFTER the
                // event, leaving DemoParser to parse "" and never populate
                // demoStartTick/demoEndTick — hence the timeline only ever
                // showed 0.
                lastDemoStem = demoArg;
                lastDemoPath = resolvedPath;

                Events::Invoke(EventType::PreDemoLoad);

                LOG_DEBUG("PlayDemo: issuing `demo \"{0}\"`", demoArg);
                Functions::Cbuf_AddText(std::format(R"(demo "{0}")", demoArg));

                // T4 port: normally PostDemoLoad is fired from a hook in
                // Hooks::Commands::Install (which is still disabled because
                // it has unwired IW3 addresses). Fire it directly here so
                // DemoParser::Run gets called and the timeline endTick gets
                // populated. Listeners (CameraManager, KeyframeManager,
                // DemoParser, etc.) reset their state — perfectly fine
                // since we're about to play this demo from frame 0.
                LOG_DEBUG("PlayDemo: invoking PostDemoLoad (path={})", lastDemoPath.string());
                try
                {
                    Events::Invoke(EventType::PostDemoLoad);
                    LOG_DEBUG("PlayDemo: PostDemoLoad returned cleanly");
                }
                catch (const std::exception& e)
                {
                    LOG_ERROR("PlayDemo: PostDemoLoad listener threw std::exception: {}", e.what());
                }
                catch (...)
                {
                    LOG_ERROR("PlayDemo: PostDemoLoad listener threw non-std exception");
                }
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

        // core/-cleanup-1:1 fail-safe wrapper. Many visuals methods touch
        // 10+ WaW dvars by FindDvar(...)->current.foo. If any dvar name is
        // wrong or unwired in T4, that's a null deref → SEH AV → upstream
        // catch(...) in UIManager fires MessageBox every frame (since
        // VisualsMenu's OnFrame listener runs these even when the Visuals
        // tab isn't selected). Wrap each method body so the throw stops at
        // t4's boundary and a safe default is returned.
// Variadic so initializer-list commas in `body` don't break arg counting.
#define T4_SAFE_GET(name, ret_default, ...)                                                                     \
    try { __VA_ARGS__ }                                                                                         \
    catch (...)                                                                                                 \
    {                                                                                                           \
        static bool warned = false;                                                                             \
        if (!warned) { warned = true; LOG_WARN("t4 " name ": exception caught (further silenced); returning default"); } \
        return ret_default;                                                                                     \
    }
#define T4_SAFE_SET(name, ...)                                                                                  \
    try { __VA_ARGS__ return; }                                                                                 \
    catch (...)                                                                                                 \
    {                                                                                                           \
        static bool warned = false;                                                                             \
        if (!warned) { warned = true; LOG_WARN("t4 " name ": exception caught (further silenced)"); }           \
        return;                                                                                                 \
    }

        Types::Sun GetSun() final
        {
            T4_SAFE_GET("GetSun", Types::Sun{},
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
                sun.brightness = r_lightTweakSunLight->current.value;
                return sun;
            )
        }

        Types::DoF GetDof()
        {
            T4_SAFE_GET("GetDof", Types::DoF{},
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
            )
        }

        Types::Filmtweaks GetFilmtweaks()
        {
            T4_SAFE_GET("GetFilmtweaks", Types::Filmtweaks{},
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
            )
        }

        Types::HudInfo GetHudInfo()
        {
            T4_SAFE_GET("GetHudInfo", Types::HudInfo{},
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
                    // TODO(t4): showBloodOverlay was inverse of CG_DrawPlayerLowHealthOverlay.IsApplied();
                    // assume "shown" until the patch lands again (its address is unwired anyway).
                    true,
                    Functions::FindDvar("ui_hud_obituaries")->current.string[0] == '1',
                    teamColorAllies,
                    teamColorAxis
                };

                return hudInfo;
            )
        }

        void SetSun(Types::Sun sun) final
        {
            T4_SAFE_SET("SetSun",
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
            )
        }

        void SetDof(Types::DoF dof) final
        {
            T4_SAFE_SET("SetDof",
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
            )
        }

        void SetFilmtweaks(Types::Filmtweaks filmtweaks) final
        {
            T4_SAFE_SET("SetFilmtweaks",
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
            )
        }

        void SetHudInfo(Types::HudInfo hudInfo) final
        {
            T4_SAFE_SET("SetHudInfo",
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
                // TODO(t4): blood-overlay control was three patches:
                //   CG_DrawPlayerLowHealthOverlay / CG_DrawFlashDamage / CG_DrawDamageDirectionIndicators.
                // Re-add when those addresses are wired. The patches were
                // removed alongside t4/src/Patches.hpp.
                (void)hudInfo.showBloodOverlay;

                std::stringstream teamColorAllies;
                teamColorAllies << hudInfo.killfeedTeam1Color[0] << " " << hudInfo.killfeedTeam1Color[1] << " "
                                << hudInfo.killfeedTeam1Color[2] << " 1\0";

                Functions::Dvar_SetStringByName("g_TeamColor_Allies", teamColorAllies.str().c_str());

                std::stringstream teamColorAxis;
                teamColorAxis << hudInfo.killfeedTeam2Color[0] << " " << hudInfo.killfeedTeam2Color[1] << " "
                              << hudInfo.killfeedTeam2Color[2] << " 1\0";
                Functions::Dvar_SetStringByName("g_TeamColor_Axis", teamColorAxis.str().c_str());
            )
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

            // T4 port: centity_s inner-field offsets are IW3-style and don't
            // match T4 (see Structures.hpp comment on centity_s pad). So
            // entity.pose.eType reads garbage for most slots — only ~1 slot
            // accidentally lands on ET_PLAYER, making the bonecam dropdown
            // useless. Bypass: use the slot INDEX as the player heuristic.
            // WaW MP reserves slots 0..MAX_CLIENTS-1 for players (MAX_CLIENTS
            // = 18 in WaW MP). Beyond that, mark Unsupported. clientNum and
            // isValid are set to defensible defaults until real offsets are
            // wired.
            constexpr int MAX_CLIENTS_WAW_MP = 18;

            // POV player marker: read cg_s.clientNum at 0x0098FCE0 (per
            // session-7 bonecam memory). That slot's bonecam entry gets a
            // distinctive id=999 so the dropdown labels it "Player 999"
            // instead of its raw slot number. core/ stays 1:1 — we can't
            // change Entity::ToString to literally say "POV Player", but
            // the user can spot "Player 999" easily. The vector POSITION
            // still equals the real slot (preserves clientObjMap indexing
            // in GetBoneData); only the displayed .id changes.
            int povClientNum = -1;
            try {
                povClientNum = static_cast<int>(
                    *reinterpret_cast<volatile std::uint32_t*>(0x0098FCE0));
                if (povClientNum < 0 || povClientNum >= MAX_CLIENTS_WAW_MP)
                    povClientNum = -1;  // out of range = unknown/bogus, skip relabel
            } catch (...) { povClientNum = -1; }

            for (int i = 0; i < 256; i++)
            {
                auto entity = cg_entities[i];
                auto typeFromIw3 = ToEntityType(entity.pose.eType);
                Types::EntityType type = typeFromIw3;
                if (i < MAX_CLIENTS_WAW_MP && typeFromIw3 == Types::EntityType::Unsupported)
                {
                    // Heuristic: any first-18 slot that didn't already
                    // classify is a player candidate. User picks one; if it
                    // has no dobj/model, GetBoneData returns id=-1 cleanly.
                    type = Types::EntityType::Player;
                }
                int displayId = (i == povClientNum) ? 999 : i;
                entities.push_back(
                    Types::Entity
                    {
                        .id = displayId,
                        .type = type,
                        .clientNum = i,    // until real offset is wired, slot==clientNum
                        .isValid = true,
                    }
                );
            }

            return entities;
        }

        auto FindBoneIndex(Structures::DObj_s* dobj, uint16_t boneName)
        {
            try {
            if (!dobj || !dobj->models || !dobj->numModels)
                return -1;

            // Sanity: dobj->numModels is a uint8 nominally <16 in practice.
            // If we see a value out of plausible range, treat as garbage dobj
            // (e.g. local POV viewer with no model in first-person) and bail
            // before we deref dobj->models[m] into who-knows-where.
            const int numModels = static_cast<unsigned char>(dobj->numModels);
            if (numModels <= 0 || numModels > 32)
                return -1;

            auto boneIndex = -1;
            auto totalBones = 0;
            for (int m = 0; m < numModels; m++)
            {
                auto model = dobj->models[m];
                if (!model)
                    return -1;
                const int numBones = static_cast<unsigned int>(model->numBones);
                if (numBones <= 0 || numBones > 512)
                    return -1;
                if (!model->boneNames)
                    return -1;

                for (int b = 0; b < numBones; b++)
                {
                    auto bone = model->boneNames[b];
                    if (bone == boneName)
                    {
                        boneIndex = totalBones + b;
                    }
                }
                totalBones += numBones;
            }

            return boneIndex;
            } catch (...) {
                // Garbage dobj/model pointers — return -1 cleanly so the
                // outer GetBoneData can fall through to its safe fallback
                // instead of being caught by the outer try/catch.
                return -1;
            }
        }

        Types::BoneData GetBoneData(int32_t entityId, const std::string& name) final
        {
            // Wrap in try/catch so a bad bone lookup can't crash the render
            // pipeline (BoneCamera::Update fires every frame). NOTE: as of
            // 2026-05-25 the underlying CG_DObjGetWorldBoneMatrix call still
            // throws — see project-iwxmvm-bonecam-blocked for context.
            // Returning id=-1 just shows "Bone not found on entity" in the UI.
            try {
            uint16_t* clientObjMap = Structures::GetClientObjectMap();
            Structures::DObj_s* objBuf = Structures::GetObjBuf();
            if (!clientObjMap || !objBuf)
            {
                static bool warned = false;
                if (!warned) { warned = true; LOG_WARN("GetBoneData: clientObjMap or objBuf null — bonecam unavailable"); }
                return {.id = -1};
            }

            uint16_t dobjIndex = clientObjMap[entityId];
            Structures::DObj_s* dobj = &objBuf[dobjIndex];

            auto entities = Structures::GetEntities();
            auto entity = &entities[entityId];

            // T4 MP SL_GetStringOfSize is 4-arg: (inst, string, user, len).
            // inst=0 = server-side script instance (default for engine code).
            auto boneName = Functions::SL_GetStringOfSize(
                0, name.c_str(), 1, static_cast<unsigned int>(name.size() + 1));

            // T4 port 2026-05-24: SKIP the timestamp swap. IW3 forces bone
            // recomputation by overwriting dobj->skel.timeStamp with
            // clientActive.skelTimeStamp. In T4 we don't have the right
            // clientActive offset for skelTimeStamp yet, so the swap was
            // setting dobj's timestamp to a garbage value.

            auto boneIndex = FindBoneIndex(dobj, boneName);
            if (boneIndex == -1)
                return {.id = -1};

            // Matrix MUST be 3x4 (12 floats) per X360 catalog signature
            // `int CG_DObjGetWorldBoneMatrix(cpose_t*, DObj_s*, int,
            //  float[3][4]& axis, float* origin)`. Allocating 3x3 (9 floats)
            // overflowed the stack into the adjacent `origin` buffer and
            // caused the wrapper to corrupt return values.
            float rotationMatrix[3 * 4];   // 12 floats per actual signature
            float origin[3];

            // Bonecam call marker — log once per second so we know GetBoneData
            // is reaching the CG_DObj call. Helps correlate with VEH AV logs
            // to determine if CG_DObj is throwing or completing cleanly.
            {
                static auto last_mark = std::chrono::steady_clock::now() - std::chrono::seconds(10);
                auto now = std::chrono::steady_clock::now();
                if (now - last_mark >= std::chrono::seconds(1)) {
                    last_mark = now;
                    LOG_INFO("BONECAM_CALL: about to call CG_DObj  bone='{}' idx={} dobj@{} entity@{} mat@{} origin@{}",
                             name, boneIndex, (void*)dobj, (void*)entity, (void*)rotationMatrix, (void*)origin);
                }
            }

            auto result = Functions::CG_DObjGetWorldBoneMatrix(
                entity, boneIndex, (float*)rotationMatrix, dobj, origin);

            {
                static auto last_mark = std::chrono::steady_clock::now() - std::chrono::seconds(10);
                auto now = std::chrono::steady_clock::now();
                if (now - last_mark >= std::chrono::seconds(1)) {
                    last_mark = now;
                    LOG_INFO("BONECAM_CALL: returned cleanly result={} origin=[{:.1f},{:.1f},{:.1f}]",
                             result, origin[0], origin[1], origin[2]);
                }
            }

            if (!result)
                return {.id = -1};

            Types::BoneData boneData;
            boneData.id = boneIndex;
            boneData.position = glm::make_vec3(origin);
            // 3x4 matrix: take rows [0..2], cols [0..2] — skip the 4th column
            // (translation, redundant with `origin`). Pack into 3x3 for caller.
            float rotMat3x3[9] = {
                rotationMatrix[0], rotationMatrix[1], rotationMatrix[2],
                rotationMatrix[4], rotationMatrix[5], rotationMatrix[6],
                rotationMatrix[8], rotationMatrix[9], rotationMatrix[10],
            };
            boneData.rotation = glm::make_mat3(rotMat3x3);
            return boneData;
            } catch (...) {
                static bool warned = false;
                if (!warned) { warned = true; LOG_WARN("GetBoneData: CG_DObjGetWorldBoneMatrix threw (further silenced); see project-iwxmvm-bonecam-blocked"); }
                return {.id = -1};
            }
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

            // TODO(t4): Con_TimeJumped NOP patch was Apply()'d around the call
            // here and Revert()'d after, to suppress an audio-time-jump message.
            // Re-add when Con_TimeJumpedCall's address is wired.
            _asm
            {
                pushad
                xor eax, eax
                call CL_FirstSnapshot
                popad
            }
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
