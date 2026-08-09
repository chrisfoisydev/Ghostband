// GhostBand — live AI accompaniment for singer-songwriters.
// Copyright 2026 GhostBand contributors. Licensed under Apache-2.0.

#pragma once

#include <string>
#include <vector>

namespace ghostband::core {

/// Names a set of sounding MIDI notes, e.g. {55, 59, 62} -> "G major".
///
/// **Display only.** MRT2 is steered by the raw note numbers, never by these labels —
/// the brief is explicit about that, and it matters: a mislabelled chord would otherwise
/// become a *wrong* chord rather than merely a wrong caption. This function exists so the
/// performer can confirm at a glance that the harmony being sent is the one they played.
///
/// Returns an empty string when nothing is sounding, and a bare note-name list when the
/// set matches no known chord — never a guess dressed up as a chord.
std::string nameChord(const std::vector<int>& midiNotes);

/// Note name for a MIDI number, e.g. 60 -> "C". Sharps only; no key context to choose
/// flats from, and inventing one would be worse than being consistent.
std::string noteName(int midiNote);

/// Space-separated note names, in the order given, e.g. "G B D".
std::string noteNames(const std::vector<int>& midiNotes);

} // namespace ghostband::core
