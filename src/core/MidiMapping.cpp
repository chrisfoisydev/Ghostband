// GhostBand — live AI accompaniment for singer-songwriters.
// Copyright 2026 GhostBand contributors. Licensed under Apache-2.0.

#include "MidiMapping.h"

#include <algorithm>
#include <sstream>

namespace ghostband::core {

const char* toString(PerformanceAction a) noexcept {
    switch (a) {
        case PerformanceAction::None:            return "none";
        case PerformanceAction::NextSection:     return "next_section";
        case PerformanceAction::PreviousSection: return "previous_section";
        case PerformanceAction::RepeatSection:   return "repeat_section";
        case PerformanceAction::ToggleAiBand:    return "toggle_ai_band";
        case PerformanceAction::Panic:           return "panic";
        case PerformanceAction::IntensityUp:     return "intensity_up";
        case PerformanceAction::IntensityDown:   return "intensity_down";
        case PerformanceAction::NextSong:        return "next_song";
        case PerformanceAction::PreviousSong:    return "previous_song";
    }
    return "none";
}

const char* toDisplayString(PerformanceAction a) noexcept {
    switch (a) {
        case PerformanceAction::None:            return "-";
        case PerformanceAction::NextSection:     return "NEXT SECTION";
        case PerformanceAction::PreviousSection: return "PREVIOUS SECTION";
        case PerformanceAction::RepeatSection:   return "REPEAT SECTION";
        case PerformanceAction::ToggleAiBand:    return "AI BAND ON/OFF";
        case PerformanceAction::Panic:           return "PANIC";
        case PerformanceAction::IntensityUp:     return "INTENSITY +";
        case PerformanceAction::IntensityDown:   return "INTENSITY -";
        case PerformanceAction::NextSong:        return "NEXT SONG";
        case PerformanceAction::PreviousSong:    return "PREVIOUS SONG";
    }
    return "-";
}

PerformanceAction parsePerformanceAction(const std::string& s) noexcept {
    for (auto a : allPerformanceActions()) {
        if (s == toString(a)) return a;
    }
    return PerformanceAction::None;
}

std::vector<PerformanceAction> allPerformanceActions() {
    return {
        PerformanceAction::PreviousSection,
        PerformanceAction::NextSection,
        PerformanceAction::RepeatSection,
        PerformanceAction::PreviousSong,
        PerformanceAction::NextSong,
        PerformanceAction::ToggleAiBand,
        PerformanceAction::IntensityUp,
        PerformanceAction::IntensityDown,
        PerformanceAction::Panic,
    };
}

bool MidiBinding::operator==(const MidiBinding& other) const noexcept {
    return type == other.type && channel == other.channel && number == other.number;
}

bool MidiBinding::matches(const MidiBinding& incoming) const noexcept {
    if (!isValid() || !incoming.isValid()) return false;
    if (type != incoming.type) return false;
    if (number != incoming.number) return false;
    // Channel 0 on either side means "any". A stored any-channel binding is the common
    // case; an incoming any-channel message would be a caller that did not know.
    if (channel != 0 && incoming.channel != 0 && channel != incoming.channel) return false;
    return true;
}

std::string MidiBinding::toString() const {
    if (!isValid()) return "none";
    std::ostringstream os;
    os << (type == Type::Note ? "note" : "cc") << ':' << channel << ':' << number;
    return os.str();
}

namespace {

/// Strict decimal parse: the whole token must be digits.
///
/// `std::stoi` stops at the first non-digit, so it reads "60:extra" as 60 and would turn
/// a malformed settings line into a plausible-looking binding. A pedal that maps to
/// something the performer never typed is worse than one that reports itself unmapped.
bool parseWholeNumber(const std::string& token, int& out) noexcept {
    if (token.empty()) return false;
    for (char c : token) {
        if (c < '0' || c > '9') return false;
    }
    try {
        std::size_t consumed = 0;
        const int value = std::stoi(token, &consumed);
        if (consumed != token.size()) return false;
        out = value;
        return true;
    } catch (...) {
        return false; // out of range — a corrupt file must not throw into a live app
    }
}

} // namespace

MidiBinding MidiBinding::parse(const std::string& s) noexcept {
    MidiBinding b;
    if (s.empty() || s == "none") return b;

    std::istringstream is(s);
    std::string type_token, channel_token, number_token;
    if (!std::getline(is, type_token, ':')) return b;
    if (!std::getline(is, channel_token, ':')) return b;
    if (!std::getline(is, number_token)) return b;

    if (type_token == "note") b.type = Type::Note;
    else if (type_token == "cc") b.type = Type::ControlChange;
    else return {};

    if (!parseWholeNumber(channel_token, b.channel)) return {};
    if (!parseWholeNumber(number_token, b.number)) return {};
    if (b.number < 0 || b.number > 127 || b.channel < 0 || b.channel > 16) return {};
    return b;
}

MidiMappingSet::MidiMappingSet() {
    // One entry per action, allocated once here. Nothing after construction resizes the
    // vector, which is what lets the MIDI thread run `handleMessage` — including the
    // bind() a MIDI Learn press performs — without allocating.
    for (auto a : allPerformanceActions()) {
        entries_.push_back({a, MidiBinding{}, kNeverFired});
    }
}

MidiMappingSet MidiMappingSet::makeDefault() {
    // Five switches, on general-purpose CCs rather than notes.
    //
    // Notes were the obvious first choice and are wrong: a footswitch bound to note 61 is
    // indistinguishable from a performer playing C#4, so a fresh install with only a
    // keyboard attached would find mid-keyboard notes silently changing section. CC 80-84
    // are undefined in the MIDI spec, are what footswitch-to-MIDI boxes most often send,
    // and are never produced by playing keys. Notes remain bindable via MIDI Learn for
    // controllers that only send them - see KNOWN_ISSUES.md §14.
    MidiMappingSet set;
    set.bind(PerformanceAction::PreviousSection, {MidiBinding::Type::ControlChange, 0, 80});
    set.bind(PerformanceAction::NextSection,     {MidiBinding::Type::ControlChange, 0, 81});
    set.bind(PerformanceAction::ToggleAiBand,    {MidiBinding::Type::ControlChange, 0, 82});
    set.bind(PerformanceAction::IntensityUp,     {MidiBinding::Type::ControlChange, 0, 83});
    set.bind(PerformanceAction::Panic,           {MidiBinding::Type::ControlChange, 0, 84});
    return set;
}

MidiMappingSet::Entry* MidiMappingSet::find(PerformanceAction action) {
    for (auto& e : entries_) {
        if (e.action == action) return &e;
    }
    return nullptr;
}

const MidiMappingSet::Entry* MidiMappingSet::find(PerformanceAction action) const {
    for (const auto& e : entries_) {
        if (e.action == action) return &e;
    }
    return nullptr;
}

void MidiMappingSet::bind(PerformanceAction action, const MidiBinding& binding) {
    if (action == PerformanceAction::None || !binding.isValid()) return;

    // Steal the binding from whoever had it. Two actions on one switch is never what
    // anyone means, and silently leaving both would make the pedal ambiguous.
    displaced_ = PerformanceAction::None;
    for (auto& e : entries_) {
        if (e.action != action && e.binding == binding) {
            displaced_ = e.action;
            e.binding = {};
        }
    }

    if (auto* existing = find(action)) {
        existing->binding = binding;
        existing->last_fired_ms = kNeverFired;
    }
}

void MidiMappingSet::clear(PerformanceAction action) {
    if (auto* e = find(action)) e->binding = {};
}

void MidiMappingSet::clearAll() {
    for (auto& e : entries_) {
        e.binding = {};
        e.last_fired_ms = kNeverFired;
    }
}

MidiBinding MidiMappingSet::bindingFor(PerformanceAction action) const {
    const auto* e = find(action);
    return e != nullptr ? e->binding : MidiBinding{};
}

bool MidiMappingSet::hasBinding(PerformanceAction action) const {
    return bindingFor(action).isValid();
}

int MidiMappingSet::mappedCount() const noexcept {
    int n = 0;
    for (const auto& e : entries_) {
        if (e.binding.isValid()) ++n;
    }
    return n;
}

PerformanceAction MidiMappingSet::actionFor(const MidiBinding& incoming) const {
    for (const auto& e : entries_) {
        if (e.binding.matches(incoming)) return e.action;
    }
    return PerformanceAction::None;
}

void MidiMappingSet::beginLearn(PerformanceAction action) noexcept {
    learning_ = action;
    displaced_ = PerformanceAction::None;
}

void MidiMappingSet::cancelLearn() noexcept { learning_ = PerformanceAction::None; }

PerformanceAction MidiMappingSet::handleMessage(const MidiBinding& incoming, bool pressed,
                                                double timeMs) {
    // Releases never trigger. A footswitch sends one message down and another up; acting
    // on both would advance two sections per stomp.
    if (!pressed) return PerformanceAction::None;
    if (!incoming.isValid()) return PerformanceAction::None;

    if (learning_ != PerformanceAction::None) {
        const auto target = learning_;
        bind(target, incoming);
        learning_ = PerformanceAction::None;
        // The press that teaches a mapping must not also perform it — otherwise mapping
        // PANIC silences the band as a side effect of setting it up.
        return PerformanceAction::None;
    }

    for (auto& e : entries_) {
        if (!e.binding.matches(incoming)) continue;

        // Mechanical bounce. A double-fire on NEXT skips a section mid-song, which the
        // performer cannot undo gracefully in front of an audience.
        if (timeMs - e.last_fired_ms < debounce_ms_) return PerformanceAction::None;

        e.last_fired_ms = timeMs;
        return e.action;
    }
    return PerformanceAction::None;
}

std::string MidiMappingSet::serialise() const {
    std::ostringstream os;
    for (const auto& e : entries_) {
        if (!e.binding.isValid()) continue;
        os << toString(e.action) << '=' << e.binding.toString() << '\n';
    }
    return os.str();
}

MidiMappingSet MidiMappingSet::deserialise(const std::string& text) {
    MidiMappingSet set;
    std::istringstream is(text);
    std::string line;

    while (std::getline(is, line)) {
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;   // tolerate junk rather than refusing to load

        const auto action = parsePerformanceAction(line.substr(0, eq));
        const auto binding = MidiBinding::parse(line.substr(eq + 1));
        if (action != PerformanceAction::None && binding.isValid()) {
            set.bind(action, binding);
        }
    }
    return set;
}

} // namespace ghostband::core
