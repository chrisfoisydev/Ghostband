// GhostBand — live AI accompaniment for singer-songwriters.
// Copyright 2026 GhostBand contributors. Licensed under Apache-2.0.
//
// Setlist navigation. The rule underneath most of these: the performer moves the set and
// only the performer moves the set, and a set that is partly missing is still playable
// provided the gaps are visible.

#include "TestMain.h"
#include "core/Setlist.h"

using namespace ghostband::core;

namespace {

Song namedSong(const std::string& title) {
    Song s = makeDemoSong();
    s.title = title;
    return s;
}

Setlist threeSongSet() {
    Setlist l;
    l.name = "Friday, The Blue Room";
    l.entries = {{"a.ghostsong", "Ghost Light"},
                 {"b.ghostsong", "Paper Boats"},
                 {"c.ghostsong", "The Long Way"}};
    return l;
}

std::vector<std::optional<Song>> allPresent() {
    return {namedSong("Ghost Light"), namedSong("Paper Boats"), namedSong("The Long Way")};
}

} // namespace

TEST_MAIN_BEGIN("Setlist")

TEST("a fresh controller holds nothing") {
    SetlistController c;
    CHECK(!c.isLoaded());
    CHECK_EQ(c.size(), 0);
    CHECK(c.currentSong() == nullptr);
    CHECK(c.currentEntry() == nullptr);
    CHECK(c.peekNext() == nullptr);
    CHECK(!c.goToNext());
    CHECK(!c.goToPrevious());
}

TEST("loading starts at the first song") {
    SetlistController c;
    CHECK(c.load(threeSongSet(), allPresent()));

    CHECK(c.isLoaded());
    CHECK_EQ(c.size(), 3);
    CHECK_EQ(c.currentIndex(), 0);
    CHECK(c.isFirstSong());
    CHECK(!c.isLastSong());
    CHECK(c.currentSong() != nullptr);
    CHECK(c.currentSong()->title == "Ghost Light");
}

TEST("a mismatched song count is refused") {
    // A programming error, not bad input — silently padding would put the wrong song
    // under the wrong entry for the rest of the set.
    SetlistController c;
    CHECK(!c.load(threeSongSet(), {namedSong("only one")}));
    CHECK(!c.isLoaded());
}

TEST("next and previous walk the set") {
    SetlistController c;
    c.load(threeSongSet(), allPresent());

    CHECK(c.goToNext());
    CHECK_EQ(c.currentIndex(), 1);
    CHECK(c.currentSong()->title == "Paper Boats");

    CHECK(c.goToNext());
    CHECK_EQ(c.currentIndex(), 2);
    CHECK(c.isLastSong());

    CHECK(c.goToPrevious());
    CHECK_EQ(c.currentIndex(), 1);
}

TEST("the set does not wrap at either end") {
    // Same rule as SectionController: a surprise return to the top of the set mid-show is
    // worse than a press that does nothing.
    SetlistController c;
    c.load(threeSongSet(), allPresent());

    CHECK(!c.goToPrevious());
    CHECK_EQ(c.currentIndex(), 0);

    c.goTo(2);
    CHECK(!c.goToNext());
    CHECK_EQ(c.currentIndex(), 2);
}

TEST("goTo refuses out-of-range and no-op moves") {
    SetlistController c;
    c.load(threeSongSet(), allPresent());

    CHECK(!c.goTo(-1));
    CHECK(!c.goTo(3));
    CHECK(!c.goTo(0));           // already there
    CHECK_EQ(c.currentIndex(), 0);
    CHECK(c.goTo(2));
    CHECK_EQ(c.currentIndex(), 2);
}

TEST("peekNext shows the next song and nothing at the end") {
    SetlistController c;
    c.load(threeSongSet(), allPresent());

    CHECK(c.peekNext() != nullptr);
    CHECK(c.peekNext()->cachedTitle == "Paper Boats");

    c.goTo(2);
    CHECK(c.peekNext() == nullptr);
}

TEST("a one-song set is a valid set that cannot move") {
    SetlistController c;
    Setlist l;
    l.entries = {{"only.ghostsong", "Only Song"}};
    c.load(l, {namedSong("Only Song")});

    CHECK(c.isFirstSong());
    CHECK(c.isLastSong());
    CHECK(!c.goToNext());
    CHECK(!c.goToPrevious());
    CHECK(c.peekNext() == nullptr);
}

// --- Missing songs --------------------------------------------------------------------

TEST("a missing song leaves a visible gap rather than closing up") {
    // Dropping the entry would renumber the set. The performer's running order — and the
    // paper one taped to the monitor — would stop agreeing with the screen.
    SetlistController c;
    c.load(threeSongSet(), {namedSong("Ghost Light"), std::nullopt,
                            namedSong("The Long Way")});

    CHECK_EQ(c.size(), 3);
    CHECK_EQ(c.missingCount(), 1);
    CHECK(c.hasMissingSongs());

    CHECK(c.goToNext());
    CHECK_EQ(c.currentIndex(), 1);
    CHECK(c.currentSong() == nullptr);           // the gap is reachable and reports itself
    CHECK(c.currentEntry() != nullptr);
    CHECK(c.currentEntry()->cachedTitle == "Paper Boats");

    CHECK(c.goToNext());
    CHECK(c.currentSong() != nullptr);
    CHECK(c.currentSong()->title == "The Long Way");
}

TEST("missing songs are named, not counted") {
    SetlistController c;
    c.load(threeSongSet(), {std::nullopt, namedSong("Paper Boats"), std::nullopt});

    const auto missing = c.missingTitles();
    CHECK_EQ(static_cast<int>(missing.size()), 2);
    CHECK(missing[0] == "Ghost Light");
    CHECK(missing[1] == "The Long Way");
}

TEST("a missing song with no cached title falls back to its file name") {
    // Still better than nothing: the performer can go and find the file.
    Setlist l;
    l.entries = {{"verse-2-final.ghostsong", ""}};
    SetlistController c;
    c.load(l, {std::nullopt});

    const auto missing = c.missingTitles();
    CHECK_EQ(static_cast<int>(missing.size()), 1);
    CHECK(missing[0] == "verse-2-final.ghostsong");
}

TEST("a set where every song is missing still loads and reports it") {
    // Wrong songs folder, most likely. Refusing to load would hide which songs were
    // expected, which is exactly what the performer needs to see.
    SetlistController c;
    CHECK(c.load(threeSongSet(), {std::nullopt, std::nullopt, std::nullopt}));
    CHECK(c.isLoaded());
    CHECK_EQ(c.missingCount(), 3);
    CHECK(c.currentSong() == nullptr);
    CHECK(c.currentEntry() != nullptr);
}

TEST("song pointers stay valid across navigation") {
    // SectionController holds the Song* it is given, so these must not dangle when the
    // set moves. Load is the only thing that may invalidate them.
    SetlistController c;
    c.load(threeSongSet(), allPresent());

    const Song* first = c.currentSong();
    c.goToNext();
    c.goToNext();
    c.goTo(0);
    CHECK(c.currentSong() == first);
    CHECK(c.currentSong()->title == "Ghost Light");
}

TEST("clear returns the controller to empty") {
    SetlistController c;
    c.load(threeSongSet(), allPresent());
    c.goToNext();
    c.clear();

    CHECK(!c.isLoaded());
    CHECK_EQ(c.size(), 0);
    CHECK_EQ(c.currentIndex(), 0);
    CHECK(c.currentSong() == nullptr);
    CHECK_EQ(c.missingCount(), 0);
}

TEST("loading a second set replaces the first entirely") {
    SetlistController c;
    c.load(threeSongSet(), allPresent());
    c.goTo(2);

    Setlist other;
    other.name = "Saturday";
    other.entries = {{"x.ghostsong", "New Song"}};
    CHECK(c.load(other, {namedSong("New Song")}));

    CHECK_EQ(c.size(), 1);
    CHECK_EQ(c.currentIndex(), 0);       // not left pointing past the end of the new set
    CHECK(c.setlist().name == "Saturday");
    CHECK(c.currentSong()->title == "New Song");
}

// --- Validation -------------------------------------------------------------------------

TEST("a setlist needs at least one song") {
    Setlist l;
    CHECK(!l.isValid());
    CHECK(!l.validate().empty());

    l.entries = {{"a.ghostsong", "A"}};
    CHECK(l.isValid());
}

TEST("an entry with no file is invalid and says which one") {
    Setlist l = threeSongSet();
    l.entries[1].songFile.clear();

    CHECK(!l.isValid());
    CHECK(l.validate().find('2') != std::string::npos);   // 1-based, as a performer counts
}

TEST("validation messages are pure ASCII") {
    // Non-ASCII in a performer-facing string reached the screen as mojibake once already.
    Setlist l;
    const auto messages = {l.validate(), threeSongSet().validate()};
    for (const auto& m : messages) {
        for (char ch : m) {
            const auto u = static_cast<unsigned char>(ch);
            CHECK(u >= 0x20 && u <= 0x7e);
        }
    }
}

TEST_MAIN_END()
