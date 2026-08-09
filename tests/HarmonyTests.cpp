// GhostBand — live AI accompaniment for singer-songwriters.
// Copyright 2026 GhostBand contributors. Licensed under Apache-2.0.
//
// Phase 1 harmony: note state (what MRT2 actually receives) and chord naming
// (what the performer reads). The first must be exactly right; the second must
// never lie.

#include "TestMain.h"
#include "backend/NullBackend.h"
#include "core/ChordNamer.h"
#include "core/MidiHarmonyState.h"

using namespace ghostband::core;
using ghostband::backend::NullBackend;

TEST_MAIN_BEGIN("Harmony")

// ---------------------------------------------------------------------------
// MidiHarmonyState
// ---------------------------------------------------------------------------

TEST("notes reach the backend and are released") {
    NullBackend backend;
    MidiHarmonyState harmony;
    harmony.setBackend(&backend);

    harmony.noteOn(60);
    harmony.noteOn(64);
    harmony.noteOn(67);
    CHECK_EQ(backend.heldNoteCount(), 3);
    CHECK_EQ(harmony.soundingCount(), 3);
    CHECK(backend.isNoteHeld(64));

    harmony.noteOff(64);
    CHECK(!backend.isNoteHeld(64));
    CHECK_EQ(harmony.soundingCount(), 2);
}

TEST("note-on with velocity 0 is a note-off") {
    // The MIDI spec allows either form and controllers genuinely differ; a keyboard that
    // uses velocity-0 would otherwise leave every note stuck on.
    NullBackend backend;
    MidiHarmonyState harmony;
    harmony.setBackend(&backend);

    harmony.noteOn(60, 100);
    CHECK(harmony.isSounding(60));
    harmony.noteOn(60, 0);
    CHECK(!harmony.isSounding(60));
    CHECK(!backend.isNoteHeld(60));
}

TEST("sustain pedal holds notes after the key is released") {
    NullBackend backend;
    MidiHarmonyState harmony;
    harmony.setBackend(&backend);

    harmony.setSustainPedal(true);
    harmony.noteOn(60);
    harmony.noteOff(60);

    // Key up, but still sounding — and crucially MRT2 has NOT been told to release it.
    CHECK(!harmony.isKeyDown(60));
    CHECK(harmony.isSounding(60));
    CHECK(backend.isNoteHeld(60));
    CHECK_EQ(harmony.soundingCount(), 1);

    harmony.setSustainPedal(false);
    CHECK(!harmony.isSounding(60));
    CHECK(!backend.isNoteHeld(60));
    CHECK_EQ(harmony.soundingCount(), 0);
}

TEST("a key still held when the pedal lifts keeps sounding") {
    NullBackend backend;
    MidiHarmonyState harmony;
    harmony.setBackend(&backend);

    harmony.noteOn(60);
    harmony.setSustainPedal(true);
    harmony.setSustainPedal(false);

    CHECK(harmony.isSounding(60));
    CHECK(backend.isNoteHeld(60));
}

TEST("re-striking a sustained key sends a fresh onset") {
    // MRT2 treats every set_note_on as an onset. Suppressing the repeat because the note
    // was already sounding would silently swallow a deliberate re-articulation.
    NullBackend backend;
    MidiHarmonyState harmony;
    harmony.setBackend(&backend);

    harmony.setSustainPedal(true);
    harmony.noteOn(60);
    harmony.noteOff(60);
    CHECK(harmony.isSounding(60));

    const auto before = harmony.generation();
    harmony.noteOn(60);
    CHECK(harmony.generation() > before);
    CHECK(harmony.isKeyDown(60));
    CHECK(backend.isNoteHeld(60));
}

TEST("allNotesOff releases everything including the pedal") {
    // Required on MIDI disconnect. A stranded pedal would silently re-sustain the next
    // note played, and a stuck note pins the band to one harmony for the rest of the song.
    NullBackend backend;
    MidiHarmonyState harmony;
    harmony.setBackend(&backend);

    harmony.setSustainPedal(true);
    harmony.noteOn(60);
    harmony.noteOn(64);
    harmony.noteOff(60);
    CHECK_EQ(harmony.soundingCount(), 2);

    harmony.allNotesOff();

    CHECK_EQ(harmony.soundingCount(), 0);
    CHECK_EQ(backend.heldNoteCount(), 0);
    CHECK(!harmony.sustainPedalDown());
}

TEST("out-of-range notes are ignored") {
    NullBackend backend;
    MidiHarmonyState harmony;
    harmony.setBackend(&backend);

    harmony.noteOn(-1);
    harmony.noteOn(128);
    harmony.noteOn(9999);
    harmony.noteOff(-7);
    CHECK_EQ(harmony.soundingCount(), 0);
    CHECK_EQ(backend.heldNoteCount(), 0);
}

TEST("works with no backend attached") {
    // The state machine has to be usable before a model is loaded, and testable with no
    // backend at all.
    MidiHarmonyState harmony;
    harmony.noteOn(60);
    CHECK(harmony.isSounding(60));
    harmony.allNotesOff();
    CHECK_EQ(harmony.soundingCount(), 0);
}

TEST("soundingNotes is ascending and matches the count") {
    MidiHarmonyState harmony;
    harmony.noteOn(67);
    harmony.noteOn(55);
    harmony.noteOn(59);

    const auto notes = harmony.soundingNotes();
    CHECK_EQ(notes.size(), 3u);
    CHECK_EQ(notes[0], 55);
    CHECK_EQ(notes[1], 59);
    CHECK_EQ(notes[2], 67);
    CHECK_EQ(harmony.soundingCount(), 3);
}

// ---------------------------------------------------------------------------
// ChordNamer — display only; MRT2 never sees these strings
// ---------------------------------------------------------------------------

TEST("names common triads") {
    CHECK(nameChord({55, 59, 62}) == "G major");   // the brief's own example
    CHECK(nameChord({60, 64, 67}) == "C major");
    CHECK(nameChord({64, 67, 71}) == "E minor");
    CHECK(nameChord({60, 63, 66}) == "C dim");
    CHECK(nameChord({60, 64, 68}) == "C aug");
    CHECK(nameChord({60, 65, 67}) == "C sus4");
    CHECK(nameChord({60, 62, 67}) == "C sus2");
    CHECK(nameChord({60, 67}) == "C 5");
}

TEST("sevenths win over the triad they contain") {
    // Otherwise every dominant chord would read as a plain major.
    CHECK(nameChord({60, 64, 67, 70}) == "C 7");
    CHECK(nameChord({60, 64, 67, 71}) == "C major 7");
    CHECK(nameChord({60, 63, 67, 70}) == "C minor 7");
    CHECK(nameChord({60, 63, 66, 70}) == "C m7b5");
}

TEST("inversions name the chord and the bass") {
    // "Am I playing the right chord" and "is the bass right" are different questions.
    CHECK(nameChord({64, 67, 72}) == "C major/E");
    CHECK(nameChord({67, 72, 76}) == "C major/G");
}

TEST("octave doubling does not change the name") {
    CHECK(nameChord({48, 60, 64, 67, 72}) == "C major");
}

TEST("a single note is named as a note") {
    CHECK(nameChord({60}) == "C");
    CHECK(nameChord({61}) == "C#");
}

TEST("nothing sounding names nothing") {
    CHECK(nameChord({}).empty());
}

TEST("an unrecognised set lists the notes instead of guessing") {
    // A confidently wrong chord label is worse than none — the performer would stop
    // trusting the display, which makes it useless exactly when it matters.
    const auto name = nameChord({60, 61, 62, 63});
    CHECK(name == "C C# D D#");
}

TEST("note names cover the octave") {
    CHECK(noteName(60) == "C");
    CHECK(noteName(69) == "A");
    CHECK(noteName(71) == "B");
    CHECK(noteName(72) == "C");
    CHECK(noteNames({55, 59, 62}) == "G B D");  // the brief's harmony display example
}

TEST_MAIN_END()
