// GhostBand — live AI accompaniment for singer-songwriters.
// Copyright 2026 GhostBand contributors. Licensed under Apache-2.0.

#pragma once

#include <vector>

namespace ghostband::core {

/// Tracks which notes a chord source is currently holding, and works out the smallest set
/// of note-offs and note-ons that gets from here to the next chord.
///
/// ## Why a diff rather than "stop everything, start the new chord"
///
/// MRT2 treats every `set_note_on` as an **onset** — `docs/MRT2_API_NOTES.md` §5, and the
/// same reason `MidiHarmonyState` deliberately re-strikes a held key. That is correct for
/// a player pressing a key again. It is wrong for a chord change: G major and E minor share
/// G and B, and releasing and re-striking those two makes the band re-articulate notes that
/// a real player would simply let ring. Over a four-chord loop that is an audible tic on
/// every bar line.
///
/// So the common tones are left alone. Only what actually changes is sent.
///
/// This also keeps the note-off/note-on ordering right: stops are reported before starts,
/// so a chord change never briefly sounds both chords at once.
///
/// Pure bookkeeping over note numbers — no MRT2, no JUCE, no threads. The caller owns the
/// backend and the ordering guarantees that go with it.
class SoundingChord {
public:
    struct Change {
        /// Notes to release, in ascending order. Send these first.
        std::vector<int> toStop;
        /// Notes to strike, in ascending order.
        std::vector<int> toStart;

        bool isEmpty() const noexcept { return toStop.empty() && toStart.empty(); }
    };

    /// Move to `notes`, which need not be sorted or unique.
    ///
    /// An empty target releases everything — which is what a chord symbol that failed to
    /// parse must **not** do. See `SongMapPlayer`: a rejected slot yields no notes and the
    /// caller is expected to skip the change entirely rather than pass an empty vector
    /// here, so the band holds what it was playing instead of falling silent.
    Change moveTo(const std::vector<int>& notes);

    /// Release everything. Equivalent to `moveTo({})`, named for the intent.
    Change clear();

    const std::vector<int>& sounding() const noexcept { return sounding_; }
    bool isEmpty() const noexcept { return sounding_.empty(); }

private:
    /// Always sorted and unique, which is what makes the diff a linear merge.
    std::vector<int> sounding_;
};

} // namespace ghostband::core
