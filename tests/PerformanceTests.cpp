// GhostBand — live AI accompaniment for singer-songwriters.
// Copyright 2026 GhostBand contributors. Licensed under Apache-2.0.
//
// The whole performance flow: load a song, move between sections, and confirm the band
// actually changes. Runs against NullBackend, so it needs no audio device, no model, and
// no Mac — which is the point of keeping this logic in core.

#include "TestMain.h"
#include "backend/NullBackend.h"
#include "core/Logging.h"
#include "core/PerformanceEngine.h"

using namespace ghostband::core;
using ghostband::backend::NullBackend;

namespace {

Song threeSectionSong() {
    Song song;
    song.title = "Test Song";
    song.defaultStylePrompt = "base";

    SongSection verse;
    verse.name = "Verse";
    verse.stylePrompt = "sparse verse";
    verse.aiIntensity = 0.2f;
    verse.transitionMs = 500;

    SongSection chorus;
    chorus.name = "Chorus";
    chorus.stylePrompt = "full chorus";
    chorus.aiIntensity = 0.8f;
    chorus.transitionMs = 250;

    SongSection bridge;
    bridge.name = "Bridge";
    bridge.stylePrompt = "bridge";
    bridge.aiIntensity = 0.5f;
    bridge.transitionMs = kTransitionInstantMs;

    song.sections = {verse, chorus, bridge};
    return song;
}

float sum(const std::array<float, kMaxPrompts>& w) {
    float t = 0.0f;
    for (float x : w) t += x;
    return t;
}

} // namespace

TEST_MAIN_BEGIN("PerformanceEngine")

// The engine logs section changes by design. Silence it here so a failure is legible
// rather than buried in structured log lines.
Logger::instance().setSink([](const std::string&) {});

TEST("loading a song settles on the first section and applies its intensity") {
    NullBackend backend;
    PerformanceEngine engine;
    engine.setBackend(&backend);

    const Song song = threeSectionSong();
    CHECK(engine.loadSong(&song));

    CHECK(engine.hasSong());
    CHECK_EQ(engine.sections().currentIndex(), 0);
    CHECK(engine.sections().current()->name == "Verse");
    CHECK(!engine.sections().isTransitioning());   // no crossfade from nothing
    CHECK_NEAR(engine.intensity().intensity(), 0.2f, 1e-5);
}

TEST("an invalid song is refused rather than half-loaded") {
    NullBackend backend;
    PerformanceEngine engine;
    engine.setBackend(&backend);

    Song broken;   // no sections
    CHECK(!engine.loadSong(&broken));
    CHECK(!engine.hasSong());
}

TEST("every section change is instant when the song fits its slots") {
    // The property the whole prompt-slot design exists to guarantee.
    NullBackend backend;
    PerformanceEngine engine;
    engine.setBackend(&backend);
    const Song song = threeSectionSong();
    engine.loadSong(&song);

    CHECK(engine.allocator().isFullyResident());

    CHECK(engine.goToNext());
    CHECK(!engine.lastChangeRequiredEncode());
    CHECK(engine.goToNext());
    CHECK(!engine.lastChangeRequiredEncode());
    CHECK(engine.goToPrevious());
    CHECK(!engine.lastChangeRequiredEncode());
    CHECK_EQ(engine.encodedChangeCount(), 0u);
}

TEST("a section change applies that section's authored intensity") {
    // The live knob is deliberately overwritten: the section's value is what the performer
    // wrote for this part of the song, and a stale knob would silently override it.
    NullBackend backend;
    PerformanceEngine engine;
    engine.setBackend(&backend);
    const Song song = threeSectionSong();
    engine.loadSong(&song);

    CHECK_NEAR(engine.intensity().intensity(), 0.2f, 1e-5);   // Verse
    const float quiet_drums = backend.cfgDrums();

    engine.goToNext();
    CHECK_NEAR(engine.intensity().intensity(), 0.8f, 1e-5);   // Chorus
    const float loud_drums = backend.cfgDrums();
    CHECK(loud_drums > quiet_drums);                          // the band genuinely fills out

    engine.goToPrevious();
    CHECK_NEAR(engine.intensity().intensity(), 0.2f, 1e-5);
    CHECK(backend.cfgDrums() < loud_drums);                   // and thins again
}

TEST("a very quiet section removes the drums entirely") {
    // 0.2 sits above the drumless threshold; a genuinely sparse section sits below it.
    NullBackend backend;
    PerformanceEngine engine;
    engine.setBackend(&backend);

    Song song = threeSectionSong();
    song.sections[0].aiIntensity = 0.05f;
    engine.loadSong(&song);
    CHECK(backend.drumless());

    engine.goToNext();                       // Chorus at 0.8
    CHECK(!backend.drumless());
}

TEST("the transition crossfades between section slots and settles") {
    NullBackend backend;
    PerformanceEngine engine;
    engine.setBackend(&backend);
    const Song song = threeSectionSong();
    engine.loadSong(&song);

    const int verse_slot = engine.allocator().slotFor(0);
    const int chorus_slot = engine.allocator().slotFor(1);
    CHECK(verse_slot != chorus_slot);

    engine.goToNext();   // chorus, 250 ms
    auto w = engine.currentBlendWeights();
    CHECK_NEAR(w[static_cast<std::size_t>(verse_slot)], 1.0f, 1e-4);  // still all verse
    CHECK_NEAR(sum(w), 1.0f, 1e-4);

    engine.tick(125.0);
    w = engine.currentBlendWeights();
    CHECK_NEAR(w[static_cast<std::size_t>(verse_slot)], 0.5f, 1e-3);
    CHECK_NEAR(w[static_cast<std::size_t>(chorus_slot)], 0.5f, 1e-3);
    CHECK_NEAR(sum(w), 1.0f, 1e-4);

    engine.tick(125.0);
    w = engine.currentBlendWeights();
    CHECK_NEAR(w[static_cast<std::size_t>(chorus_slot)], 1.0f, 1e-4);
    CHECK(!engine.sections().isTransitioning());
}

TEST("an instant-transition section arrives with no crossfade") {
    NullBackend backend;
    PerformanceEngine engine;
    engine.setBackend(&backend);
    const Song song = threeSectionSong();
    engine.loadSong(&song);

    engine.goTo(2);   // Bridge, transition 0 ms
    CHECK(!engine.sections().isTransitioning());
    const auto w = engine.currentBlendWeights();
    CHECK_NEAR(w[static_cast<std::size_t>(engine.allocator().slotFor(2))], 1.0f, 1e-4);
}

TEST("repeating a section re-runs its transition") {
    NullBackend backend;
    PerformanceEngine engine;
    engine.setBackend(&backend);
    const Song song = threeSectionSong();
    engine.loadSong(&song);
    engine.goToNext();                       // Chorus
    for (int i = 0; i < 10; ++i) engine.tick(100.0);
    CHECK(!engine.sections().isTransitioning());

    CHECK(engine.retriggerCurrent());
    CHECK(engine.sections().isTransitioning());
    CHECK_EQ(engine.sections().currentIndex(), 1);

    // Weight stays entirely on the chorus slot: it is a re-entry, not a crossfade to
    // itself, so nothing should dip.
    const auto w = engine.currentBlendWeights();
    CHECK_NEAR(w[static_cast<std::size_t>(engine.allocator().slotFor(1))], 1.0f, 1e-4);
}

TEST("blend weights always sum to 1 across a whole performance") {
    // Anything less would quietly weaken the style conditioning mid-song.
    NullBackend backend;
    PerformanceEngine engine;
    engine.setBackend(&backend);
    const Song song = threeSectionSong();
    engine.loadSong(&song);

    const int moves[] = {1, 2, 1, 0, 2, 0};
    for (int target : moves) {
        engine.goTo(target);
        for (int i = 0; i < 12; ++i) {
            engine.tick(25.0);
            CHECK_NEAR(sum(engine.currentBlendWeights()), 1.0f, 1e-4);
        }
    }
}

TEST("a song that overflows its slots reports the stall instead of hiding it") {
    NullBackend backend;
    PerformanceEngine engine;
    engine.setBackend(&backend);

    Song song;
    song.defaultStylePrompt = "base";
    for (int i = 0; i < static_cast<int>(kMaxPrompts) + 2; ++i) {
        SongSection s;
        s.name = "S" + std::to_string(i);
        s.stylePrompt = "prompt " + std::to_string(i);
        song.sections.push_back(s);
    }
    CHECK(engine.loadSong(&song));
    CHECK(!engine.allocator().isFullyResident());

    // Early sections are resident, so their changes stay instant.
    engine.goTo(1);
    CHECK(!engine.lastChangeRequiredEncode());

    // Reaching the tail costs an encode — and says so.
    engine.goTo(static_cast<int>(kMaxPrompts) + 1);
    CHECK(engine.lastChangeRequiredEncode());
    CHECK(engine.encodedChangeCount() > 0u);
}

TEST("a section with the band off is reported, not silently ignored") {
    // A solo verse is a musical choice. The caller mutes its output stage rather than
    // stopping generation, which would cost a restart on the way back in.
    NullBackend backend;
    PerformanceEngine engine;
    engine.setBackend(&backend);

    Song song = threeSectionSong();
    song.sections[0].aiEnabled = false;
    engine.loadSong(&song);

    CHECK(!engine.currentSectionAiEnabled());
    engine.goToNext();
    CHECK(engine.currentSectionAiEnabled());
}

TEST("the engine works with no backend attached") {
    // Section state must be trackable before a model exists, and testable without one.
    PerformanceEngine engine;
    const Song song = threeSectionSong();
    CHECK(engine.loadSong(&song));
    CHECK(engine.goToNext());
    engine.tick(100.0);
    CHECK_EQ(engine.sections().currentIndex(), 1);
}

TEST("unloading leaves the engine inert rather than dangling") {
    NullBackend backend;
    PerformanceEngine engine;
    engine.setBackend(&backend);
    const Song song = threeSectionSong();
    engine.loadSong(&song);

    engine.unloadSong();
    CHECK(!engine.hasSong());
    CHECK(!engine.goToNext());
    engine.tick(100.0);   // must not touch the freed song
    CHECK(engine.sections().current() == nullptr);
}

TEST("ticking a settled section does not keep writing to the backend") {
    // A settled section should not cost a parameter write every timer tick.
    NullBackend backend;
    PerformanceEngine engine;
    engine.setBackend(&backend);
    const Song song = threeSectionSong();
    engine.loadSong(&song);

    engine.goTo(2);   // instant transition, settles immediately
    const auto before = engine.currentBlendWeights();
    for (int i = 0; i < 50; ++i) engine.tick(10.0);
    const auto after = engine.currentBlendWeights();

    for (std::size_t i = 0; i < before.size(); ++i) CHECK_NEAR(before[i], after[i], 1e-6);
}

TEST_MAIN_END()
