// GhostBand — live AI accompaniment for singer-songwriters.
// Copyright 2026 GhostBand contributors. Licensed under Apache-2.0.

#include "TestMain.h"
#include "core/EngineState.h"

#include <thread>
#include <vector>

using namespace ghostband::core;

TEST_MAIN_BEGIN("EngineState")

TEST("starts Unloaded with no model") {
    EngineStateMachine sm;
    CHECK(sm.state() == EngineState::Unloaded);
    CHECK(!sm.hasModel());
    CHECK(!sm.isRunning());
    CHECK_EQ(sm.transitionCount(), 0u);
}

TEST("happy path: Unloaded -> Loading -> Ready -> Running -> Ready") {
    EngineStateMachine sm;
    CHECK(sm.transitionTo(EngineState::Loading));
    CHECK(sm.transitionTo(EngineState::Ready));
    CHECK(sm.hasModel());
    CHECK(sm.transitionTo(EngineState::Running));
    CHECK(sm.isRunning());
    CHECK(sm.transitionTo(EngineState::Ready));
    CHECK(!sm.isRunning());
    CHECK(sm.hasModel());
    CHECK_EQ(sm.transitionCount(), 4u);
}

TEST("cannot start generation without a loaded model") {
    EngineStateMachine sm;
    CHECK(!sm.transitionTo(EngineState::Running));
    CHECK(sm.state() == EngineState::Unloaded);
    CHECK_EQ(sm.transitionCount(), 0u);
}

TEST("cannot unload while running — must stop first") {
    // Tearing a model out from under a live inference thread is the crash we cannot
    // have on stage, so the state machine refuses the edge outright.
    EngineStateMachine sm;
    sm.transitionTo(EngineState::Loading);
    sm.transitionTo(EngineState::Ready);
    sm.transitionTo(EngineState::Running);

    CHECK(!sm.transitionTo(EngineState::Unloaded));
    CHECK(sm.isRunning());

    CHECK(sm.transitionTo(EngineState::Ready));
    CHECK(sm.transitionTo(EngineState::Unloaded));
}

TEST("self-transitions are rejected") {
    EngineStateMachine sm;
    CHECK(!sm.transitionTo(EngineState::Unloaded));
    CHECK(!sm.transition(EngineState::Unloaded, EngineState::Unloaded));
}

TEST("failure is reachable from every state and records a reason") {
    for (auto s : {EngineState::Unloaded, EngineState::Loading,
                   EngineState::Ready, EngineState::Running}) {
        CHECK(EngineStateMachine::isLegalTransition(s, EngineState::Error));
    }

    EngineStateMachine sm;
    sm.transitionTo(EngineState::Loading);
    sm.fail("mlxfn not found at /models/mrt2_small");

    CHECK(sm.state() == EngineState::Error);
    CHECK(!sm.hasModel());
    CHECK(!sm.isRunning());
    CHECK(sm.errorReason() == "mlxfn not found at /models/mrt2_small");
}

TEST("error recovery is explicit and routes through Unloaded") {
    EngineStateMachine sm;
    sm.transitionTo(EngineState::Loading);
    sm.transitionTo(EngineState::Ready);
    sm.fail("generation stalled");

    // No shortcut back to Ready: a retry must re-establish the model from cold.
    CHECK(!sm.transitionTo(EngineState::Ready));
    CHECK(!sm.transitionTo(EngineState::Running));
    CHECK(!sm.transitionTo(EngineState::Loading));

    CHECK(sm.clearError());
    CHECK(sm.state() == EngineState::Unloaded);
    CHECK(sm.errorReason().empty());

    CHECK(sm.transitionTo(EngineState::Loading));
}

TEST("clearError does nothing when not in Error") {
    EngineStateMachine sm;
    CHECK(!sm.clearError());
    CHECK(sm.state() == EngineState::Unloaded);
}

TEST("repeated failures do not inflate the transition count") {
    EngineStateMachine sm;
    sm.fail("one");
    const auto after_first = sm.transitionCount();
    sm.fail("two");
    CHECK_EQ(sm.transitionCount(), after_first);
    CHECK(sm.errorReason() == "two");
}

TEST("transition(from,to) is a no-op when current state differs") {
    EngineStateMachine sm;
    sm.transitionTo(EngineState::Loading);
    CHECK(!sm.transition(EngineState::Unloaded, EngineState::Loading));
    CHECK(sm.state() == EngineState::Loading);
}

TEST("concurrent transitionTo: exactly one winner, state stays coherent") {
    // The realistic race: a UI click and a MIDI footswitch both asking to start.
    // Exactly one must win, and the count must reflect exactly one transition.
    EngineStateMachine sm;
    sm.transitionTo(EngineState::Loading);
    sm.transitionTo(EngineState::Ready);
    const auto before = sm.transitionCount();

    std::atomic<int> winners{0};
    std::vector<std::thread> threads;
    threads.reserve(8);
    for (int i = 0; i < 8; ++i) {
        threads.emplace_back([&sm, &winners] {
            if (sm.transitionTo(EngineState::Running)) {
                winners.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }
    for (auto& t : threads) t.join();

    CHECK_EQ(winners.load(), 1);
    CHECK(sm.state() == EngineState::Running);
    CHECK_EQ(sm.transitionCount(), before + 1);
}

TEST("display strings are stage-legible and never empty") {
    CHECK(std::string(toDisplayString(EngineState::Unloaded)) == "NO MODEL");
    CHECK(std::string(toDisplayString(EngineState::Ready)) == "READY");
    CHECK(std::string(toDisplayString(EngineState::Running)) == "GENERATING");
    CHECK(std::string(toDisplayString(EngineState::Error)) == "ERROR");
    CHECK(std::string(toString(EngineState::Loading)) == "Loading");
}

TEST_MAIN_END()
