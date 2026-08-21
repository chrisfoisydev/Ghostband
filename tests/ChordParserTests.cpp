// GhostBand — live AI accompaniment for singer-songwriters.
// Copyright 2026 GhostBand contributors. Licensed under Apache-2.0.
//
// Chord symbols to note numbers. The rule underneath all of these: a wrong parse is a
// wrong chord under the singer, not a wrong caption — so anything not fully understood is
// refused rather than approximated.

#include "TestMain.h"
#include "core/ChordParser.h"

using namespace ghostband::core;

namespace {

std::vector<int> notesOf(const std::string& symbol, int octave = 4) {
    return chordSymbolNotes(symbol, octave);
}

bool refuses(const std::string& symbol) { return !parseChord(symbol).has_value(); }

} // namespace

TEST_MAIN_BEGIN("ChordParser")

TEST("bare letters are major triads") {
    CHECK(notesOf("C") == std::vector<int>({60, 64, 67}));
    CHECK(notesOf("G") == std::vector<int>({67, 71, 74}));
    CHECK(notesOf("F") == std::vector<int>({65, 69, 72}));
}

TEST("accidentals move the root by a semitone") {
    CHECK(notesOf("C#") == std::vector<int>({61, 65, 68}));
    CHECK(notesOf("Db") == std::vector<int>({61, 65, 68}));   // enharmonic, same notes
    CHECK(notesOf("Bb") == std::vector<int>({70, 74, 77}));
}

TEST("every root sits in one octave, by pitch class") {
    // A deliberate choice, and worth stating because the alternative is defensible.
    //
    // Roots are placed absolutely: octave 4 means the root falls in 60..71, whatever the
    // letter. So a chart reading "C  Bb" moves *up* a major seventh rather than down a
    // tone. The alternative - choosing each octave to minimise movement from the previous
    // chord - is real voice leading, and it is what a bass player would do.
    //
    // Not done, for two reasons. It makes a chord's notes depend on the chord before it,
    // so the same symbol in the same section can produce different notes depending on
    // where the performer restarted; and MRT2 arranges what it is given rather than
    // playing it literally, so the gain is speculative while the loss of predictability
    // is certain. If the band's register turns out to matter musically, this is the place
    // to revisit, and `chordNotes` takes an octave precisely so a caller can drive it.
    for (const char* symbol : {"C", "Db", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"}) {
        const auto notes = notesOf(symbol, 4);
        CHECK(!notes.empty());
        // No slash bass in this list, so the lowest note is the root.
        const int root = notes.front();
        CHECK(root >= 60);
        CHECK(root <= 71);
    }
}

TEST("minor, seventh and the qualities a songwriter actually types") {
    CHECK(notesOf("Am") == std::vector<int>({69, 72, 76}));
    CHECK(notesOf("Amin") == notesOf("Am"));
    CHECK(notesOf("A-") == notesOf("Am"));
    CHECK(notesOf("G7") == std::vector<int>({67, 71, 74, 77}));
    CHECK(notesOf("Cmaj7") == std::vector<int>({60, 64, 67, 71}));
    CHECK(notesOf("CM7") == notesOf("Cmaj7"));
    CHECK(notesOf("Am7") == std::vector<int>({69, 72, 76, 79}));
    CHECK(notesOf("Dsus4") == std::vector<int>({62, 67, 69}));
    CHECK(notesOf("Dsus") == notesOf("Dsus4"));      // bare sus is sus4 by convention
    CHECK(notesOf("Dsus2") == std::vector<int>({62, 64, 69}));
    CHECK(notesOf("C6") == std::vector<int>({60, 64, 67, 69}));
    CHECK(notesOf("Bdim") == std::vector<int>({71, 74, 77}));
    CHECK(notesOf("Caug") == std::vector<int>({60, 64, 68}));
    CHECK(notesOf("C+") == notesOf("Caug"));
}

TEST("longer suffixes win over their own prefixes") {
    // The quality table is ordered longest-first. If "m7" matched before "m7b5", the
    // half-diminished chord would silently become a minor seventh - a different chord,
    // played under a singer, with nothing on screen to say so.
    CHECK(notesOf("Bm7b5") == std::vector<int>({71, 74, 77, 81}));
    CHECK(notesOf("Bm7") == std::vector<int>({71, 74, 78, 81}));
    CHECK(notesOf("Bm7b5") != notesOf("Bm7"));

    CHECK(notesOf("Cmaj7") != notesOf("C7"));
    CHECK(notesOf("Cmaj9") != notesOf("C9"));
    CHECK(notesOf("Cdim7") != notesOf("Cdim"));
}

TEST("slash chords put the written bass underneath") {
    const auto notes = notesOf("D/F#");
    CHECK(!notes.empty());
    // F# below the D root, not above it: a written inversion is about what the bass does.
    CHECK(notes.front() == 54);            // F#3
    CHECK(notes == std::vector<int>({54, 62, 66, 69}));

    // A slash bass equal to the root is not an inversion and adds nothing.
    CHECK(notesOf("C/C") == notesOf("C"));
}

TEST("an unrecognised quality is refused, never approximated") {
    // The single most important behaviour in this file. "Gxyz" as G major would be a
    // typo silently becoming a chord.
    CHECK(refuses("Gxyz"));
    CHECK(refuses("Gmaj77"));
    CHECK(refuses("Cminor"));      // spelled out - not in the table, so not guessed at
    CHECK(refuses("C major"));
    CHECK(refuses("H"));           // German B - a real notation, but not one we accept
    CHECK(refuses(""));
    CHECK(refuses("   "));
    CHECK(refuses("7"));           // quality with no root
    CHECK(refuses("#"));
}

TEST("a broken slash bass fails the whole symbol") {
    CHECK(refuses("D/"));
    CHECK(refuses("D/H"));
    CHECK(refuses("D/F#x"));
    CHECK(refuses("D/F#/A"));
}

TEST("double accidentals are refused rather than guessed") {
    // Far more likely a typo than a deliberate double sharp in a chord chart.
    CHECK(refuses("C##"));
    CHECK(refuses("Cbb"));
}

TEST("surrounding whitespace is tolerated") {
    // Typed into a text field, so leading and trailing spaces are a certainty. The symbol
    // is trimmed, but nothing *inside* it is ignored.
    CHECK(notesOf("  G  ") == notesOf("G"));
    CHECK(notesOf("\tAm7\n") == notesOf("Am7"));
}

TEST("the typed spelling is preserved for display") {
    const auto chord = parseChord("  Bb maj7 ");
    // "Bb maj7" has an interior space, which is not a quality we accept.
    CHECK(!chord.has_value());

    const auto good = parseChord("  Bbmaj7 ");
    CHECK(good.has_value());
    CHECK(good->symbol == "Bbmaj7");   // trimmed, but the performer's own spelling
}

TEST("octave places the root where asked") {
    CHECK(notesOf("C", 3) == std::vector<int>({48, 52, 55}));
    CHECK(notesOf("C", 4) == std::vector<int>({60, 64, 67}));
    CHECK(notesOf("C", 5) == std::vector<int>({72, 76, 79}));
}

TEST("notes never leave the MIDI range") {
    for (int octave = -2; octave <= 9; ++octave) {
        for (const char* symbol : {"C", "Bmaj9", "F#13", "Ebm7b5"}) {
            for (int note : notesOf(symbol, octave)) {
                CHECK(note >= 0);
                CHECK(note <= 127);
            }
        }
    }
}

TEST("a progression reports which entries are wrong, not merely that one is") {
    const std::vector<std::string> progression = {"G", "D", "Em7", "Cxyz", "C", "Gwrong"};
    const auto bad = unparsableChordIndices(progression);
    CHECK(bad == std::vector<int>({3, 5}));

    CHECK(unparsableChordIndices({"G", "D", "Am", "C"}).empty());
    CHECK(unparsableChordIndices({}).empty());
}

TEST("a chord built by hand with no intervals yields nothing") {
    Chord empty;
    empty.intervals.clear();
    CHECK(chordNotes(empty).empty());
}

TEST_MAIN_END()
