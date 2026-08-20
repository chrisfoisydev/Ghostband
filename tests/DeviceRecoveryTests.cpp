// GhostBand — live AI accompaniment for singer-songwriters.
// Copyright 2026 GhostBand contributors. Licensed under Apache-2.0.
//
// Audio device loss and recovery. The rules underneath most of these: the device comes
// back on its own, the band does not, and the retry loop never gives up on the gig.

#include "TestMain.h"
#include "core/DeviceRecovery.h"

using namespace ghostband::core;

namespace {

/// A recovery already in the Lost state at t=0, which is the starting point for most of
/// these tests.
DeviceRecovery lostAt(std::int64_t t = 0) {
    DeviceRecovery r;
    r.noteLost("device disappeared", t);
    return r;
}

} // namespace

TEST_MAIN_BEGIN("DeviceRecovery")

TEST("a fresh recovery is running and idle") {
    DeviceRecovery r;
    CHECK(r.health() == DeviceHealth::Running);
    CHECK(r.attempts() == 0);
    CHECK(!r.shouldRetryNow(0));
    CHECK(!r.shouldRetryNow(1'000'000));
    CHECK(r.reason().empty());
    CHECK(r.displayReason().empty());
}

TEST("losing the device schedules a fast first retry") {
    auto r = lostAt(1000);
    CHECK(r.health() == DeviceHealth::Lost);
    CHECK(r.reason() == "device disappeared");

    // Not immediately: a device mid-failure is not ready to reopen the instant it errors.
    CHECK(!r.shouldRetryNow(1000));
    CHECK(!r.shouldRetryNow(1399));
    CHECK(r.shouldRetryNow(1400));
}

TEST("retries back off exponentially and then cap") {
    DeviceRecovery::Config config;
    config.firstRetryMs = 400;
    config.maxRetryMs = 8000;
    config.backoffFactor = 2.0;
    DeviceRecovery r{config};

    r.noteLost("gone", 0);

    // Asserted as the wall-clock times attempts actually happen, rather than as a list of
    // gaps: the gaps are easy to state off-by-one (this test did, first time round) and
    // what matters operationally is "how long until it tries again", which is what these
    // absolute times show. Gaps are 400, 800, 1600, 3200, 6400, then capped at 8000.
    const std::int64_t attempt_times[] = {400, 1200, 2800, 6000, 12400, 20400, 28400, 36400};

    std::int64_t now = 0;
    for (std::int64_t want : attempt_times) {
        now += r.nextRetryInMs(now);
        const std::int64_t got = now;
        CHECK(got == want);
        CHECK(r.shouldRetryNow(now));
        r.noteRetryFailed(now);
    }

    // Just under a minute to make eight attempts, and one attempt every 8 s thereafter —
    // cheap enough to leave running for the rest of a set.
    CHECK(now < 40'000);
}

TEST("a burst of errors does not reset the schedule") {
    // A dying interface reports repeatedly. If each report restarted the backoff, the
    // retry loop would become a tight spin at the worst possible moment.
    auto r = lostAt(0);
    r.noteRetryFailed(400);
    r.noteRetryFailed(1200);
    const int attempts_before = r.attempts();
    const std::int64_t due_before = r.nextRetryInMs(1200);

    r.noteLost("still gone", 1200);
    r.noteLost("really quite gone", 1201);

    CHECK(r.attempts() == attempts_before);
    CHECK(r.nextRetryInMs(1200) == due_before);
    CHECK(r.reason() == "device disappeared");   // the first reason, not the last
}

TEST("a successful reopen restores audio but NOT the band") {
    auto r = lostAt(0);
    r.noteRetryFailed(400);
    r.noteRetrySucceeded();

    // The distinction this whole class exists for. Running would mean the band is playing
    // again, which nobody asked for and which would arrive mid-phrase.
    CHECK(r.health() == DeviceHealth::Restored);
    CHECK(r.health() != DeviceHealth::Running);
    CHECK(!r.shouldRetryNow(1'000'000));
}

TEST("the band resumes only when explicitly acknowledged") {
    auto r = lostAt(0);
    r.noteRetrySucceeded();
    CHECK(r.health() == DeviceHealth::Restored);

    CHECK(r.acknowledgeRestored());
    CHECK(r.health() == DeviceHealth::Running);
    CHECK(r.attempts() == 0);
    CHECK(r.reason().empty());
}

TEST("acknowledging when nothing is wrong does nothing") {
    // A stray press on RECOVER BAND must not be able to fake a recovery.
    DeviceRecovery running;
    CHECK(!running.acknowledgeRestored());
    CHECK(running.health() == DeviceHealth::Running);

    auto lost = lostAt(0);
    CHECK(!lost.acknowledgeRestored());
    CHECK(lost.health() == DeviceHealth::Lost);
}

TEST("by default it never gives up") {
    auto r = lostAt(0);
    std::int64_t now = 0;
    for (int i = 0; i < 5000; ++i) {          // ~11 hours at the 8 s cap
        now += r.nextRetryInMs(now);
        r.noteRetryFailed(now);
    }
    CHECK(!r.hasGivenUp());
    CHECK(r.shouldRetryNow(now + 8000));
    CHECK(r.health() == DeviceHealth::Lost);
}

TEST("a bounded config does give up, and says so") {
    DeviceRecovery::Config config;
    config.maxAttempts = 3;
    DeviceRecovery r{config};

    std::int64_t now = 0;
    r.noteLost("gone", now);
    for (int i = 0; i < 3; ++i) {
        now += r.nextRetryInMs(now);
        r.noteRetryFailed(now);
    }
    CHECK(r.hasGivenUp());
    CHECK(!r.shouldRetryNow(now + 1'000'000));
}

TEST("the device coming back cleanly clears everything") {
    auto r = lostAt(0);
    r.noteRetryFailed(400);
    CHECK(r.attempts() == 1);

    r.noteRunning();
    CHECK(r.health() == DeviceHealth::Running);
    CHECK(r.attempts() == 0);
    CHECK(r.reason().empty());
    CHECK(!r.shouldRetryNow(1'000'000));
}

TEST("the performer-facing message names the voice and guitar first") {
    // The one question worth answering under stage lights is "is my own signal still
    // working?" - so the banner has to answer it without being read twice.
    auto r = lostAt(0);
    const auto lost = r.displayReason();
    CHECK(lost.find("voice and guitar are unaffected") != std::string::npos);
    CHECK(lost.find("error") == std::string::npos);

    r.noteRetrySucceeded();
    const auto restored = r.displayReason();
    CHECK(restored.find("still silent") != std::string::npos);
    CHECK(restored.find("RECOVER BAND") != std::string::npos);
}

TEST("status text distinguishes device health from band health") {
    CHECK(std::string(toDisplayString(DeviceHealth::Running)) == "OK");
    CHECK(std::string(toDisplayString(DeviceHealth::Lost)) == "NO OUTPUT");
    // Deliberately not "RECOVERED" - the device recovered, the band did not.
    CHECK(std::string(toDisplayString(DeviceHealth::Restored)) == "AUDIO BACK");
}

TEST_MAIN_END()
