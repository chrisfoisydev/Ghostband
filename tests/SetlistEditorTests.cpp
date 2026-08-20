// GhostBand — live AI accompaniment for singer-songwriters.
// Copyright 2026 GhostBand contributors. Licensed under Apache-2.0.
//
// Building a set. The recurring concerns: reordering must never lose or duplicate a song,
// the ends must refuse rather than wrap, and no single click may destroy work
// irrecoverably. The wrap rule matters as much here as in SetlistController — a song that
// jumps from the closer to the opener because someone clicked once too often is the exact
// surprise that class exists to prevent.

#include "TestMain.h"
#include "core/Persistence.h"
#include "core/SetlistEditor.h"

#include <utility>

using namespace ghostband::core;

namespace {

/// A set of `count` songs named "Song 1".."Song N", in the state of one just opened.
///
/// Constructed from a `Setlist` rather than built with `addSong`: building it through the
/// mutators leaves an undo entry per song, so a test asserting "this refused edit cost no
/// undo" would pass or fail on the fixture's history rather than on the edit. `markSaved()`
/// is not enough — it clears the dirty flag, and deliberately not the stack.
SetlistEditor setOf(int count) {
    Setlist list;
    for (int i = 1; i <= count; ++i) {
        SetlistEntry entry;
        entry.songFile = "song-" + std::to_string(i) + ".ghostsong";
        entry.cachedTitle = "Song " + std::to_string(i);
        list.entries.push_back(std::move(entry));
    }
    return SetlistEditor{std::move(list)};
}

/// Running order as titles, so a reorder assertion reads like the set does.
std::string order(const SetlistEditor& e) {
    std::string out;
    for (int i = 0; i < e.size(); ++i) {
        if (!out.empty()) out += " ";
        out += e.entryAt(i)->cachedTitle;
    }
    return out;
}

} // namespace

TEST_MAIN_BEGIN("SetlistEditor")

TEST("a new editor is empty and not performable") {
    // Unlike SongEditor, which starts from the demo song: there is no sensible default set,
    // and inventing one would put songs in front of a performer that they did not choose.
    SetlistEditor e;
    CHECK(e.isEmpty());
    CHECK(!e.isPerformable());
    CHECK(!e.isDirty());
    CHECK(!e.canUndo());
}

TEST("reset replaces the set and clears history") {
    auto e = setOf(2);
    e.setName("Edited");
    CHECK(e.isDirty());
    CHECK(e.canUndo());

    e.reset(Setlist{});
    CHECK(!e.isDirty());          // opening a file is not an edit
    CHECK(!e.canUndo());
    CHECK(e.isEmpty());
}

// --- Adding ----------------------------------------------------------------------------

TEST("adding appends in order and marks dirty") {
    auto e = setOf(3);
    CHECK(e.size() == 3);
    CHECK(order(e) == "Song 1 Song 2 Song 3");
    CHECK(e.isPerformable());
}

TEST("an entry with no song file is refused") {
    // Setlist::validate rejects these, so creating one would build a set that cannot be
    // saved — better to refuse than to accept an unsaveable state.
    SetlistEditor e;
    CHECK(e.addSong("", "Ghost Light") == -1);
    CHECK(e.isEmpty());
    CHECK(!e.isDirty());
}

TEST("a blank title falls back to the file name") {
    // The cached title exists so a missing song still shows something actionable. Blank
    // would defeat that at exactly the moment it matters.
    SetlistEditor e;
    e.addSong("ghost-light.ghostsong", "");
    CHECK(e.entryAt(0)->cachedTitle == "ghost-light.ghostsong");
}

TEST("the same song may appear twice") {
    // Encores and reprises are real. SongEditor enforces unique section names because
    // prompt-slot allocation requires it; nothing here requires unique songs.
    SetlistEditor e;
    e.addSong("ghost-light.ghostsong", "Ghost Light");
    e.addSong("ghost-light.ghostsong", "Ghost Light");
    CHECK(e.size() == 2);
    CHECK(e.isPerformable());
}

TEST("insert places the entry at the requested index") {
    auto e = setOf(3);
    CHECK(e.insertSong(1, "new.ghostsong", "New") == 1);
    CHECK(order(e) == "Song 1 New Song 2 Song 3");
}

TEST("insert at size appends, past size is refused") {
    auto e = setOf(2);
    CHECK(e.insertSong(2, "a.ghostsong", "A") == 2);   // one past the last is "append"
    CHECK(e.insertSong(9, "b.ghostsong", "B") == -1);
    CHECK(e.insertSong(-1, "c.ghostsong", "C") == -1);
    CHECK(e.size() == 3);
}

// --- Removing --------------------------------------------------------------------------

TEST("removing takes out the right entry") {
    auto e = setOf(4);
    CHECK(e.removeEntry(1));
    CHECK(order(e) == "Song 1 Song 3 Song 4");
}

TEST("an out-of-range remove is refused rather than clamped") {
    // A clamp would delete the first or last song instead of nothing, and in a twelve-song
    // set nobody notices until the gig.
    auto e = setOf(3);
    CHECK(!e.removeEntry(-1));
    CHECK(!e.removeEntry(3));
    CHECK(e.size() == 3);
    CHECK(!e.isDirty());          // a refused edit is not an edit
}

TEST("removing the last entry is allowed, unlike removing the last section") {
    // Clearing a set out and starting again is a normal step in building one. A song with
    // no sections is never a state anyone wants, which is why SongEditor refuses there.
    auto e = setOf(1);
    CHECK(e.removeEntry(0));
    CHECK(e.isEmpty());
    CHECK(!e.isPerformable());    // ...but it says so, rather than pretending
}

// --- Reordering ------------------------------------------------------------------------

TEST("moving an entry lands it at the requested index") {
    auto e = setOf(4);
    CHECK(e.moveEntry(0, 2));
    CHECK(order(e) == "Song 2 Song 3 Song 1 Song 4");
}

TEST("moving backwards works as well as forwards") {
    auto e = setOf(4);
    CHECK(e.moveEntry(3, 0));
    CHECK(order(e) == "Song 4 Song 1 Song 2 Song 3");
}

TEST("a move never loses or duplicates a song") {
    // The failure mode of an erase/insert reorder is an off-by-one that drops one entry and
    // clones another. Checking the whole running order catches that; checking size alone
    // would not.
    auto e = setOf(5);
    CHECK(e.moveEntry(1, 3));
    CHECK(e.size() == 5);
    CHECK(order(e) == "Song 1 Song 3 Song 4 Song 2 Song 5");
}

TEST("moving onto itself is refused and costs no undo") {
    auto e = setOf(3);
    CHECK(!e.moveEntry(1, 1));
    CHECK(!e.canUndo());
    CHECK(order(e) == "Song 1 Song 2 Song 3");
}

TEST("up and down refuse at the ends rather than wrapping") {
    auto e = setOf(3);
    CHECK(!e.moveUp(0));
    CHECK(!e.moveDown(2));
    CHECK(order(e) == "Song 1 Song 2 Song 3");

    CHECK(e.moveDown(0));
    CHECK(order(e) == "Song 2 Song 1 Song 3");
    CHECK(e.moveUp(1));
    CHECK(order(e) == "Song 1 Song 2 Song 3");
}

// --- Naming ----------------------------------------------------------------------------

TEST("an emptied name falls back rather than being refused") {
    auto e = setOf(1);
    e.setName("Friday Night");
    CHECK(e.setlist().name == "Friday Night");

    e.setName("");
    CHECK(e.setlist().name == "Untitled Setlist");
}

TEST("setting the same name again is not an edit") {
    auto e = setOf(1);
    e.setName("Friday Night");
    const int depth = e.undoDepth();
    e.setName("Friday Night");
    CHECK(e.undoDepth() == depth);
}

// --- Undo ------------------------------------------------------------------------------

TEST("undo restores a removed song") {
    // Removing is the one click that destroys work. Everything else can be typed again.
    auto e = setOf(3);
    e.removeEntry(0);
    CHECK(order(e) == "Song 2 Song 3");

    CHECK(e.undo());
    CHECK(order(e) == "Song 1 Song 2 Song 3");
}

TEST("undo restores a reorder") {
    auto e = setOf(3);
    e.moveEntry(0, 2);
    CHECK(e.undo());
    CHECK(order(e) == "Song 1 Song 2 Song 3");
}

TEST("undo stops at the bottom instead of misbehaving") {
    auto e = setOf(2);
    e.removeEntry(0);
    CHECK(e.undo());
    CHECK(!e.undo());
    CHECK(e.size() == 2);
}

TEST("undo history is bounded") {
    auto e = setOf(1);
    for (int i = 0; i < 200; ++i) e.setName("Name " + std::to_string(i));
    CHECK(e.undoDepth() <= 50);
}

TEST("undo leaves the set dirty") {
    // Undoing back to the saved state is indistinguishable from editing to it, and
    // claiming "no unsaved changes" when we are not certain is the wrong way to be wrong.
    auto e = setOf(2);
    e.removeEntry(0);
    e.undo();
    CHECK(e.isDirty());
}

// --- Persistence round trip --------------------------------------------------------------

TEST("an edited set survives a save and reload") {
    // The Song Editor lost a rename between the model and disk (KNOWN_ISSUES.md §25).
    // That was app-layer wiring, but the round trip belongs in core too, so the format
    // half of it can never be the unknown again.
    auto e = setOf(3);
    e.setName("Friday Night");
    e.moveEntry(2, 0);
    e.removeEntry(2);

    Setlist back;
    const auto result = deserialiseSetlist(serialiseSetlist(e.setlist()), back);
    CHECK(result.ok);

    SetlistEditor reopened{back};
    CHECK(reopened.setlist().name == "Friday Night");
    CHECK(order(reopened) == order(e));
}

TEST_MAIN_END()
