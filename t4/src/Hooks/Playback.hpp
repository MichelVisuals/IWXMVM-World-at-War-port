#pragma once

namespace IWXMVM::T4::Hooks::Playback
{
    void SkipForward(std::int32_t ticks);
    void RewindBy(std::int32_t ticks);
    void Install();

    void Reset();

    extern std::atomic<std::int32_t> rewindTo;
    extern volatile std::uint32_t g_sv_frame_hits;
    extern volatile std::uint32_t g_sv_frame_last_msec;

    // CG_CalcViewValues candidate hunt counters
    extern volatile std::uint32_t g_candA_hits;
    extern volatile std::uint32_t g_candB_hits;
    extern volatile std::uint32_t g_candC_hits;
    extern volatile std::uint32_t g_candD_hits;
    extern volatile std::uint32_t g_candE_hits;
    extern volatile std::uint32_t g_candF_hits;
    extern volatile std::uint32_t g_candG_hits;
    void InstallCandidateHunt();
}