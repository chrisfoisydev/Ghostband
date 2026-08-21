// GhostBand — live AI accompaniment for singer-songwriters.
// Copyright 2026 GhostBand contributors. Licensed under Apache-2.0.

#pragma once

#include <optional>
#include <string>
#include <vector>

namespace ghostband::core {

/// A chord symbol the performer typed, resolved into something playable.
///
/// This is the inverse of `ChordNamer`, and the difference in consequence between the two
/// is the whole reason this file is careful. `nameChord` is **display only** — a wrong
/// label is a wrong caption. `parseChord` feeds MRT2, so a wrong parse is a **wrong chord
/// under the singer**, in front of an audience, with no way to tell it was a typo rather
/// than the band being wrong.
///
/// So: this parser refuses anything it does not fully understand. There is no "closest
/// match", no ignoring of trailing characters, no assuming major when a quality is
/// unrecognised. `"Gxyz"` returns nothing rather than G major.
struct Chord {
    /// 0 = C, 1 = C#, … 11 = B.
    int rootPitchClass = 0;
    /// Semitone offsets above the root, always starting with 0. E.g. major = {0, 4, 7}.
    std::vector<int> intervals;
    /// Pitch class of a slash bass ("D/F#"), if one was written and differs from the root.
    std::optional<int> bassPitchClass;
    /// The symbol as typed, kept for display so the performer sees their own spelling.
    std::string symbol;
};

/// Parse a chord symbol. Returns nothing for anything not fully understood.
///
/// Accepted, deliberately bounded:
/// - roots `A`–`G` with optional `#`/`b` (and Unicode-free `s` is *not* accepted — one
///   spelling per accidental keeps the round trip through the song file exact)
/// - qualities: major (empty, `maj`, `M`), minor (`m`, `min`, `-`), `dim`/`°`-free `dim`,
///   `aug`/`+`, `sus2`, `sus4`/`sus`, `6`, `m6`, `7`, `maj7`/`M7`, `m7`, `m7b5`, `dim7`,
///   `9`, `maj9`, `m9`, `add9`
/// - an optional slash bass: `/` followed by a note name
///
/// Anything else — a quality this list does not contain, trailing junk, an empty string —
/// returns `std::nullopt`. That refusal is the feature.
std::optional<Chord> parseChord(const std::string& symbol);

/// True when `parseChord` would succeed. For validating a progression as it is typed.
bool isParsableChord(const std::string& symbol);

/// Which entries of a progression cannot be parsed, by index.
///
/// The Song Editor needs to show *which* chord is wrong, not merely that one is — a
/// progression of eight symbols with a typo in the sixth is not usefully described by
/// "invalid progression".
std::vector<int> unparsableChordIndices(const std::vector<std::string>& progression);

/// MIDI note numbers for a chord, voiced for MRT2.
///
/// `octave` places the root: 4 gives a root at middle C (60) for a C chord. Voicing is
/// deliberately plain — root position, close, plus the bass note an octave down when a
/// slash bass was written. MRT2 receives note numbers and does its own arranging; a clever
/// voicing here would be this app second-guessing the model.
///
/// Returns an empty vector for a chord with no intervals, which `parseChord` cannot
/// produce — the check is for callers constructing a Chord by hand.
std::vector<int> chordNotes(const Chord& chord, int octave = 4);

/// Convenience: symbol straight to notes. Empty for anything unparsable.
std::vector<int> chordSymbolNotes(const std::string& symbol, int octave = 4);

} // namespace ghostband::core
