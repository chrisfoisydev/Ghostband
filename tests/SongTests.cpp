// GhostBand — live AI accompaniment for singer-songwriters.
// Copyright 2026 GhostBand contributors. Licensed under Apache-2.0.
//
// Phase 2 foundation: the Song/Section model and the controller that moves between them.
// The property that matters most here is not correctness of navigation — it is that the
// performer can never be trapped in a timeline.

#include "TestMain.h"
#include "core/GhostBandConstants.h"
#include "core/SectionController.h"
#include "core/Song.h"

using namespace ghostband::core;

TEST_MAIN_BEGIN("Song & Sections")

// ---------------------------------------------------------------------------
// Song
// ---------------------------------------------------------------------------

TEST("a section with no prompt inherits the song default") {
    // Inheritance is stored, not copied: editing the song prompt must update every
    // section that never overrode it.
    SongSection s;
    CHECK(s.resolvedPrompt("song default") == "song default");
    s.stylePrompt = "sparse piano";
    CHECK(s.resolvedPrompt("song default") == "sparse piano");
}

TEST("validation rejects songs that cannot be performed") {
    Song song;
    CHECK(!song.isValid());
    CHECK(song.validate() == "Song has no sections");

    // A section with no prompt and no song default leaves MRT2 unconditioned — the band
    // silently stays whatever it last was.
    SongSection bare;
    bare.name = "Verse";
    song.sections = {bare};
    CHECK(!song.isValid());
    CHECK(song.validate().find("no style prompt") != std::string::npos);

    song.defaultStylePrompt = "warm indie folk";
    CHECK(song.isValid());
}

TEST("validation catches out-of-range section values") {
    Song song = makeDemoSong();
    CHECK(song.isValid());

    song.sections[0].aiIntensity = 1.5f;
    CHECK(song.validate().find("intensity") != std::string::npos);
    song.sections[0].aiIntensity = 0.5f;

    song.sections[0].transitionMs = 99999;
    CHECK(song.validate().find("transition") != std::string::npos);
    song.sections[0].transitionMs = kTransitionFastMs;

    song.sections[0].tempoBpm = 5.0;
    CHECK(song.validate().find("tempo") != std::string::npos);
    song.sections[0].tempoBpm = 96.0;

    song.sections[0].name.clear();
    CHECK(song.validate().find("no name") != std::string::npos);
}

TEST("distinct prompts drive the slot budget, not section count") {
    // Ten sections that share two prompts still fit MRT2's six slots, so every change in
    // that song is instant. Counting sections instead would reject a perfectly fine song.
    Song song;
    song.defaultStylePrompt = "base";

    SongSection verse;
    verse.name = "Verse";
    verse.stylePrompt = "sparse";
    SongSection chorus;
    chorus.name = "Chorus";
    chorus.stylePrompt = "full";

    for (int i = 0; i < 5; ++i) {
        song.sections.push_back(verse);
        song.sections.push_back(chorus);
    }

    CHECK_EQ(song.sections.size(), 10u);
    CHECK_EQ(song.distinctPrompts().size(), 2u);
    CHECK(song.fitsPromptSlots());
}

TEST("a song with more distinct prompts than slots is flagged") {
    // Not invalid — just unable to guarantee an instant change. See KNOWN_ISSUES §4.
    Song song;
    song.defaultStylePrompt = "base";
    for (int i = 0; i < static_cast<int>(kMaxPrompts) + 1; ++i) {
        SongSection s;
        s.name = "S" + std::to_string(i);
        s.stylePrompt = "prompt " + std::to_string(i);
        song.sections.push_back(s);
    }
    CHECK(!song.fitsPromptSlots());
    CHECK(song.isValid());  // still performable, just not stall-free
}

TEST("the demo song is valid, generic, and fits the slots") {
    const Song song = makeDemoSong();
    CHECK(song.isValid());
    CHECK(song.fitsPromptSlots());
    CHECK(song.sections.size() >= 2u);

    // Brief §8: prompts describe musical attributes, never an artist.
    for (const auto& s : song.sections) {
        const auto p = s.resolvedPrompt(song.defaultStylePrompt);
        CHECK(p.find("style of") == std::string::npos);
        CHECK(p.find("instrumental") != std::string::npos);
    }
    // Verses should be quieter arrangements than choruses, or the demo teaches nothing.
    CHECK(song.sections[0].aiIntensity < song.sections[1].aiIntensity);
}

TEST("the experimental harmony source says so in its label") {
    // A performer must never discover mid-set that the source they picked was the
    // experimental one.
    const std::string label = toDisplayString(HarmonySource::GuitarExperimental);
    CHECK(label.find("EXPERIMENTAL") != std::string::npos);
    CHECK(std::string(toDisplayString(HarmonySource::Midi)) == "MIDI");
}

// ---------------------------------------------------------------------------
// SectionController
// ---------------------------------------------------------------------------

TEST("an empty controller is inert rather than crashing") {
    SectionController c;
    CHECK_EQ(c.sectionCount(), 0);
    CHECK(c.current() == nullptr);
    CHECK(c.peekNext() == nullptr);
    CHECK(!c.goToNext());
    CHECK(!c.goToPrevious());
    CHECK(!c.retriggerCurrent());
}

TEST("navigation moves forward and back") {
    const Song song = makeDemoSong();
    SectionController c;
    c.setSong(&song);

    CHECK_EQ(c.currentIndex(), 0);
    CHECK(c.current()->name == "Verse");
    CHECK(c.peekNext()->name == "Chorus");
    CHECK(c.isFirstSection());

    CHECK(c.goToNext());
    CHECK(c.current()->name == "Chorus");
    CHECK(c.isLastSection());
    CHECK(c.peekNext() == nullptr);  // nothing to preview at the end

    CHECK(c.goToPrevious());
    CHECK(c.current()->name == "Verse");
}

TEST("the set does not wrap in either direction") {
    // A surprise jump back to the top mid-song is worse than the button doing nothing.
    const Song song = makeDemoSong();
    SectionController c;
    c.setSong(&song);

    CHECK(!c.goToPrevious());        // already at the first
    CHECK_EQ(c.currentIndex(), 0);

    while (c.goToNext()) { }
    CHECK(c.isLastSection());
    CHECK(!c.goToNext());            // and stays there
    CHECK(c.isLastSection());
}

TEST("a chorus can be repeated — the performer is never trapped in a timeline") {
    // Brief §30.5. Repeating is not a mode; it is landing on the same section again, so
    // no other code has to know repeats exist.
    const Song song = makeDemoSong();
    SectionController c;
    c.setSong(&song);
    c.goToNext();  // Chorus

    const auto before = c.changeCount();
    CHECK(c.retriggerCurrent());
    CHECK_EQ(c.currentIndex(), 1);
    CHECK(c.current()->name == "Chorus");
    CHECK_EQ(c.changeCount(), before + 1);   // counted as a real change
    CHECK(c.isTransitioning());              // and it re-runs the transition

    // Arbitrary jumps are allowed too: back to the verse and forward again, any time.
    CHECK(c.goTo(0));
    CHECK(c.goTo(1));
    CHECK(!c.goTo(99));
    CHECK(!c.goTo(-1));
    CHECK_EQ(c.currentIndex(), 1);
}

TEST("goTo the current section is rejected, retrigger is not") {
    // The distinction matters: a footswitch bounce must not restart a section, but a
    // deliberate repeat must.
    const Song song = makeDemoSong();
    SectionController c;
    c.setSong(&song);
    CHECK(!c.goTo(0));
    CHECK(c.retriggerCurrent());
}

TEST("the transition ramps over the entering section's configured time") {
    // Time belongs to the section being entered — that is what a performer means when
    // they set it on the chorus.
    Song song = makeDemoSong();
    song.sections[1].transitionMs = 500;
    SectionController c;
    c.setSong(&song);

    c.goToNext();
    CHECK(c.isTransitioning());
    CHECK_NEAR(c.transitionProgress(), 0.0f, 1e-5);

    c.tick(250.0);
    CHECK_NEAR(c.transitionProgress(), 0.5f, 1e-4);

    c.tick(250.0);
    CHECK_NEAR(c.transitionProgress(), 1.0f, 1e-5);
    CHECK(!c.isTransitioning());

    c.tick(1000.0);                       // and does not overshoot
    CHECK_NEAR(c.transitionProgress(), 1.0f, 1e-5);
}

TEST("an instant transition is settled immediately") {
    Song song = makeDemoSong();
    song.sections[1].transitionMs = kTransitionInstantMs;
    SectionController c;
    c.setSong(&song);

    c.goToNext();
    CHECK(!c.isTransitioning());
    CHECK_NEAR(c.transitionProgress(), 1.0f, 1e-5);
}

TEST("previousIndex names where the transition came from") {
    // The blend needs both ends: what we are leaving and what we are entering.
    const Song song = makeDemoSong();
    SectionController c;
    c.setSong(&song);

    c.goToNext();
    CHECK_EQ(c.previousIndex(), 0);
    CHECK_EQ(c.currentIndex(), 1);
}

TEST("snapTransition lands settled, for song load and panic recovery") {
    // Crossfading from a section that was never heard would be meaningless.
    Song song = makeDemoSong();
    song.sections[1].transitionMs = 2000;
    SectionController c;
    c.setSong(&song);
    c.goToNext();
    CHECK(c.isTransitioning());

    c.snapTransition();
    CHECK(!c.isTransitioning());
    CHECK_EQ(c.previousIndex(), c.currentIndex());
}

TEST("loading a song resets to the first section, settled") {
    const Song song = makeDemoSong();
    SectionController c;
    c.setSong(&song);
    c.goToNext();

    c.setSong(&song);
    CHECK_EQ(c.currentIndex(), 0);
    CHECK(!c.isTransitioning());
    CHECK_EQ(c.changeCount(), 0u);
}

TEST("negative tick deltas cannot run the transition backwards") {
    Song song = makeDemoSong();
    song.sections[1].transitionMs = 500;
    SectionController c;
    c.setSong(&song);
    c.goToNext();

    c.tick(250.0);
    const float mid = c.transitionProgress();
    c.tick(-1000.0);
    CHECK(c.transitionProgress() >= mid);
}

TEST_MAIN_END()
