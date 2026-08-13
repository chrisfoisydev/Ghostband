// GhostBand — live AI accompaniment for singer-songwriters.
// Copyright 2026 GhostBand contributors. Licensed under Apache-2.0.
//
// Foot-controller mapping. Everything here is a stage failure mode written down as a
// test: a stomp that advances two sections, a bounce that skips one, a pedal that goes
// quietly dead after a re-map, a settings file that throws on load mid-soundcheck.

#include "TestMain.h"
#include "core/MidiMapping.h"

using namespace ghostband::core;

namespace {

MidiBinding note(int number, int channel = 0) {
    return {MidiBinding::Type::Note, channel, number};
}

MidiBinding cc(int number, int channel = 0) {
    return {MidiBinding::Type::ControlChange, channel, number};
}

} // namespace

TEST_MAIN_BEGIN("MidiMapping")

TEST("an unbound message does nothing") {
    MidiMappingSet set;
    CHECK_EQ(static_cast<int>(set.handleMessage(note(60), true, 0.0)),
             static_cast<int>(PerformanceAction::None));
    CHECK(!set.hasBinding(PerformanceAction::NextSection));
}

TEST("a bound press fires its action") {
    MidiMappingSet set;
    set.bind(PerformanceAction::NextSection, note(61));

    CHECK(set.hasBinding(PerformanceAction::NextSection));
    CHECK(set.bindingFor(PerformanceAction::NextSection) == note(61));
    CHECK_EQ(static_cast<int>(set.handleMessage(note(61), true, 0.0)),
             static_cast<int>(PerformanceAction::NextSection));
}

TEST("a release never fires") {
    // A footswitch sends one message down and another up. Acting on both would advance
    // two sections per stomp.
    MidiMappingSet set;
    set.bind(PerformanceAction::NextSection, note(61));

    CHECK_EQ(static_cast<int>(set.handleMessage(note(61), false, 0.0)),
             static_cast<int>(PerformanceAction::None));
    // ...and the ignored release must not have consumed the debounce budget.
    CHECK_EQ(static_cast<int>(set.handleMessage(note(61), true, 1.0)),
             static_cast<int>(PerformanceAction::NextSection));
}

TEST("a mechanical bounce does not skip a section") {
    // The failure this exists to prevent: NEXT fires twice from one stomp, the performer
    // lands on the wrong section, and there is no graceful recovery in front of a room.
    MidiMappingSet set;
    set.bind(PerformanceAction::NextSection, note(61));
    CHECK_NEAR(set.debounceMs(), 120.0, 1e-9);

    CHECK_EQ(static_cast<int>(set.handleMessage(note(61), true, 1000.0)),
             static_cast<int>(PerformanceAction::NextSection));
    CHECK_EQ(static_cast<int>(set.handleMessage(note(61), true, 1005.0)),
             static_cast<int>(PerformanceAction::None));   // bounce
    CHECK_EQ(static_cast<int>(set.handleMessage(note(61), true, 1119.0)),
             static_cast<int>(PerformanceAction::None));   // still inside the window
    CHECK_EQ(static_cast<int>(set.handleMessage(note(61), true, 1120.0)),
             static_cast<int>(PerformanceAction::NextSection));  // deliberate second stomp
}

TEST("debouncing is per-binding, not global") {
    // Stomping NEXT then PANIC in quick succession is a real thing a performer does, and
    // a shared debounce would swallow the PANIC.
    MidiMappingSet set;
    set.bind(PerformanceAction::NextSection, note(61));
    set.bind(PerformanceAction::Panic, note(64));

    CHECK_EQ(static_cast<int>(set.handleMessage(note(61), true, 500.0)),
             static_cast<int>(PerformanceAction::NextSection));
    CHECK_EQ(static_cast<int>(set.handleMessage(note(64), true, 510.0)),
             static_cast<int>(PerformanceAction::Panic));
}

TEST("the debounce window is configurable") {
    MidiMappingSet set;
    set.bind(PerformanceAction::NextSection, note(61));
    set.setDebounceMs(10.0);

    CHECK_EQ(static_cast<int>(set.handleMessage(note(61), true, 0.0)),
             static_cast<int>(PerformanceAction::NextSection));
    CHECK_EQ(static_cast<int>(set.handleMessage(note(61), true, 5.0)),
             static_cast<int>(PerformanceAction::None));
    CHECK_EQ(static_cast<int>(set.handleMessage(note(61), true, 10.0)),
             static_cast<int>(PerformanceAction::NextSection));

    set.setDebounceMs(-5.0);   // nonsense clamps to "no debounce" rather than misbehaving
    CHECK_NEAR(set.debounceMs(), 0.0, 1e-9);
}

TEST("channel 0 matches any channel, and a specific channel is respected") {
    MidiMappingSet any;
    any.bind(PerformanceAction::NextSection, note(61, 0));
    CHECK_EQ(static_cast<int>(any.actionFor(note(61, 7))),
             static_cast<int>(PerformanceAction::NextSection));
    CHECK_EQ(static_cast<int>(any.actionFor(note(61, 16))),
             static_cast<int>(PerformanceAction::NextSection));

    MidiMappingSet pinned;
    pinned.bind(PerformanceAction::NextSection, note(61, 3));
    CHECK_EQ(static_cast<int>(pinned.actionFor(note(61, 3))),
             static_cast<int>(PerformanceAction::NextSection));
    CHECK_EQ(static_cast<int>(pinned.actionFor(note(61, 4))),
             static_cast<int>(PerformanceAction::None));
}

TEST("notes and CCs on the same number are different bindings") {
    MidiMappingSet set;
    set.bind(PerformanceAction::NextSection, note(64));
    set.bind(PerformanceAction::Panic, cc(64));

    CHECK_EQ(static_cast<int>(set.actionFor(note(64))),
             static_cast<int>(PerformanceAction::NextSection));
    CHECK_EQ(static_cast<int>(set.actionFor(cc(64))),
             static_cast<int>(PerformanceAction::Panic));
}

TEST("rebinding an action replaces its binding rather than accumulating") {
    MidiMappingSet set;
    set.bind(PerformanceAction::NextSection, note(61));
    set.bind(PerformanceAction::NextSection, cc(20));

    CHECK(set.bindingFor(PerformanceAction::NextSection) == cc(20));
    CHECK_EQ(static_cast<int>(set.actionFor(note(61))),
             static_cast<int>(PerformanceAction::None));
}

TEST("binding a switch already in use steals it and says so") {
    // Two actions on one switch is never what anyone means. The alternative — leaving
    // both — gives an ambiguous pedal; staying silent gives one that quietly went dead.
    MidiMappingSet set;
    set.bind(PerformanceAction::NextSection, note(61));
    CHECK_EQ(static_cast<int>(set.lastDisplacedAction()),
             static_cast<int>(PerformanceAction::None));

    set.bind(PerformanceAction::Panic, note(61));
    CHECK_EQ(static_cast<int>(set.lastDisplacedAction()),
             static_cast<int>(PerformanceAction::NextSection));

    CHECK(!set.hasBinding(PerformanceAction::NextSection));
    CHECK_EQ(static_cast<int>(set.actionFor(note(61))),
             static_cast<int>(PerformanceAction::Panic));
}

TEST("clearing a binding leaves the pedal inert") {
    MidiMappingSet set;
    set.bind(PerformanceAction::Panic, note(64));
    set.clear(PerformanceAction::Panic);

    CHECK(!set.hasBinding(PerformanceAction::Panic));
    CHECK_EQ(static_cast<int>(set.handleMessage(note(64), true, 0.0)),
             static_cast<int>(PerformanceAction::None));

    set.bind(PerformanceAction::Panic, note(64));   // and can be re-bound
    CHECK_EQ(static_cast<int>(set.handleMessage(note(64), true, 1.0)),
             static_cast<int>(PerformanceAction::Panic));
}

TEST("mappedCount tracks how much of the pedal is live") {
    MidiMappingSet set;
    CHECK_EQ(set.mappedCount(), 0);

    set.bind(PerformanceAction::NextSection, cc(81));
    CHECK_EQ(set.mappedCount(), 1);
    set.bind(PerformanceAction::Panic, cc(84));
    CHECK_EQ(set.mappedCount(), 2);

    set.bind(PerformanceAction::PreviousSection, cc(84));   // steals from Panic
    CHECK_EQ(set.mappedCount(), 2);

    set.clear(PerformanceAction::NextSection);
    CHECK_EQ(set.mappedCount(), 1);

    CHECK_EQ(MidiMappingSet::makeDefault().mappedCount(), 5);
}

TEST("clearAll empties the set") {
    MidiMappingSet set = MidiMappingSet::makeDefault();
    set.clearAll();
    for (auto a : allPerformanceActions()) CHECK(!set.hasBinding(a));
    CHECK_EQ(set.mappedCount(), 0);
    CHECK(set.serialise().empty());
}

TEST("an invalid binding is refused rather than stored") {
    MidiMappingSet set;
    set.bind(PerformanceAction::NextSection, MidiBinding{});
    CHECK(!set.hasBinding(PerformanceAction::NextSection));

    set.bind(PerformanceAction::None, note(61));
    CHECK_EQ(static_cast<int>(set.actionFor(note(61))),
             static_cast<int>(PerformanceAction::None));
}

// --- MIDI Learn -------------------------------------------------------------------

TEST("learn captures the next press and does not perform it") {
    // Mapping PANIC must not silence the band as a side effect of setting it up.
    MidiMappingSet set;
    set.beginLearn(PerformanceAction::Panic);
    CHECK(set.isLearning());
    CHECK_EQ(static_cast<int>(set.learningAction()),
             static_cast<int>(PerformanceAction::Panic));

    CHECK_EQ(static_cast<int>(set.handleMessage(cc(45), true, 0.0)),
             static_cast<int>(PerformanceAction::None));
    CHECK(!set.isLearning());
    CHECK(set.bindingFor(PerformanceAction::Panic) == cc(45));

    // The next press of the same switch does fire it.
    CHECK_EQ(static_cast<int>(set.handleMessage(cc(45), true, 500.0)),
             static_cast<int>(PerformanceAction::Panic));
}

TEST("learn ignores releases, so it captures the switch the performer pressed") {
    // Pressing a pedal that is already held would otherwise be learnt from its release.
    MidiMappingSet set;
    set.beginLearn(PerformanceAction::NextSection);
    set.handleMessage(note(61), false, 0.0);
    CHECK(set.isLearning());
    CHECK(!set.hasBinding(PerformanceAction::NextSection));

    set.handleMessage(note(61), true, 1.0);
    CHECK(!set.isLearning());
    CHECK(set.bindingFor(PerformanceAction::NextSection) == note(61));
}

TEST("cancelling learn leaves the set untouched") {
    MidiMappingSet set;
    set.bind(PerformanceAction::NextSection, note(61));
    set.beginLearn(PerformanceAction::Panic);
    set.cancelLearn();

    CHECK(!set.isLearning());
    CHECK(!set.hasBinding(PerformanceAction::Panic));
    CHECK_EQ(static_cast<int>(set.handleMessage(note(61), true, 0.0)),
             static_cast<int>(PerformanceAction::NextSection));
}

TEST("beginLearn clears any stale displacement report") {
    MidiMappingSet set;
    set.bind(PerformanceAction::NextSection, note(61));
    set.bind(PerformanceAction::Panic, note(61));   // steals
    CHECK_EQ(static_cast<int>(set.lastDisplacedAction()),
             static_cast<int>(PerformanceAction::NextSection));

    set.beginLearn(PerformanceAction::RepeatSection);
    CHECK_EQ(static_cast<int>(set.lastDisplacedAction()),
             static_cast<int>(PerformanceAction::None));
}

// --- Defaults ---------------------------------------------------------------------

TEST("the default layout is a five-switch pedal") {
    MidiMappingSet set = MidiMappingSet::makeDefault();

    CHECK(set.bindingFor(PerformanceAction::PreviousSection) == cc(80));
    CHECK(set.bindingFor(PerformanceAction::NextSection) == cc(81));
    CHECK(set.bindingFor(PerformanceAction::ToggleAiBand) == cc(82));
    CHECK(set.bindingFor(PerformanceAction::IntensityUp) == cc(83));
    CHECK(set.bindingFor(PerformanceAction::Panic) == cc(84));

    // Five switches, so the rest are deliberately left unmapped rather than doubled up.
    // Song navigation is among them: it only means anything once a setlist is loaded, and
    // a default that does nothing on most days teaches the performer to distrust the pedal.
    CHECK(!set.hasBinding(PerformanceAction::RepeatSection));
    CHECK(!set.hasBinding(PerformanceAction::IntensityDown));
    CHECK(!set.hasBinding(PerformanceAction::NextSong));
    CHECK(!set.hasBinding(PerformanceAction::PreviousSong));
    CHECK_EQ(set.mappedCount(), 5);
}

TEST("no default binding is a note, so playing keys cannot change section") {
    // The failure this prevents: a fresh install with only a keyboard attached, where
    // mid-keyboard notes silently steal sections because they were also the pedal
    // defaults. Nothing a performer plays should be able to trigger a performance action.
    MidiMappingSet set = MidiMappingSet::makeDefault();
    for (auto a : allPerformanceActions()) {
        const auto b = set.bindingFor(a);
        if (b.isValid()) CHECK(b.type == MidiBinding::Type::ControlChange);
    }
    for (int n = 0; n <= 127; ++n) {
        CHECK_EQ(static_cast<int>(set.actionFor(note(n))),
                 static_cast<int>(PerformanceAction::None));
    }
}

TEST("no two default actions share a switch") {
    MidiMappingSet set = MidiMappingSet::makeDefault();
    const auto actions = allPerformanceActions();

    for (std::size_t i = 0; i < actions.size(); ++i) {
        const auto a = set.bindingFor(actions[i]);
        if (!a.isValid()) continue;
        for (std::size_t j = i + 1; j < actions.size(); ++j) {
            const auto b = set.bindingFor(actions[j]);
            if (!b.isValid()) continue;
            CHECK(!(a == b));
        }
    }
}

TEST("the default pedal drives every mapped action from a press") {
    MidiMappingSet set = MidiMappingSet::makeDefault();
    double t = 0.0;
    for (int n = 80; n <= 84; ++n) {
        t += 200.0;
        CHECK(set.handleMessage(cc(n), true, t) != PerformanceAction::None);
    }
}

TEST("learning suppresses other actions while armed") {
    // Otherwise arming learn and then stomping the wrong pedal would fire that pedal's
    // action in the middle of a song.
    MidiMappingSet set = MidiMappingSet::makeDefault();
    set.beginLearn(PerformanceAction::IntensityDown);

    CHECK_EQ(static_cast<int>(set.handleMessage(cc(84), true, 0.0)),
             static_cast<int>(PerformanceAction::None));   // CC 84 is PANIC by default
    CHECK_EQ(static_cast<int>(set.lastDisplacedAction()),
             static_cast<int>(PerformanceAction::Panic));
    CHECK(set.bindingFor(PerformanceAction::IntensityDown) == cc(84));
    CHECK(!set.hasBinding(PerformanceAction::Panic));
}

TEST("binding and learning never allocate") {
    // handleMessage runs on JUCE's MIDI thread, and a MIDI Learn press writes a binding
    // from there. If the entry vector could grow, that write would allocate on a thread
    // that must stay bounded. Probed via the vector's storage address, which a resize
    // would change.
    MidiMappingSet set;
    const void* storage = set.bindingProbeAddress();

    for (auto a : allPerformanceActions()) {
        set.bind(a, cc(20 + static_cast<int>(a)));
        CHECK(set.bindingProbeAddress() == storage);
    }
    set.beginLearn(PerformanceAction::RepeatSection);
    set.handleMessage(cc(99), true, 0.0);
    CHECK(set.bindingProbeAddress() == storage);

    set.clearAll();
    CHECK(set.bindingProbeAddress() == storage);
}

// --- Persistence ------------------------------------------------------------------

TEST("a binding round-trips through text") {
    const MidiBinding bindings[] = {note(60), note(127, 16), cc(0), cc(64, 1)};
    for (const auto& b : bindings) {
        CHECK(MidiBinding::parse(b.toString()) == b);
    }
    CHECK(!MidiBinding::parse(MidiBinding{}.toString()).isValid());
}

TEST("corrupt binding text yields an invalid binding rather than throwing") {
    // A settings file edited by hand, or truncated by a crash, must not take the app down
    // at launch — least of all at soundcheck.
    const char* junk[] = {
        "", "none", "note", "note:", "note:0", "note:0:",
        "note:x:60", "note:0:y", "sysex:0:60", "note:0:-1", "note:0:128",
        "note:99:60", "note:-1:60", ":::", "note:0:60:extra",
        "9999999999999999999999:0:60", "note:0:9999999999999999999999",
    };
    for (const char* s : junk) {
        ++ghostband::test::g_checks;
        if (MidiBinding::parse(s).isValid()) {
            // Named explicitly: a bare CHECK inside the loop reports the expression but
            // not the input, which makes the failure a guessing game.
            ghostband::test::reportFailure(__FILE__, __LINE__,
                std::string("accepted junk binding text: \"") + s + '"');
        }
    }
}

TEST("a mapping set round-trips through text") {
    MidiMappingSet original = MidiMappingSet::makeDefault();
    original.bind(PerformanceAction::RepeatSection, cc(80, 2));

    const MidiMappingSet restored = MidiMappingSet::deserialise(original.serialise());
    for (auto a : allPerformanceActions()) {
        CHECK(restored.hasBinding(a) == original.hasBinding(a));
        CHECK(restored.bindingFor(a) == original.bindingFor(a));
    }
}

TEST("serialising omits cleared bindings") {
    MidiMappingSet set = MidiMappingSet::makeDefault();
    set.clear(PerformanceAction::Panic);

    const MidiMappingSet restored = MidiMappingSet::deserialise(set.serialise());
    CHECK(!restored.hasBinding(PerformanceAction::Panic));
    CHECK(restored.hasBinding(PerformanceAction::NextSection));
}

TEST("a corrupt settings file loads what it can instead of refusing") {
    // Losing one mapping is recoverable on stage; losing the whole pedal is not.
    const std::string text =
        "next_section=note:0:61\n"
        "this line is garbage\n"
        "=note:0:62\n"
        "not_an_action=note:0:63\n"
        "panic=wat\n"
        "\n"
        "previous_section=note:0:60\n";

    const MidiMappingSet set = MidiMappingSet::deserialise(text);
    CHECK(set.bindingFor(PerformanceAction::NextSection) == note(61));
    CHECK(set.bindingFor(PerformanceAction::PreviousSection) == note(60));
    CHECK(!set.hasBinding(PerformanceAction::Panic));
    CHECK(!set.hasBinding(PerformanceAction::ToggleAiBand));
}

TEST("deserialising nothing yields an empty set, not a default one") {
    // A performer who cleared every mapping must not find the defaults back next launch.
    const MidiMappingSet set = MidiMappingSet::deserialise("");
    for (auto a : allPerformanceActions()) CHECK(!set.hasBinding(a));
}

TEST("a file with CRLF line endings loads every mapping") {
    // Found on hardware, not in review. JUCE's File::replaceWithText defaults to writing
    // "\r\n", so the file GhostBand saved came back with a trailing CR on every line;
    // "81\r" parses as no number at all, and all five default mappings were dropped in
    // silence. The performer's pedal would have worked at soundcheck and been dead by the
    // show, with nothing on screen to explain it.
    const auto original = MidiMappingSet::makeDefault();

    std::string crlf;
    for (char c : original.serialise()) {
        if (c == '\n') crlf += '\r';
        crlf += c;
    }

    const auto restored = MidiMappingSet::deserialise(crlf);
    CHECK_EQ(restored.mappedCount(), original.mappedCount());
    for (auto a : allPerformanceActions()) {
        CHECK(restored.bindingFor(a) == original.bindingFor(a));
    }
}

TEST("a restored set still debounces from a clean slate") {
    MidiMappingSet set = MidiMappingSet::deserialise("next_section=note:0:61\n");
    CHECK_EQ(static_cast<int>(set.handleMessage(note(61), true, 0.0)),
             static_cast<int>(PerformanceAction::NextSection));
    CHECK_EQ(static_cast<int>(set.handleMessage(note(61), true, 1.0)),
             static_cast<int>(PerformanceAction::None));
}

// --- Naming -----------------------------------------------------------------------

TEST("every action name round-trips and is unique") {
    for (auto a : allPerformanceActions()) {
        CHECK_EQ(static_cast<int>(parsePerformanceAction(toString(a))),
                 static_cast<int>(a));
    }
    const auto actions = allPerformanceActions();
    for (std::size_t i = 0; i < actions.size(); ++i) {
        for (std::size_t j = i + 1; j < actions.size(); ++j) {
            CHECK(std::string(toString(actions[i])) != toString(actions[j]));
        }
    }
    CHECK_EQ(static_cast<int>(parsePerformanceAction("no_such_action")),
             static_cast<int>(PerformanceAction::None));
}

TEST("all performer-facing display strings are pure ASCII") {
    // Non-ASCII in a UI literal reached the screen as mojibake on a real run. The same
    // guard as EngineStateTests, extended to the mapping screen's labels.
    auto isAscii = [](const std::string& s) {
        for (char ch : s) {
            const auto c = static_cast<unsigned char>(ch);
            if (c < 0x20 || c > 0x7e) return false;
        }
        return true;
    };

    for (auto a : allPerformanceActions()) {
        CHECK(isAscii(toDisplayString(a)));
        CHECK(isAscii(toString(a)));
        CHECK(isAscii(MidiBinding{MidiBinding::Type::Note, 1, 60}.toString()));
    }
    CHECK(isAscii(toDisplayString(PerformanceAction::None)));
}

TEST_MAIN_END()
