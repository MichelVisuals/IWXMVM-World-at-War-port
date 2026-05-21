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
#include "Components/Playback.hpp"

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

            // T4 port: enable just the SV_Frame pause hook now that SV_Frame's
            // T4 address is wired (0x0057F7E5). Hooks::Playback::Install internally
            // skips FS_Read hook when fsh is still HardAddr<0>, so this is safe.
            Hooks::Playback::Install();

            // CG_CalcViewValues candidate hunt: install per-frame call
            // counters at 4 FP-heavy unidentified WaWMVM hook addresses.
            // The one ticking at ~60Hz during demo playback is the per-frame
            // view-calc function we need to hook for free camera.
            Hooks::Playback::InstallCandidateHunt();

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

            // T4 port hunt: two-pass scan for cls.realtime (engine ms since
            // launch). Pass 1 records all small-positive uint32s. Pass 2
            // checks 2 seconds later and reports values that advanced by
            // ~1500-2500 ms. Filters out spikes/snapshots; clean signal for
            // monotonic engine clocks. clientStatic_s.realtime is at offset
            // 0x150 per T4SP-Server-Plugin asserts, so once we have the VA we
            // derive the struct base. (Also exposes related clocks like
            // cl.snap.serverTime, cgs.time, etc.)
            Events::RegisterListener(EventType::OnFrame, [&]() {
                constexpr int BUF_SIZE = 65536;
                static RtPair* pass1_buf = nullptr;
                static int pass1_count = 0;
                static int phase = 0;  // 0=idle, 1=did pass1, 2=done
                static int wait_counter = 0;

                if (phase >= 2) return;
                if (GetGameState() != Types::GameState::InDemo) return;

                if (phase == 0)
                {
                    LOG_INFO("realtime-hunt pass 1: scanning BSS for engine-clock candidates");
                    if (!pass1_buf)
                        pass1_buf = new RtPair[BUF_SIZE];
                    pass1_count = 0;

                    std::uintptr_t addr = 0x01000000;
                    while (addr < 0x05000000)
                    {
                        MEMORY_BASIC_INFORMATION mbi{};
                        if (::VirtualQuery(reinterpret_cast<void*>(addr), &mbi, sizeof(mbi)) == 0)
                        {
                            addr += 0x1000;
                            continue;
                        }
                        const std::uintptr_t region_end = reinterpret_cast<std::uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
                        if (mbi.State == MEM_COMMIT &&
                            (mbi.Protect & (PAGE_READWRITE | PAGE_READONLY | PAGE_WRITECOPY)) != 0 &&
                            (mbi.Protect & PAGE_GUARD) == 0)
                        {
                            const std::uintptr_t scan_end = std::min(region_end, (std::uintptr_t)0x05000000);
                            RealtimeScanRegionSEH((std::uintptr_t)mbi.BaseAddress, scan_end,
                                                   pass1_buf, BUF_SIZE, pass1_count);
                        }
                        addr = region_end > addr ? region_end : addr + 0x1000;
                    }
                    LOG_INFO("realtime-hunt pass 1: captured {} candidates; sleeping ~2s for pass 2",
                             pass1_count);
                    phase = 1;
                    wait_counter = 0;
                }
                else if (phase == 1)
                {
                    ++wait_counter;
                    if (wait_counter < 600) return;  // ~5s @ 120fps, ~10s @ 60fps

                    LOG_INFO("realtime-hunt pass 2: checking advancement after {} frames", wait_counter);
                    int reported = 0;
                    // First, count by delta bucket to see the distribution
                    int bucket_negative = 0, bucket_0_100 = 0, bucket_100_1000 = 0,
                        bucket_1000_5000 = 0, bucket_5000_20000 = 0, bucket_huge = 0;
                    for (int i = 0; i < pass1_count; ++i)
                    {
                        std::uint32_t new_val = 0;
                        if (RealtimeReadSEH(pass1_buf[i].va, new_val) != 0) continue;
                        if (new_val < pass1_buf[i].value) { ++bucket_negative; continue; }
                        const std::uint32_t delta = new_val - pass1_buf[i].value;
                        if      (delta < 100)    ++bucket_0_100;
                        else if (delta < 1000)   ++bucket_100_1000;
                        else if (delta < 5000)   ++bucket_1000_5000;
                        else if (delta < 20000)  ++bucket_5000_20000;
                        else                     ++bucket_huge;

                        // Report candidates whose delta is in 1000-20000 range
                        // (typical engine ms clock advancement for our wait period
                        // depending on fps and timescale).
                        if (delta >= 1000 && delta <= 20000 && reported < 80)
                        {
                            LOG_INFO("  cls.realtime cand @ 0x{:08X}: {} -> {} (delta={}, cls_base candidate=0x{:08X})",
                                     pass1_buf[i].va, pass1_buf[i].value, new_val,
                                     delta, pass1_buf[i].va - 0x150);
                            ++reported;
                        }
                    }
                    LOG_INFO("realtime-hunt deltas: <0:{}, 0-100:{}, 100-1K:{}, 1K-5K:{}, 5K-20K:{}, >20K:{}",
                             bucket_negative, bucket_0_100, bucket_100_1000,
                             bucket_1000_5000, bucket_5000_20000, bucket_huge);
                    LOG_INFO("realtime-hunt: reported {} candidates", reported);
                    delete[] pass1_buf;
                    pass1_buf = nullptr;
                    pass1_count = 0;
                    phase = 2;
                }
            });

            // CG_CalcViewValues candidate hunt: every 120 frames (~2 sec),
            // log the call counters from the 4 hooked candidates. The one
            // with the highest per-2-sec delta is per-frame called = our
            // CG_CalcViewValues target.
            Events::RegisterListener(EventType::OnFrame, [&]() {
                static int frame_ct = 0;
                static std::uint32_t last[7] = {};
                if (++frame_ct % 120 != 0) return;
                const std::uint32_t now_a = Hooks::Playback::g_candA_hits;
                const std::uint32_t now_b = Hooks::Playback::g_candB_hits;
                const std::uint32_t now_c = Hooks::Playback::g_candC_hits;
                const std::uint32_t now_d = Hooks::Playback::g_candD_hits;
                const std::uint32_t now_e = Hooks::Playback::g_candE_hits;
                const std::uint32_t now_f = Hooks::Playback::g_candF_hits;
                const std::uint32_t now_g = Hooks::Playback::g_candG_hits;
                LOG_INFO("cand-hunt: A(0x430670)+{} D(0x70CA98)+{} E(0x43E3B0)+{} F(0x446440)+{} G(0x6B6CE0)+{} (B/C unchanged)",
                         now_a - last[0], now_d - last[3],
                         now_e - last[4], now_f - last[5], now_g - last[6]);
                last[0] = now_a; last[1] = now_b; last[2] = now_c; last[3] = now_d;
                last[4] = now_e; last[5] = now_f; last[6] = now_g;
            });

            // T4 port: pause via timescale=0. Engine clamps to 0.001x which
            // is effectively frozen for camera composition (a 74-sec demo
            // plays in 20+ hours at that rate). Not TRUE freeze — true pause
            // would need SV_Frame hook (T4 addr not yet located; needs
            // leaked-source or Ghidra session). Earlier NOP-write attempts
            // accelerated the demo because the addresses we found were
            // "previous realtime" buffers — see git log e71155d..bf8ac48.
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
                if (!ts) return;
                if (isPaused)
                {
                    if (ts->current.value > 0.0f) savedTimescale = ts->current.value;
                    ts->current.value = 0.0f;
                }
                else
                {
                    ts->current.value = savedTimescale;
                }
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

        struct RtPair { std::uint32_t va; std::uint32_t value; };

        // SEH-only POD helper for pass-1 scan (no C++ objects so __try works).
        // Caller pre-allocates buffer + passes capacity.
        static int RealtimeScanRegionSEH(std::uintptr_t region_base, std::uintptr_t region_end,
                                         RtPair* buf, int buf_size, int& cursor)
        {
            __try
            {
                for (std::uintptr_t va = region_base; va + 4 <= region_end && cursor < buf_size; va += 4)
                {
                    const std::uint32_t v = *reinterpret_cast<const std::uint32_t*>(va);
                    if (v >= 5000 && v <= 10'000'000)
                    {
                        buf[cursor].va = (std::uint32_t)va;
                        buf[cursor].value = v;
                        ++cursor;
                    }
                }
                return 0;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return 1;
            }
        }

        // SEH-only POD helper for pass-2 single-value read.
        static int RealtimeReadSEH(std::uintptr_t va, std::uint32_t& out)
        {
            __try { out = *reinterpret_cast<const std::uint32_t*>(va); return 0; }
            __except (EXCEPTION_EXECUTE_HANDLER) { return 1; }
        }

        // One-shot live-process diagnostic: hunt clientActive_s base via
        // VirtualQuery walk + multi-field fingerprint. Runs SEH-guarded so a
        // bad read past a committed-region edge can't crash the game. Results
        // (candidate bases + matching field values) go to IWXMVM.log.
        struct CaScanRawResult
        {
            std::uint32_t base_va;
            std::uint32_t cst;       // value at offset 0x20F8
            std::uint32_t ost;       // value at offset 0x20FC
            std::uint32_t ofst;      // value at offset 0x2100
            std::int32_t  std_delta; // value at offset 0x2104
            std::uint32_t ossnap;    // value at offset 0x2108
            std::uint32_t es;        // value at offset 0x210C
            std::uint32_t newSnap;   // value at offset 0x2110
        };

        static int ScanCaRegionSEH(const std::uint8_t* region_base, size_t region_size,
                                   CaScanRawResult* out_hits, int max_hits,
                                   std::uint32_t demoStart, std::uint32_t demoEnd)
        {
            int n_hits = 0;
            __try
            {
                // Need enough headroom for the deepest read (offset 0x2110).
                if (region_size < 0x2200) return 0;
                size_t end = region_size - 0x2200;
                for (size_t off = 0; off <= end && n_hits < max_hits; off += 4)
                {
                    const std::uint8_t* p = region_base + off;
                    // Relaxed fingerprint — focus on the time fields which are
                    // best-anchored to clientActive_s offsets per T4SP-Server-Plugin
                    // asserts. snap.valid/snapFlags are weak indicators because
                    // T4 MP may use different values or our snap offset might be
                    // wrong.
                    //   p[0x20F4] (alwaysFalse): must be 0
                    // Drop alwaysFalse check (offset 0x20F4 might not be alwaysFalse
                    // in T4 MP — T4SP-Server-Plugin asserts may not apply). Keep the
                    // hard-to-fake constraints: cl.serverTime is plausible; serverTimeDelta
                    // is small SIGNED int (kills pointer-table noise); time fields form
                    // a small cluster around cl.serverTime; newSnapshots is 0..3.
                    std::uint32_t cst = *(const std::uint32_t*)(p + 0x20F8);
                    // Strong filter: cst MUST be within the demo's tick range
                    // (it's the current playhead position in server-time units).
                    if (cst < demoStart || cst > demoEnd + 5000) continue;
                    int std_delta = (int)*(const std::uint32_t*)(p + 0x2104);
                    if (std_delta < -100000 || std_delta > 100000) continue;
                    std::uint32_t ost = *(const std::uint32_t*)(p + 0x20FC);
                    if (ost == 0) continue;
                    int dos = (int)ost - (int)cst;
                    if (dos < -1000 || dos > 1000) continue;
                    std::uint32_t ofst = *(const std::uint32_t*)(p + 0x2100);
                    if (ofst == 0) continue;
                    int dofst = (int)ofst - (int)cst;
                    if (dofst < -1000 || dofst > 1000) continue;
                    std::uint32_t ossnap = *(const std::uint32_t*)(p + 0x2108);
                    if (ossnap == 0) continue;
                    int dosnap = (int)ossnap - (int)cst;
                    if (dosnap < -2000 || dosnap > 2000) continue;
                    std::uint32_t newSnap = *(const std::uint32_t*)(p + 0x2110);
                    if (newSnap > 3) continue;
                    std::uint32_t es = *(const std::uint32_t*)(p + 0x210C);
                    out_hits[n_hits].base_va = (std::uint32_t)(uintptr_t)p;
                    out_hits[n_hits].cst = cst;
                    out_hits[n_hits].ost = ost;
                    out_hits[n_hits].ofst = ofst;
                    out_hits[n_hits].std_delta = std_delta;
                    out_hits[n_hits].ossnap = ossnap;
                    out_hits[n_hits].es = es;
                    out_hits[n_hits].newSnap = newSnap;
                    ++n_hits;
                }
                return n_hits;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return n_hits;
            }
        }

        // Simple value-scan: find every 32-bit value in BSS that's in [lo, hi].
        // Bucket by 64KB page. The page with the most hits likely contains
        // serverTime + related fields = clientActive_s neighborhood.
        struct ValueHit
        {
            std::uint32_t va;
            std::uint32_t value;
        };

        static int ScanForServerTimeValuesSEH(const std::uint8_t* region_base, size_t region_size,
                                              std::uint32_t lo, std::uint32_t hi,
                                              ValueHit* out_hits, int max_hits)
        {
            int n = 0;
            __try
            {
                size_t end = region_size < 4 ? 0 : region_size - 4;
                for (size_t off = 0; off <= end && n < max_hits; off += 4)
                {
                    std::uint32_t v = *(const std::uint32_t*)(region_base + off);
                    if (v >= lo && v <= hi)
                    {
                        out_hits[n].va = (std::uint32_t)(uintptr_t)(region_base + off);
                        out_hits[n].value = v;
                        ++n;
                    }
                }
                return n;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return n;
            }
        }

        static void HuntClientActiveOnce()
        {
            // Get demo bounds from DemoParser — these were computed in PostDemoLoad.
            // cl.serverTime MUST be within [demoStart, demoEnd] during playback.
            auto [demoStart, demoEnd] = DemoParser::GetDemoTickRange();
            if (demoStart == 0 || demoEnd == 0 || demoEnd <= demoStart)
            {
                LOG_WARN("clientActive hunt: DemoParser hasn't determined bounds yet (start={}, end={}); skipping",
                         demoStart, demoEnd);
                return;
            }
            // Widen by ~10 seconds in case serverTime is slightly outside parsed bounds
            const std::uint32_t lo = demoStart > 10000 ? demoStart - 10000 : 0;
            const std::uint32_t hi = demoEnd + 10000;
            LOG_INFO("clientActive hunt: searching BSS for any uint32 in [{}..{}] (demo bounds widened)",
                     lo, hi);

            // Value-only scan
            constexpr int MAX_VALUE_HITS = 2048;
            static ValueHit value_hits[MAX_VALUE_HITS];
            int total_value_hits = 0;
            {
                std::uintptr_t addr = 0x01000000;
                constexpr std::uintptr_t SCAN_HI = 0x05000000;
                while (addr < SCAN_HI && total_value_hits < MAX_VALUE_HITS)
                {
                    MEMORY_BASIC_INFORMATION mbi{};
                    if (::VirtualQuery(reinterpret_cast<void*>(addr), &mbi, sizeof(mbi)) == 0)
                    {
                        addr += 0x1000;
                        continue;
                    }
                    std::uintptr_t region_end = reinterpret_cast<std::uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
                    if (mbi.State == MEM_COMMIT &&
                        (mbi.Protect & (PAGE_READWRITE | PAGE_READONLY | PAGE_WRITECOPY)) != 0 &&
                        (mbi.Protect & PAGE_GUARD) == 0)
                    {
                        std::uintptr_t scan_start = std::max(addr, reinterpret_cast<std::uintptr_t>(mbi.BaseAddress));
                        std::uintptr_t scan_end = std::min(region_end, SCAN_HI);
                        if (scan_end > scan_start)
                        {
                            int got = ScanForServerTimeValuesSEH(
                                reinterpret_cast<const std::uint8_t*>(scan_start),
                                scan_end - scan_start, lo, hi,
                                value_hits + total_value_hits, MAX_VALUE_HITS - total_value_hits);
                            total_value_hits += got;
                        }
                    }
                    addr = region_end > addr ? region_end : addr + 0x1000;
                }
            }
            LOG_INFO("clientActive hunt: found {} uint32 values in demo range", total_value_hits);

            // Cluster by 64KB page
            std::map<std::uint32_t, int> page_counts;
            for (int i = 0; i < total_value_hits; ++i)
                page_counts[value_hits[i].va & ~0xFFFF]++;
            std::vector<std::pair<std::uint32_t, int>> ranked(page_counts.begin(), page_counts.end());
            std::sort(ranked.begin(), ranked.end(),
                      [](const auto& a, const auto& b) { return a.second > b.second; });
            LOG_INFO("clientActive hunt: top pages by demo-time-value density:");
            for (int i = 0; i < (int)ranked.size() && i < 10; ++i)
            {
                LOG_INFO("  page=0x{:08X}  hits={}", ranked[i].first, ranked[i].second);
            }
            // Print all hits within the TOP page (probable clientActive_s neighborhood).
            // Show offset within page so we can identify cl.serverTime's field offset.
            if (!ranked.empty())
            {
                std::uint32_t top_page = ranked[0].first;
                LOG_INFO("clientActive hunt: ALL hits in top page 0x{:08X} (offset within page -> value):", top_page);
                int printed = 0;
                for (int i = 0; i < total_value_hits && printed < 250; ++i)
                {
                    if ((value_hits[i].va & ~0xFFFF) != top_page) continue;
                    std::uint32_t offset = value_hits[i].va & 0xFFFF;
                    LOG_INFO("  +0x{:04X} (0x{:08X}) = {}", offset, value_hits[i].va, value_hits[i].value);
                    ++printed;
                }
                // Also dump the second page just in case it's part of the same struct
                if (ranked.size() > 1)
                {
                    std::uint32_t p2 = ranked[1].first;
                    LOG_INFO("clientActive hunt: hits in 2nd-best page 0x{:08X}:", p2);
                    printed = 0;
                    for (int i = 0; i < total_value_hits && printed < 100; ++i)
                    {
                        if ((value_hits[i].va & ~0xFFFF) != p2) continue;
                        std::uint32_t offset = value_hits[i].va & 0xFFFF;
                        LOG_INFO("  +0x{:04X} (0x{:08X}) = {}", offset, value_hits[i].va, value_hits[i].value);
                        ++printed;
                    }
                }
            }
            return;

            constexpr int MAX_HITS = 32;
            CaScanRawResult hits[MAX_HITS] = {};
            int total = 0;
            constexpr std::uintptr_t SCAN_LO = 0x01000000;
            constexpr std::uintptr_t SCAN_HI = 0x05000000;
            std::uintptr_t addr = SCAN_LO;
            int regions_scanned = 0;
            int regions_skipped = 0;
            while (addr < SCAN_HI && total < MAX_HITS)
            {
                MEMORY_BASIC_INFORMATION mbi{};
                if (::VirtualQuery(reinterpret_cast<void*>(addr), &mbi, sizeof(mbi)) == 0)
                {
                    addr += 0x1000;
                    continue;
                }
                std::uintptr_t region_end = reinterpret_cast<std::uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
                if (mbi.State == MEM_COMMIT &&
                    (mbi.Protect & (PAGE_READWRITE | PAGE_READONLY | PAGE_WRITECOPY)) != 0 &&
                    (mbi.Protect & PAGE_GUARD) == 0)
                {
                    std::uintptr_t scan_start = std::max(addr, (std::uintptr_t)reinterpret_cast<std::uintptr_t>(mbi.BaseAddress));
                    std::uintptr_t scan_end = std::min(region_end, SCAN_HI);
                    if (scan_end > scan_start)
                    {
                        int got = ScanCaRegionSEH(
                            reinterpret_cast<const std::uint8_t*>(scan_start),
                            scan_end - scan_start,
                            hits + total, MAX_HITS - total,
                            demoStart, demoEnd);
                        total += got;
                        ++regions_scanned;
                    }
                }
                else
                {
                    ++regions_skipped;
                }
                addr = region_end > addr ? region_end : addr + 0x1000;
            }
            LOG_INFO("clientActive hunt: scanned {} regions, skipped {}, found {} candidates",
                     regions_scanned, regions_skipped, total);
            for (int i = 0; i < total; ++i)
            {
                const auto& h = hits[i];
                LOG_INFO("  candidate[{}] base=0x{:08X}  cst={}  ost={}  ofst={}  delta={}  ossnap={}  es={}  newSnap={}",
                         i, h.base_va, h.cst, h.ost, h.ofst, h.std_delta, h.ossnap, h.es, h.newSnap);
            }
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
                LOG_DEBUG("cl.serverTime probe: [0x{:08X}] = {} (gameTick={}, endTick={})",
                          CL_SERVERTIME_VA, cl_serverTime, demoInfo.gameTick, demoInfo.endTick);
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
