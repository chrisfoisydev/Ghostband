// GhostBand — live AI accompaniment for singer-songwriters.
// Copyright 2026 GhostBand contributors. Licensed under Apache-2.0.
//
// Walking a chord chart. The rules underneath these: the app never invents a tempo, an
// explicit press always wins, and a chord that does not parse holds rather than shifts
// everything after it.

#include "TestMain.h"
#include "core/SongMapPlayer.h"

using namespace ghostband::core;

namespace {

SongSection sectionWith(std::vector<std::string> chords, double tempo = 0.0) {
    SongSection s;
    s.name = "Verse";
    s.chordProgression = std::move(chords);
    if (tempo > 0.0) s.tempoBpm = tempo;
    return s;
}

/// A four-chord section at 120 BPM. At the default 4 beats per chord that is 2000 ms each.
SongSection fourChordsAt120() {
    return sectionWith({"G", "D", "Em", "C"}, 120.0);
}

} // namespace

TEST_MAIN_BEGIN("SongMapPlayer")

TEST("a fresh player has nothing and steers nothing") {
    SongMapPlayer p;
    CHECK(!p.isActive());
    CHECK(!p.isRunning());
    CHECK(p.size() == 0);
    CHECK(p.currentNotes().empty());
    CHECK(p.currentSymbol().empty());
    CHECK(!p.tick(10'000.0));
}

TEST("a section with no tempo is Manual, and the clock does nothing") {
    // The rule that keeps the performer in front. A default of 120 BPM here would be the
    // app deciding how fast the song goes.
    SongMapPlayer p;
    CHECK(p.loadSection(sectionWith({"G", "D", "Em", "C"})));
    CHECK(p.advanceMode() == SongMapAdvance::Manual);
    CHECK(p.msPerChord() == 0.0);

    p.start();
    CHECK(!p.tick(60'000.0));      // a full minute
    CHECK(p.index() == 0);
    CHECK(p.currentSymbol() == "G");
    CHECK(p.progress() == 0.0);    // no clock, so no meaningful progress
}

TEST("a section with a tempo runs on a clock") {
    SongMapPlayer p;
    CHECK(p.loadSection(fourChordsAt120()));
    CHECK(p.advanceMode() == SongMapAdvance::Clock);
    CHECK(p.msPerChord() == 2000.0);

    p.start();
    CHECK(!p.tick(1999.0));
    CHECK(p.index() == 0);
    CHECK(p.tick(1.0));
    CHECK(p.index() == 1);
    CHECK(p.currentSymbol() == "D");
}

TEST("the clock only runs once started") {
    SongMapPlayer p;
    p.loadSection(fourChordsAt120());
    CHECK(!p.isRunning());
    CHECK(!p.tick(10'000.0));
    CHECK(p.index() == 0);

    p.start();
    CHECK(p.tick(2000.0));
    p.stop();
    const int held = p.index();
    CHECK(!p.tick(10'000.0));
    CHECK(p.index() == held);
}

TEST("the progression loops, because a section repeats until the performer moves on") {
    SongMapPlayer p;
    p.loadSection(fourChordsAt120());
    p.start();
    for (int i = 0; i < 4; ++i) CHECK(p.tick(2000.0));
    CHECK(p.index() == 0);
    CHECK(p.currentSymbol() == "G");
}

TEST("an explicit press always works, in either mode") {
    SongMapPlayer manual;
    manual.loadSection(sectionWith({"G", "D", "Em", "C"}));
    manual.start();
    CHECK(manual.advance());
    CHECK(manual.currentSymbol() == "D");
    CHECK(manual.retreat());
    CHECK(manual.currentSymbol() == "G");
    CHECK(manual.retreat());
    CHECK(manual.currentSymbol() == "C");     // wraps backwards

    SongMapPlayer clocked;
    clocked.loadSection(fourChordsAt120());
    clocked.start();
    CHECK(clocked.advance());
    CHECK(clocked.currentSymbol() == "D");
}

TEST("an explicit press re-phases the clock") {
    // Without this, pressing just before a boundary would flick straight past the chord
    // the performer was asking for.
    SongMapPlayer p;
    p.loadSection(fourChordsAt120());
    p.start();

    CHECK(!p.tick(1900.0));          // 100 ms from the next chord
    CHECK(p.advance());              // deliberately move to D
    CHECK(p.currentSymbol() == "D");

    CHECK(!p.tick(100.0));           // the old boundary would have fired here
    CHECK(p.currentSymbol() == "D"); // still D: it got its full 2000 ms
    CHECK(p.tick(1900.0));
    CHECK(p.currentSymbol() == "Em");
}

TEST("restart goes to the top and re-phases") {
    SongMapPlayer p;
    p.loadSection(fourChordsAt120());
    p.start();
    p.tick(2000.0);
    p.tick(2000.0);
    CHECK(p.index() == 2);

    p.restart();
    CHECK(p.index() == 0);
    CHECK(p.progress() == 0.0);
    CHECK(!p.tick(1999.0));          // a full chord length from here, not a remainder
    CHECK(p.tick(1.0));
}

TEST("a chord that does not parse holds its slot and yields no notes") {
    // The alternative - dropping it - would shift every later chord earlier and put the
    // whole rest of the section out of step with the chart the singer is reading.
    SongMapPlayer p;
    CHECK(p.loadSection(sectionWith({"G", "Dxyz", "Em", "C"}, 120.0)));
    CHECK(p.size() == 4);
    CHECK(p.hasRejectedChords());
    CHECK(p.rejectedIndices() == std::vector<int>({1}));

    p.start();
    CHECK(!p.currentNotes().empty());          // G
    CHECK(p.tick(2000.0));
    CHECK(p.currentSymbol() == "Dxyz");
    CHECK(p.currentNotes().empty());           // nothing pushed: the band holds
    CHECK(p.tick(2000.0));
    CHECK(p.currentSymbol() == "Em");          // still in the right place
    CHECK(!p.currentNotes().empty());
}

TEST("a section where nothing parses steers nothing at all") {
    SongMapPlayer p;
    CHECK(!p.loadSection(sectionWith({"xyz", "???"}, 120.0)));
    CHECK(!p.isActive());
    CHECK(p.currentNotes().empty());

    // And it cannot be started into a state where it pretends to work.
    p.start();
    CHECK(!p.isRunning());
    CHECK(!p.tick(10'000.0));
}

TEST("an empty progression is inactive, not a crash") {
    SongMapPlayer p;
    CHECK(!p.loadSection(sectionWith({})));
    CHECK(p.size() == 0);
    CHECK(p.currentSymbol().empty());
    CHECK(p.nextSymbol().empty());
    CHECK(!p.advance());
    CHECK(!p.retreat());
    p.restart();                                // must not crash
    CHECK(p.index() == 0);
}

TEST("a single-chord section stays put") {
    SongMapPlayer p;
    CHECK(p.loadSection(sectionWith({"Am"}, 120.0)));
    p.start();
    CHECK(!p.tick(10'000.0));       // nowhere to advance to
    CHECK(p.currentSymbol() == "Am");
    CHECK(p.nextSymbol() == "Am");  // wraps to itself
}

TEST("the next chord is visible, because the stage screen shows what is coming") {
    SongMapPlayer p;
    p.loadSection(fourChordsAt120());
    CHECK(p.currentSymbol() == "G");
    CHECK(p.nextSymbol() == "D");
    p.advance();
    CHECK(p.nextSymbol() == "Em");
    p.advance();
    p.advance();
    CHECK(p.currentSymbol() == "C");
    CHECK(p.nextSymbol() == "G");   // wraps
}

TEST("progress runs 0 to 1 across a chord") {
    SongMapPlayer p;
    p.loadSection(fourChordsAt120());
    p.start();
    CHECK(p.progress() == 0.0);
    p.tick(1000.0);
    CHECK(p.progress() > 0.49);
    CHECK(p.progress() < 0.51);
    p.tick(500.0);
    CHECK(p.progress() > 0.74);
}

TEST("a huge delta lands somewhere sensible instead of spinning") {
    // A stalled message thread, or a laptop resumed from sleep mid-section.
    SongMapPlayer p;
    p.loadSection(fourChordsAt120());
    p.start();
    CHECK(p.tick(3'600'000.0));     // an hour
    CHECK(p.index() >= 0);
    CHECK(p.index() < p.size());
}

TEST("a backwards delta is ignored rather than rewinding") {
    // Wall-clock deltas can go negative if the system clock is adjusted mid-set.
    SongMapPlayer p;
    p.loadSection(fourChordsAt120());
    p.start();
    p.tick(1000.0);
    const double before = p.progress();
    CHECK(!p.tick(-5000.0));
    CHECK(p.progress() == before);
    CHECK(p.index() == 0);
}

TEST("beats per chord comes from the section, so sections can move at different rates") {
    // A chorus that moves twice as fast as its verse is ordinary songwriting, which is why
    // this is per section rather than a player-wide setting.
    SongMapPlayer p;

    auto verse = fourChordsAt120();
    verse.beatsPerChord = 4;
    p.loadSection(verse);
    CHECK(p.beatsPerChord() == 4);
    CHECK(p.msPerChord() == 2000.0);

    auto chorus = fourChordsAt120();
    chorus.beatsPerChord = 2;
    p.loadSection(chorus);
    CHECK(p.beatsPerChord() == 2);
    CHECK(p.msPerChord() == 1000.0);

    auto slow = fourChordsAt120();
    slow.beatsPerChord = 8;                 // two bars per chord
    p.loadSection(slow);
    CHECK(p.msPerChord() == 4000.0);
}

TEST("a nonsense beats-per-chord is clamped rather than trusted") {
    // Zero would make msPerChord zero and the clock advance forever in one tick. The file
    // loader clamps too, but a Song built in memory never goes through it.
    SongMapPlayer p;

    auto broken = fourChordsAt120();
    broken.beatsPerChord = 0;
    p.loadSection(broken);
    CHECK(p.beatsPerChord() == kMinBeatsPerChord);
    p.start();
    CHECK(p.msPerChord() > 0.0);
    CHECK(!p.tick(1.0));                    // does not run away

    broken.beatsPerChord = 9999;
    p.loadSection(broken);
    CHECK(p.beatsPerChord() == kMaxBeatsPerChord);
}

TEST("loading a new section resets everything") {
    SongMapPlayer p;
    p.loadSection(fourChordsAt120());
    p.start();
    p.tick(2000.0);
    CHECK(p.index() == 1);
    CHECK(p.isRunning());

    CHECK(p.loadSection(sectionWith({"Am", "F"})));
    CHECK(p.index() == 0);
    CHECK(!p.isRunning());                            // must be started deliberately
    CHECK(p.advanceMode() == SongMapAdvance::Manual); // the new section has no tempo
    CHECK(p.rejectedIndices().empty());
}

TEST_MAIN_END()
