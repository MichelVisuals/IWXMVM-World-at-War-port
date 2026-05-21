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
}