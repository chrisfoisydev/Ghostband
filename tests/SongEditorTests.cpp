// GhostBand — live AI accompaniment for singer-songwriters.
// Copyright 2026 GhostBand contributors. Licensed under Apache-2.0.
//
// Authoring a song. The recurring concern: an edit must never produce a song that cannot
// be performed or saved, and no single click may destroy work irrecoverably.

#include "TestMain.h"
#include "core/Persistence.h"
#include "core/SongEditor.h"

using namespace ghostband::core;

namespace {

int indexOfName(const SongEditor& e, const std::string& name) {
    for (int i = 0; i < e.sectionCount(); ++i) {
        if (e.sectionAt(i)->name == name) return i;
    }
    return -1;
}

/// An editor with no sections and a usable default prompt.
///
/// Structural tests need to know exactly what is in the song. A default-constructed
/// SongEditor starts from the demo, which already has two sections and one of them is
/// called "Chorus" — enough to make a test that names its own sections quietly test
/// something other than what it says.
SongEditor emptyEditor() {
    SongEditor e;
    e.reset(Song{});
    e.setDefaultPrompt("warm ensemble, instrumental");
    e.markSaved();
    return e;
}

/// Append a named section and return its index.
int addNamed(SongEditor& e, const std::string& name) {
    const int index = e.addSection();
    e.setSectionName(index, name);
    return index;
}

} // namespace

TEST_MAIN_BEGIN("SongEditor")

TEST("a new editor starts from a performable song") {
    // Starting empty would mean the first thing a performer sees is a validation error.
    SongEditor e;
    CHECK(e.isPerformable());
    CHECK(e.validationError().empty());
    CHECK(e.sectionCount() >= 1);
    CHECK(!e.isDirty());
    CHECK(!e.canUndo());
}

TEST("reset replaces the song and clears history") {
    SongEditor e;
    e.setTitle("Edited");
    CHECK(e.isDirty());
    CHECK(e.canUndo());

    e.reset(makeDemoSong());
    CHECK(!e.isDirty());          // opening a file is not an edit
    CHECK(!e.canUndo());
    CHECK(e.song().title == "Demo Song");
}

// --- Editing ---------------------------------------------------------------------------

TEST("song-level fields round-trip through the editor") {
    SongEditor e;
    e.setTitle("Ghost Light");
    e.setDefaultPrompt("warm organic indie folk ensemble, instrumental");
    e.setMasterLevelDb(-3.5f);
    e.setHarmonySource(HarmonySource::SongMap);

    CHECK(e.song().title == "Ghost Light");
    CHECK(e.song().defaultStylePrompt == "warm organic indie folk ensemble, instrumental");
    CHECK_NEAR(e.song().masterAiLevelDb, -3.5f, 1e-6);
    CHECK(e.song().harmonySource == HarmonySource::SongMap);
}

TEST("a no-op edit does not dirty the song or consume undo") {
    // Otherwise clicking into a field and out again would claim unsaved changes, and the
    // "are you sure?" prompt stops meaning anything.
    SongEditor e;
    const auto title = e.song().title;

    e.setTitle(title);
    CHECK(!e.isDirty());
    CHECK(!e.canUndo());

    e.setSectionIntensity(0, e.sectionAt(0)->aiIntensity);
    CHECK(!e.isDirty());
}

TEST("out-of-range section edits are refused, not clamped") {
    // Clamping would edit the wrong section. In a song with eight verses that is a
    // mistake nobody notices until the gig.
    SongEditor e;
    const auto before = e.song();

    CHECK(!e.setSectionName(-1, "x"));
    CHECK(!e.setSectionName(99, "x"));
    CHECK(!e.setSectionIntensity(99, 0.5f));
    CHECK(!e.setSectionAiEnabled(-1, false));
    CHECK(!e.setSectionTransitionMs(99, 100));
    CHECK(!e.removeSection(99));
    CHECK(!e.moveSection(0, 99));
    CHECK_EQ(e.duplicateSection(99), -1);

    CHECK(e.sectionAt(0)->name == before.sections[0].name);
    CHECK(!e.isDirty());
}

TEST("values are clamped into their valid ranges") {
    SongEditor e;
    e.setSectionIntensity(0, 7.0f);
    CHECK_NEAR(e.sectionAt(0)->aiIntensity, 1.0f, 1e-6);
    e.setSectionIntensity(0, -3.0f);
    CHECK_NEAR(e.sectionAt(0)->aiIntensity, 0.0f, 1e-6);

    e.setSectionTransitionMs(0, 999999);
    CHECK_EQ(e.sectionAt(0)->transitionMs, kMaxTransitionMs);
    e.setSectionTransitionMs(0, -5);
    CHECK_EQ(e.sectionAt(0)->transitionMs, 0);

    e.setMasterLevelDb(100.0f);
    CHECK_NEAR(e.song().masterAiLevelDb, 6.0f, 1e-6);
}

TEST("an empty section name becomes a placeholder rather than being refused") {
    // A blank name shows as nothing on the stage screen and fails validation on save.
    // Refusing the keystroke that empties the field would be worse than substituting.
    SongEditor e;
    CHECK(e.setSectionName(0, ""));
    CHECK(!e.sectionAt(0)->name.empty());
    CHECK(e.isPerformable());
}

// --- Structure ---------------------------------------------------------------------------

TEST("adding a section gives it a unique name") {
    SongEditor e = emptyEditor();
    const int a = e.addSection();
    const int b = e.addSection();

    CHECK(a >= 0);
    CHECK(b > a);
    CHECK(e.sectionAt(a)->name != e.sectionAt(b)->name);
}

TEST("duplicating lands next to the original, not at the end") {
    // Duplicating a verse to make Verse 2 means it belongs beside the verse. Appending it
    // to the end would silently rewrite the arrangement.
    SongEditor e = emptyEditor();
    addNamed(e, "Verse");
    addNamed(e, "Outro");                 // so there is something after the original
    const int copy = e.duplicateSection(0);

    CHECK_EQ(copy, 1);
    CHECK(e.sectionAt(1)->name != e.sectionAt(0)->name);   // renamed to stay unique
    CHECK(e.sectionAt(1)->stylePrompt == e.sectionAt(0)->stylePrompt);
    CHECK_NEAR(e.sectionAt(1)->aiIntensity, e.sectionAt(0)->aiIntensity, 1e-6);
}

TEST("no two sections may share a name") {
    // The stage screen shows the section name in 96pt type. Two called "Chorus" makes it
    // ambiguous at exactly the moment it must not be.
    SongEditor e = emptyEditor();
    const int first = addNamed(e, "Chorus");
    const int second = addNamed(e, "Chorus");

    CHECK(e.sectionAt(second)->name != "Chorus");
    CHECK_EQ(indexOfName(e, "Chorus"), first);
}

TEST("renaming a section to its own name is allowed") {
    // The uniqueness check must ignore the section being renamed, or re-typing the same
    // name would push it to "Verse 2".
    SongEditor e = emptyEditor();
    addNamed(e, "Verse");
    CHECK(e.setSectionName(0, "Verse"));
    CHECK(e.sectionAt(0)->name == "Verse");
}

TEST("sections can be reordered") {
    SongEditor e = emptyEditor();
    addNamed(e, "A");
    addNamed(e, "B");
    addNamed(e, "C");

    CHECK(e.moveSection(0, 2));           // A to the end
    CHECK(e.sectionAt(0)->name == "B");
    CHECK(e.sectionAt(1)->name == "C");
    CHECK(e.sectionAt(2)->name == "A");

    CHECK(e.moveSection(2, 0));           // and back
    CHECK(e.sectionAt(0)->name == "A");
}

TEST("moving a section to where it already is changes nothing") {
    SongEditor e;
    CHECK(!e.moveSection(0, 0));
    CHECK(!e.isDirty());
}

TEST("the last section cannot be removed") {
    // A song with no sections cannot be performed and Song::validate rejects it on save.
    // Refusing the click beats accepting an unsaveable state.
    SongEditor e;
    while (e.sectionCount() > 1) CHECK(e.removeSection(0));

    CHECK_EQ(e.sectionCount(), 1);
    CHECK(!e.removeSection(0));
    CHECK_EQ(e.sectionCount(), 1);
    CHECK(e.isPerformable());
}

TEST("removing a section keeps the rest in order") {
    SongEditor e = emptyEditor();
    addNamed(e, "A");
    addNamed(e, "B");
    addNamed(e, "C");

    CHECK(e.removeSection(1));
    CHECK_EQ(e.sectionCount(), 2);
    CHECK(e.sectionAt(0)->name == "A");
    CHECK(e.sectionAt(1)->name == "C");
}

// --- Undo -------------------------------------------------------------------------------

TEST("undo restores a deleted section") {
    // Deletion is the only irreversible click in the editor, and it destroys work that
    // exists nowhere else.
    SongEditor e;
    const int before = e.sectionCount();
    const auto name = e.sectionAt(0)->name;

    CHECK(e.removeSection(0));
    CHECK_EQ(e.sectionCount(), before - 1);

    CHECK(e.undo());
    CHECK_EQ(e.sectionCount(), before);
    CHECK(e.sectionAt(0)->name == name);
}

TEST("undo walks back through several edits") {
    SongEditor e;
    e.setTitle("One");
    e.setTitle("Two");
    e.setTitle("Three");

    CHECK(e.undo());
    CHECK(e.song().title == "Two");
    CHECK(e.undo());
    CHECK(e.song().title == "One");
    CHECK(e.undo());
    CHECK(e.song().title != "One");    // back past the first edit
}

TEST("undo on a fresh editor is refused rather than crashing") {
    SongEditor e;
    CHECK(!e.undo());
    CHECK(!e.canUndo());
}

TEST("undo history is bounded") {
    // Songs are small, but an unbounded stack over a long editing session is still a leak.
    SongEditor e;
    for (int i = 0; i < 200; ++i) e.setTitle("title " + std::to_string(i));

    CHECK(e.undoDepth() <= 50);
    CHECK(e.canUndo());
    CHECK(e.undo());          // still works after the oldest entries were dropped
}

TEST("every structural edit is undoable") {
    SongEditor e;
    const auto snapshot = e.song().sections.size();

    e.addSection();
    CHECK(e.undo());
    CHECK_EQ(e.song().sections.size(), snapshot);

    e.duplicateSection(0);
    CHECK(e.undo());
    CHECK_EQ(e.song().sections.size(), snapshot);

    e.moveSection(0, e.sectionCount() - 1);
    CHECK(e.undo());
    CHECK_EQ(e.song().sections.size(), snapshot);
}

// --- Dirty tracking ------------------------------------------------------------------

TEST("dirty tracks unsaved work") {
    SongEditor e;
    CHECK(!e.isDirty());

    e.setTitle("Changed");
    CHECK(e.isDirty());

    e.markSaved();
    CHECK(!e.isDirty());

    e.addSection();
    CHECK(e.isDirty());
}

TEST("undo leaves the song dirty") {
    // Undoing back to the saved state is indistinguishable from editing to it. Claiming
    // "no unsaved changes" when we are not certain is the wrong way to be wrong.
    SongEditor e;
    e.setTitle("Changed");
    e.markSaved();
    e.setTitle("Changed again");
    CHECK(e.undo());
    CHECK(e.isDirty());
}

// --- Warnings the editor exists to surface --------------------------------------------

TEST("prompt-slot overflow is visible while editing, not at save time") {
    // KNOWN_ISSUES §4: a song with more distinct prompts than MRT2 holds will stall on
    // some section change. The performer must learn that here, where rewording a prompt
    // still fixes it, rather than on stage.
    SongEditor e;
    e.reset(Song{});
    e.setDefaultPrompt("base");
    CHECK(e.fitsPromptSlots());

    for (int i = 0; i < SongEditor::promptCapacity() + 2; ++i) {
        const int index = e.addSection();
        e.setSectionPrompt(index, "distinct prompt " + std::to_string(i));
    }

    CHECK(e.distinctPromptCount() > SongEditor::promptCapacity());
    CHECK(!e.fitsPromptSlots());
    // Overflow is a warning, never a refusal — the song is still performable.
    CHECK(e.isPerformable());
}

TEST("sections sharing a prompt cost one slot, not one each") {
    // Capacity is counted in distinct prompts. Ten sections with two wordings fit.
    SongEditor e;
    e.reset(Song{});
    e.setDefaultPrompt("base");
    for (int i = 0; i < 10; ++i) {
        const int index = e.addSection();
        e.setSectionPrompt(index, (i % 2 == 0) ? "verse feel" : "chorus feel");
    }

    CHECK_EQ(e.distinctPromptCount(), 2);
    CHECK(e.fitsPromptSlots());
}

TEST("validation errors surface live") {
    SongEditor e;
    e.reset(Song{});
    e.addSection();                       // no prompt anywhere yet
    e.setDefaultPrompt("");
    e.setSectionPrompt(0, "");

    CHECK(!e.isPerformable());
    CHECK(!e.validationError().empty());

    e.setDefaultPrompt("warm ensemble, instrumental");
    CHECK(e.isPerformable());
}

// --- The editor's output must survive the file format ---------------------------------

TEST("an edited song round-trips through the file format") {
    // The two halves have to agree: anything the editor can produce must be saveable and
    // reload identically, or work is lost at the moment it is committed.
    SongEditor e = emptyEditor();
    e.setTitle("Edited = Song [odd]");
    addNamed(e, "Intro");
    e.setSectionNotes(0, "two lines\nof notes");
    e.setSectionChords(0, {"G", "D", "Em"});
    e.setSectionIntensity(0, 0.42f);
    const int extra = addNamed(e, "Outro");
    e.setSectionAiEnabled(extra, false);
    e.setSectionTransitionMs(extra, kTransitionInstantMs);
    CHECK_EQ(extra, 1);

    Song restored;
    CHECK(deserialiseSong(serialiseSong(e.song()), restored).ok);
    CHECK(restored.title == e.song().title);
    CHECK_EQ(static_cast<int>(restored.sections.size()), e.sectionCount());
    CHECK(restored.sections[0].notes == "two lines\nof notes");
    // Named rather than inline: a braced list inside CHECK() reads as extra macro
    // arguments, because the preprocessor splits on its commas.
    const std::vector<std::string> expected_chords{"G", "D", "Em"};
    CHECK(restored.sections[0].chordProgression == expected_chords);
    CHECK_NEAR(restored.sections[0].aiIntensity, 0.42f, 1e-6);
    CHECK(!restored.sections[1].aiEnabled);
}

TEST("a song built entirely from scratch is performable and saveable") {
    // The gap this whole feature closes: before it, the only song that could be created
    // was the demo.
    SongEditor e;
    e.reset(Song{});
    e.setTitle("From Nothing");
    e.setDefaultPrompt("spare acoustic ensemble, instrumental");

    const int verse = e.addSection();
    e.setSectionName(verse, "Verse");
    e.setSectionIntensity(verse, 0.3f);
    const int chorus = e.addSection();
    e.setSectionName(chorus, "Chorus");
    e.setSectionIntensity(chorus, 0.8f);

    CHECK(e.isPerformable());
    CHECK(e.fitsPromptSlots());

    Song restored;
    CHECK(deserialiseSong(serialiseSong(e.song()), restored).ok);
    CHECK(restored.title == "From Nothing");
    CHECK_EQ(static_cast<int>(restored.sections.size()), 2);
}

TEST("a section tempo can be set, and cleared as a musical choice") {
    auto e = emptyEditor();
    addNamed(e, "Verse");

    CHECK(e.setSectionTempoBpm(0, 96.0));
    CHECK(e.song().sections[0].tempoBpm.has_value());
    CHECK(*e.song().sections[0].tempoBpm == 96.0);

    // Clearing is not "missing": no tempo puts Song Map into Manual, where the chart moves
    // only when the performer says so.
    CHECK(e.setSectionTempoBpm(0, std::nullopt));
    CHECK(!e.song().sections[0].tempoBpm.has_value());
}

TEST("an out-of-range tempo is refused, not clamped") {
    // Clamping a typo'd 1200 to 300 would hand the performer a tempo they did not ask for.
    auto e = emptyEditor();
    addNamed(e, "Verse");
    e.setSectionTempoBpm(0, 120.0);

    CHECK(!e.setSectionTempoBpm(0, 1200.0));
    CHECK(!e.setSectionTempoBpm(0, 0.0));
    CHECK(!e.setSectionTempoBpm(0, -60.0));
    CHECK(*e.song().sections[0].tempoBpm == 120.0);   // untouched

    CHECK(e.setSectionTempoBpm(0, 20.0));             // the edges are allowed
    CHECK(e.setSectionTempoBpm(0, 300.0));
}

TEST("tempo on a section that does not exist is refused") {
    auto e = emptyEditor();
    CHECK(!e.setSectionTempoBpm(0, 120.0));
    CHECK(!e.setSectionTempoBpm(-1, 120.0));
}

TEST_MAIN_END()
