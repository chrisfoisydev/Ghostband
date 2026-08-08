// Follow — live AI accompaniment for singer-songwriters.
// Copyright 2026 Follow contributors. Licensed under Apache-2.0.

#include "TestMain.h"
#include "backend/NullBackend.h"
#include "core/AiOutputStage.h"
#include "core/Logging.h"

#include <vector>

using namespace follow::core;
using follow::backend::NullBackend;

TEST_MAIN_BEGIN("Backend & Logging")

// ---------------------------------------------------------------------------
// NullBackend — honest absence
// ---------------------------------------------------------------------------

TEST("NullBackend declares itself not real") {
    // CLAUDE.md rule 2 in executable form: the UI must be able to tell that this
    // backend is not a working AI band.
    NullBackend backend;
    CHECK(!backend.isRealBackend());
    CHECK(std::string(backend.name()) == "none (no model loaded)");
    CHECK(backend.promptStatus() == PromptStatus::Idle);
}

TEST("NullBackend writes silence, not a placeholder tone") {
    NullBackend backend;
    backend.loadModel("irrelevant");
    backend.start();

    std::vector<float> l(1024, 7.0f), r(1024, -7.0f);
    const bool ok = backend.readStereo(l.data(), r.data(), l.size());

    CHECK(ok); // silence by design is not an underrun
    for (std::size_t i = 0; i < l.size(); ++i) {
        CHECK_EQ(l[i], 0.0f);
        CHECK_EQ(r[i], 0.0f);
    }
}

TEST("NullBackend tracks note state and clears it on allNotesOff") {
    NullBackend backend;
    backend.noteOn(60);
    backend.noteOn(64);
    backend.noteOn(67);
    CHECK_EQ(backend.heldNoteCount(), 3);
    CHECK(backend.isNoteHeld(64));

    backend.noteOff(64);
    CHECK(!backend.isNoteHeld(64));
    CHECK_EQ(backend.heldNoteCount(), 2);

    // Required on device disconnect, song change, and panic: a stuck note would pin
    // the band to one chord for the rest of the song.
    backend.allNotesOff();
    CHECK_EQ(backend.heldNoteCount(), 0);
}

TEST("out-of-range MIDI notes are ignored, not written out of bounds") {
    NullBackend backend;
    backend.noteOn(-1);
    backend.noteOn(128);
    backend.noteOn(9999);
    backend.noteOff(-5);
    CHECK_EQ(backend.heldNoteCount(), 0);
}

TEST("lifecycle: unload stops generation") {
    NullBackend backend;
    backend.loadModel("m");
    backend.start();
    CHECK(backend.isLoaded());
    CHECK(backend.isRunning());

    backend.unload();
    CHECK(!backend.isLoaded());
    CHECK(!backend.isRunning());
}

TEST("arrangement parameters default to MRT2's documented values") {
    // These defaults are copied from upstream hello_mrt2 (docs/MRT2_API_NOTES.md §6.3).
    // If they drift, the intensity macro's centre point silently moves.
    NullBackend backend;
    CHECK_NEAR(backend.cfgMusicCoca(), 3.0f, 1e-6);
    CHECK_NEAR(backend.cfgNotes(), 5.0f, 1e-6);
    CHECK_NEAR(backend.cfgDrums(), 1.0f, 1e-6);
    CHECK_NEAR(backend.temperature(), 1.0f, 1e-6);
    CHECK(!backend.drumless());
}

TEST("a backend pulled through the output stage produces valid silent audio") {
    // End-to-end shape of the Phase 0 audio path, minus MRT2 itself.
    NullBackend backend;
    backend.loadModel("m");
    backend.start();

    AiOutputStage stage;
    stage.prepare(48000.0, 512);
    stage.setMuted(false);

    std::vector<float> l(512), r(512);
    for (int block = 0; block < 100; ++block) {
        const bool ok = backend.readStereo(l.data(), r.data(), l.size());
        stage.process(l.data(), r.data(), l.size(), !ok);
    }

    CHECK_EQ(stage.diagnostics().snapshot().blocksProcessed, 100u);
    CHECK_EQ(stage.diagnostics().snapshot().audioUnderruns, 0u);
    CHECK(stage.safetyMonitor().health() == Health::Healthy);
    CHECK(stage.limiterStatus() == SafetyLimiter::Status::Safe);
}

// ---------------------------------------------------------------------------
// Logging
// ---------------------------------------------------------------------------

TEST("structured log lines carry level, category, message and sorted fields") {
    auto& log = Logger::instance();
    log.reset();

    std::vector<std::string> captured;
    log.setSink([&captured](const std::string& line) { captured.push_back(line); });

    log.info(LogCategory::Model, "model loaded",
             {{"model", "mrt2_small"}, {"elapsed_ms", "1840"}});

    CHECK_EQ(captured.size(), 1u);
    const std::string& line = captured[0];
    CHECK(line.find("INFO") != std::string::npos);
    CHECK(line.find("[model]") != std::string::npos);
    CHECK(line.find("model loaded") != std::string::npos);
    CHECK(line.find("model=mrt2_small") != std::string::npos);
    // Fields are sorted so two gig logs diff cleanly.
    CHECK(line.find("elapsed_ms=") < line.find("model="));

    log.reset();
}

TEST("values containing spaces are quoted") {
    auto& log = Logger::instance();
    log.reset();
    std::vector<std::string> captured;
    log.setSink([&captured](const std::string& l) { captured.push_back(l); });

    log.error(LogCategory::Generation, "stalled", {{"reason", "mlxfn not found"}});
    CHECK(captured[0].find("reason=\"mlxfn not found\"") != std::string::npos);
    log.reset();
}

TEST("level filtering suppresses below-threshold records entirely") {
    auto& log = Logger::instance();
    log.reset();
    std::vector<std::string> captured;
    log.setSink([&captured](const std::string& l) { captured.push_back(l); });
    log.setMinLevel(LogLevel::Warn);

    log.info(LogCategory::Audio, "should not appear");
    log.debug(LogCategory::Audio, "nor this");
    log.warn(LogCategory::Audio, "device changed");

    CHECK_EQ(captured.size(), 1u);
    CHECK(captured[0].find("device changed") != std::string::npos);
    log.reset();
}

TEST("error and warning counts are tracked for the diagnostics view") {
    auto& log = Logger::instance();
    log.reset();
    log.setSink([](const std::string&) {});

    log.error(LogCategory::Model, "load failed");
    log.error(LogCategory::Audio, "device lost");
    log.warn(LogCategory::Midi, "controller disconnected");

    CHECK_EQ(log.errorCount(), 2u);
    CHECK_EQ(log.warnCount(), 1u);
    log.reset();
}

TEST("the recent-lines ring is bounded and ordered oldest-first") {
    // A 3-hour rehearsal must not grow the log ring without limit.
    auto& log = Logger::instance();
    log.reset();
    log.setSink([](const std::string&) {});

    for (int i = 0; i < 2000; ++i) {
        log.info(LogCategory::Performance, "section change", {{"index", std::to_string(i)}});
    }

    const auto recent = log.recent(1000);
    CHECK(recent.size() <= 512u);      // capped by ring capacity
    CHECK(!recent.empty());
    CHECK(recent.back().find("index=1999") != std::string::npos);  // newest last
    CHECK(recent.front().find("index=1999") == std::string::npos); // oldest first

    log.reset();
}

TEST_MAIN_END()
