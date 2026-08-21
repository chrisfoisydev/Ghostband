// GhostBand — live AI accompaniment for singer-songwriters.
// Copyright 2026 GhostBand contributors. Licensed under Apache-2.0.

#include "core/ChordParser.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <utility>

namespace ghostband::core {

namespace {

/// Letter -> pitch class. A=9 because pitch class 0 is C.
constexpr std::array<int, 7> kLetterPitchClass{9, 11, 0, 2, 4, 5, 7};   // A B C D E F G

/// Read a note name at `pos`: a letter A-G plus optional # or b. Advances `pos`.
/// Returns nothing if there is no note name there.
std::optional<int> readNote(const std::string& s, std::size_t& pos) {
    if (pos >= s.size()) return std::nullopt;

    const char letter = static_cast<char>(std::toupper(static_cast<unsigned char>(s[pos])));
    if (letter < 'A' || letter > 'G') return std::nullopt;

    int pitch_class = kLetterPitchClass[static_cast<std::size_t>(letter - 'A')];
    ++pos;

    // One accidental only. "C##" is not accepted: it is far more likely to be a typo than
    // a deliberate double sharp in a singer-songwriter's chord chart, and guessing which
    // is exactly what this parser refuses to do.
    if (pos < s.size()) {
        if (s[pos] == '#') {
            pitch_class = (pitch_class + 1) % 12;
            ++pos;
        } else if (s[pos] == 'b' || s[pos] == 'B') {
            // Lower-case b only when it *follows* a letter, which it always does here —
            // a bare "B" at this position was already consumed as the letter above.
            pitch_class = (pitch_class + 11) % 12;
            ++pos;
        }
    }
    return pitch_class;
}

/// Quality suffixes, longest first so that "m7b5" is not eaten by "m7", and "maj7" not by
/// "maj". Order is load-bearing: a shorter prefix matching first would silently produce a
/// different chord, which is the failure mode this whole file exists to prevent.
const std::vector<std::pair<std::string, std::vector<int>>>& qualityTable() {
    static const std::vector<std::pair<std::string, std::vector<int>>> table = {
        {"maj9",  {0, 4, 7, 11, 14}},
        {"m7b5",  {0, 3, 6, 10}},
        {"dim7",  {0, 3, 6, 9}},
        {"add9",  {0, 4, 7, 14}},
        {"maj7",  {0, 4, 7, 11}},
        {"sus2",  {0, 2, 7}},
        {"sus4",  {0, 5, 7}},
        {"min7",  {0, 3, 10}},
        {"dim",   {0, 3, 6}},
        {"aug",   {0, 4, 8}},
        {"maj",   {0, 4, 7}},
        {"min",   {0, 3, 7}},
        {"sus",   {0, 5, 7}},          // bare "sus" is sus4 by convention
        {"M7",    {0, 4, 7, 11}},
        {"m9",    {0, 3, 7, 10, 14}},
        {"m7",    {0, 3, 7, 10}},
        {"m6",    {0, 3, 7, 9}},
        {"11",    {0, 4, 7, 10, 14, 17}},
        {"13",    {0, 4, 7, 10, 14, 21}},
        {"m",     {0, 3, 7}},
        {"M",     {0, 4, 7}},
        {"-",     {0, 3, 7}},
        {"+",     {0, 4, 8}},
        {"9",     {0, 4, 7, 10, 14}},
        {"7",     {0, 4, 7, 10}},
        {"6",     {0, 4, 7, 9}},
        {"",      {0, 4, 7}},          // bare root = major. Must be last.
    };
    return table;
}

std::string trim(const std::string& s) {
    const auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

} // namespace

std::optional<Chord> parseChord(const std::string& symbol) {
    const std::string text = trim(symbol);
    if (text.empty()) return std::nullopt;

    std::size_t pos = 0;
    const auto root = readNote(text, pos);
    if (!root.has_value()) return std::nullopt;

    // Split off a slash bass before matching the quality, so "D/F#" tries to match the
    // quality against "" rather than against "/F#".
    std::string quality_text = text.substr(pos);
    std::optional<int> bass;

    if (const auto slash = quality_text.find('/'); slash != std::string::npos) {
        std::string bass_text = quality_text.substr(slash + 1);
        quality_text = quality_text.substr(0, slash);

        std::size_t bass_pos = 0;
        bass = readNote(bass_text, bass_pos);
        // A slash with no valid note after it, or trailing junk, is a typo. Refuse.
        if (!bass.has_value() || bass_pos != bass_text.size()) return std::nullopt;
    }

    for (const auto& [suffix, intervals] : qualityTable()) {
        if (quality_text != suffix) continue;

        Chord chord;
        chord.rootPitchClass = *root;
        chord.intervals = intervals;
        chord.symbol = text;
        // Only record a bass that actually changes the voicing. "C/C" is not an inversion.
        if (bass.has_value() && *bass != *root) chord.bassPitchClass = bass;
        return chord;
    }

    // No quality matched. Note that this compares the *whole* remainder, so trailing junk
    // fails here rather than being ignored — "Gxyz" is refused, not read as G.
    return std::nullopt;
}

bool isParsableChord(const std::string& symbol) {
    return parseChord(symbol).has_value();
}

std::vector<int> unparsableChordIndices(const std::vector<std::string>& progression) {
    std::vector<int> bad;
    for (int i = 0; i < static_cast<int>(progression.size()); ++i) {
        if (!isParsableChord(progression[static_cast<std::size_t>(i)])) bad.push_back(i);
    }
    return bad;
}

std::vector<int> chordNotes(const Chord& chord, int octave) {
    if (chord.intervals.empty()) return {};

    // MIDI note 0 is C-1, so octave 4 puts C at 60 — the usual convention, and the one
    // the on-screen keyboard already labels.
    const int root = (octave + 1) * 12 + chord.rootPitchClass;

    std::vector<int> notes;
    notes.reserve(chord.intervals.size() + 1);

    if (chord.bassPitchClass.has_value()) {
        // The bass goes below the root, in the octave beneath it. Written inversions are
        // about what the bass player does, and putting it above the chord would invert the
        // meaning of the symbol.
        int bass = (octave) * 12 + *chord.bassPitchClass;
        while (bass >= root) bass -= 12;
        if (bass >= 0) notes.push_back(bass);
    }

    for (int interval : chord.intervals) {
        const int note = root + interval;
        if (note >= 0 && note <= 127) notes.push_back(note);
    }

    std::sort(notes.begin(), notes.end());
    notes.erase(std::unique(notes.begin(), notes.end()), notes.end());
    return notes;
}

std::vector<int> chordSymbolNotes(const std::string& symbol, int octave) {
    const auto chord = parseChord(symbol);
    if (!chord.has_value()) return {};
    return chordNotes(*chord, octave);
}

} // namespace ghostband::core
