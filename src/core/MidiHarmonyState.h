// GhostBand — live AI accompaniment for singer-songwriters.
// Copyright 2026 GhostBand contributors. Licensed under Apache-2.0.

#pragma once

#include "GhostBandConstants.h"
#include "IGenerationBackend.h"

#include <atomic>
#include <cstdint>
#include <vector>

namespace ghostband::core {

/// Tracks which notes are sounding and forwards the transitions to MRT2.
///
/// MRT2 takes raw `set_note_on` / `set_note_off` and decides phrasing itself
/// (docs/MRT2_API_NOTES.md §6.1). What it does *not* do is model a sustain pedal, or
/// know that a MIDI cable was unplugged while three notes were held. Those are ours.
///
/// **Why a sounding/held distinction matters.** With the pedal down, releasing a key must
/// not stop the note — but the key is no longer down, so "held" and "sounding" diverge.
/// Getting this wrong on stage means either a chord that dies under the pedal or one that
/// never releases, and the second is worse: a stuck note pins the band to one harmony for
/// the rest of the song.
///
/// Threading: `noteOn`/`noteOff`/`setSustainPedal` come from the MIDI thread and use
/// relaxed atomics with no allocation. `soundingNotes()` allocates and is UI-thread only.
class MidiHarmonyState {
public:
    MidiHarmonyState() = default;

    /// The backend receives every transition. Null is legal (nothing is forwarded), which
    /// is what lets the state machine be tested without any backend at all.
    void setBackend(IGenerationBackend* backend) noexcept { backend_ = backend; }

    /// @name MIDI thread
    /// @{
    /// A note-on with velocity 0 is a note-off — the MIDI spec allows either, and
    /// controllers differ, so both are handled here rather than at every call site.
    void noteOn(int note, int velocity = 100) noexcept;
    void noteOff(int note) noexcept;
    void setSustainPedal(bool down) noexcept;

    /// Release everything, pedal included. Required on MIDI disconnect, song change and
    /// panic. Cheap enough to call defensively.
    void allNotesOff() noexcept;
    /// @}

    /// Sounding = key held, or released-but-sustained by the pedal.
    bool isSounding(int note) const noexcept;
    bool isKeyDown(int note) const noexcept;
    int soundingCount() const noexcept { return sounding_count_.load(std::memory_order_relaxed); }
    bool sustainPedalDown() const noexcept { return pedal_.load(std::memory_order_relaxed); }

    /// Ascending list of sounding notes. Allocates — UI thread only.
    std::vector<int> soundingNotes() const;

    /// Bumped on every change, so the UI can skip redrawing when nothing moved.
    std::uint32_t generation() const noexcept { return generation_.load(std::memory_order_relaxed); }

private:
    static bool inRange(int note) noexcept { return note >= 0 && note < kNumMidiNotes; }
    void updateSoundingCount() noexcept;

    IGenerationBackend* backend_ = nullptr;

    std::atomic<bool> key_down_[kNumMidiNotes] = {};
    /// Released while the pedal was down: no longer held, still sounding.
    std::atomic<bool> sustained_[kNumMidiNotes] = {};
    std::atomic<bool> pedal_{false};
    std::atomic<int> sounding_count_{0};
    std::atomic<std::uint32_t> generation_{0};
};

} // namespace ghostband::core
