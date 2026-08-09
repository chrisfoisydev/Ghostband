// GhostBand — live AI accompaniment for singer-songwriters.
// Copyright 2026 GhostBand contributors. Licensed under Apache-2.0.

#include "ChordNamer.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <initializer_list>

namespace ghostband::core {

namespace {

constexpr std::array<const char*, 12> kNoteNames = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};

/// Semitone offsets -> bitmask. Built programmatically rather than as hand-written
/// binary literals, which are easy to mistype and impossible to read back.
std::uint16_t maskOf(std::initializer_list<int> semitones) {
    std::uint16_t m = 0;
    for (int s : semitones) m |= static_cast<std::uint16_t>(1u << s);
    return m;
}

struct Entry { std::uint16_t mask; const char* quality; };

const std::vector<Entry>& templates() {
    static const std::vector<Entry> t = {
        {maskOf({0, 4, 7, 10}), "7"},
        {maskOf({0, 4, 7, 11}), "major 7"},
        {maskOf({0, 3, 7, 10}), "minor 7"},
        {maskOf({0, 3, 6, 10}), "m7b5"},
        {maskOf({0, 3, 6, 9}),  "dim7"},
        {maskOf({0, 3, 7, 11}), "minor major 7"},
        {maskOf({0, 4, 7}),     "major"},
        {maskOf({0, 3, 7}),     "minor"},
        {maskOf({0, 3, 6}),     "dim"},
        {maskOf({0, 4, 8}),     "aug"},
        {maskOf({0, 5, 7}),     "sus4"},
        {maskOf({0, 2, 7}),     "sus2"},
        {maskOf({0, 7}),        "5"},
    };
    return t;
}

} // namespace

std::string noteName(int midiNote) {
    if (midiNote < 0 || midiNote > 127) return {};
    return kNoteNames[static_cast<std::size_t>(midiNote % 12)];
}

std::string noteNames(const std::vector<int>& midiNotes) {
    std::string out;
    for (std::size_t i = 0; i < midiNotes.size(); ++i) {
        if (i > 0) out += ' ';
        out += noteName(midiNotes[i]);
    }
    return out;
}

std::string nameChord(const std::vector<int>& midiNotes) {
    if (midiNotes.empty()) return {};

    std::uint16_t pitch_classes = 0;
    for (int n : midiNotes) {
        if (n < 0 || n > 127) continue;
        pitch_classes |= static_cast<std::uint16_t>(1u << (n % 12));
    }
    if (pitch_classes == 0) return {};

    if (midiNotes.size() == 1) return noteName(midiNotes.front());

    // The lowest sounding note. Preferred as the root when it fits, because that is the
    // chord the player believes they are holding; inversions are named by their bass only
    // when no root-position reading works.
    const int bass = *std::min_element(midiNotes.begin(), midiNotes.end()) % 12;

    const auto tryRoot = [&](int root) -> const char* {
        std::uint16_t rotated = 0;
        for (int s = 0; s < 12; ++s) {
            if (pitch_classes & (1u << s)) {
                rotated |= static_cast<std::uint16_t>(1u << (((s - root) + 12) % 12));
            }
        }
        for (const auto& t : templates()) {
            if (rotated == t.mask) return t.quality;
        }
        return nullptr;
    };

    if (const char* q = tryRoot(bass)) {
        return noteName(bass) + " " + q;
    }
    for (int root = 0; root < 12; ++root) {
        if (root == bass) continue;
        if (const char* q = tryRoot(root)) {
            // An inversion: name the chord, and say which note is in the bass, because on
            // stage "am I playing the right chord" and "is the bass right" are different
            // questions.
            return noteName(root) + " " + q + "/" + noteName(bass);
        }
    }

    // No match. Say so plainly rather than inventing a name — a confidently wrong chord
    // label is worse than none, since the performer would stop trusting the display.
    std::vector<int> sorted(midiNotes);
    std::sort(sorted.begin(), sorted.end());
    return noteNames(sorted);
}

} // namespace ghostband::core
