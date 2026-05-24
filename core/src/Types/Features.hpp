#pragma once

namespace IWXMVM::Types
{
    enum Features : uint32_t
    {
        Features_None = 0,

        Features_ChangeAnimations = 1 << 0,
        // When set, the binding has wired the demo seek/rewind infrastructure
        // (FS_Read hook + fsh + the Rewinding state machine's address
        // dependencies) and SetTickDelta / SkipDemoForward / RewindBy can be
        // called safely. When clear, those calls are silently dropped — the
        // alternative is null-deref of clientStatic-derived fields or a
        // permanently-stuck rewindTo atomic that freezes GameView capture.
        Features_Rewinding = 1 << 1,
        // When set, the binding supports SkipDemoForward (a simple cls.realtime
        // write — no FS_Read snapshots required) but NOT RewindBy. Lets t4
        // enable timeline scrubbing FORWARD without the full rewind machinery.
        // SkipDemoForward checks (Rewinding | SkipForwardOnly); RewindBy checks
        // Rewinding only.
        Features_SkipForwardOnly = 1 << 2,
    };
}  // namespace IWXMVM::Types