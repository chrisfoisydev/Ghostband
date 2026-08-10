// GhostBand — live AI accompaniment for singer-songwriters.
// Copyright 2026 GhostBand contributors. Licensed under Apache-2.0.

#pragma once

#include "Song.h"

#include <atomic>
#include <cstddef>

namespace ghostband::core {

/// Tracks which section is playing and ramps the transition between them.
///
/// **Nothing here advances on its own.** There is no clock, no bar count, no song
/// position. The performer moves sections and only the performer moves sections — the
/// brief (§30.5) requires that repeating a chorus, extending a bridge, or ending early all
/// stay possible, and any form of automatic advance quietly removes that. A section is an
/// arrangement instruction, not a timeline entry.
///
/// The transition ramp is driven by `tick()` from a control-thread timer, not from the
/// audio thread. Blend-weight changes only affect the *next generated frame* (40 ms
/// granularity), so sample-accurate ramping would be false precision.
///
/// Threading: control thread. `currentIndex()` is atomic so a UI or diagnostics poll from
/// another thread reads a coherent value.
class SectionController {
public:
    SectionController() = default;

    /// The song must outlive the controller. Resets to the first section.
    void setSong(const Song* song) noexcept;
    const Song* song() const noexcept { return song_; }

    int sectionCount() const noexcept;
    int currentIndex() const noexcept { return current_.load(std::memory_order_relaxed); }
    /// Index the transition is coming *from*, or the current index when settled.
    int previousIndex() const noexcept { return previous_.load(std::memory_order_relaxed); }

    const SongSection* current() const noexcept;
    /// What Performance Mode shows as "Next". Null at the last section — the set does not
    /// wrap, because a surprise return to the top mid-song is worse than showing nothing.
    const SongSection* peekNext() const noexcept;
    const SongSection* sectionAt(int index) const noexcept;

    /// @name Navigation — all return false if they cannot move
    /// @{
    bool goToNext() noexcept;
    bool goToPrevious() noexcept;
    bool goTo(int index) noexcept;

    /// Re-enter the current section, restarting its transition. This is how a repeated
    /// chorus is expressed: not a special "repeat" mode, just landing on the same section
    /// again, so no other code has to know repeats exist.
    bool retriggerCurrent() noexcept;
    /// @}

    bool isFirstSection() const noexcept { return currentIndex() == 0; }
    bool isLastSection() const noexcept;

    /// Advance the transition ramp. Call from a control-thread timer, ~100 Hz.
    void tick(double deltaMs) noexcept;

    /// 0 at the moment of the change, 1 once the transition has finished.
    float transitionProgress() const noexcept {
        return progress_.load(std::memory_order_relaxed);
    }
    bool isTransitioning() const noexcept { return transitionProgress() < 1.0f; }

    /// Jump straight to the settled state. For song load and PANIC recovery, where a
    /// crossfade from a section that was never heard would be meaningless.
    void snapTransition() noexcept;

    /// Monotonic count of section changes, for diagnostics and tests.
    std::uint64_t changeCount() const noexcept {
        return changes_.load(std::memory_order_relaxed);
    }

private:
    bool moveTo(int index, bool allowSame) noexcept;

    const Song* song_ = nullptr;
    std::atomic<int> current_{0};
    std::atomic<int> previous_{0};
    std::atomic<float> progress_{1.0f};
    std::atomic<std::uint64_t> changes_{0};

    double transition_elapsed_ms_ = 0.0;
    double transition_total_ms_ = 0.0;
};

} // namespace ghostband::core
