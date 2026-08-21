// GhostBand — live AI accompaniment for singer-songwriters.
// Copyright 2026 GhostBand contributors. Licensed under Apache-2.0.

#include "core/SongMapPlayer.h"

#include "core/ChordParser.h"

#include <algorithm>

namespace ghostband::core {

const char* toString(SongMapAdvance a) noexcept {
    switch (a) {
        case SongMapAdvance::Manual: return "Manual";
        case SongMapAdvance::Clock:  return "Clock";
    }
    return "?";
}

void SongMapPlayer::setConfig(Config config) noexcept {
    // A beatsPerChord of zero would make msPerChord zero and the clock advance infinitely
    // in one tick. Clamped rather than asserted: this is reachable from a UI field.
    config.beatsPerChord = std::max(1, config.beatsPerChord);
    config_ = config;
    rephase();
}

bool SongMapPlayer::loadSection(const SongSection& section) {
    slots_.clear();
    rejected_.clear();
    index_ = 0;
    running_ = false;
    elapsed_in_chord_ms_ = 0.0;

    slots_.reserve(section.chordProgression.size());
    for (int i = 0; i < static_cast<int>(section.chordProgression.size()); ++i) {
        const auto& symbol = section.chordProgression[static_cast<std::size_t>(i)];

        Slot slot;
        slot.symbol = symbol;
        if (const auto chord = parseChord(symbol); chord.has_value()) {
            slot.notes = chordNotes(*chord, config_.octave);
        } else {
            // Keeps its slot with no notes. See the class comment: dropping it would shift
            // every later chord earlier and put the section out of step with the chart.
            rejected_.push_back(i);
        }
        slots_.push_back(std::move(slot));
    }

    // A tempo the performer actually entered, or no clock at all. Inventing 120 BPM here
    // would be the app deciding how fast the song goes.
    tempo_bpm_ = section.tempoBpm.value_or(0.0);
    mode_ = tempo_bpm_ > 0.0 ? SongMapAdvance::Clock : SongMapAdvance::Manual;

    active_ = std::any_of(slots_.begin(), slots_.end(),
                          [](const Slot& s) { return !s.notes.empty(); });
    return active_;
}

double SongMapPlayer::msPerChord() const noexcept {
    if (mode_ != SongMapAdvance::Clock || tempo_bpm_ <= 0.0) return 0.0;
    const double ms_per_beat = 60'000.0 / tempo_bpm_;
    return ms_per_beat * static_cast<double>(config_.beatsPerChord);
}

void SongMapPlayer::start() noexcept {
    if (!active_) return;
    running_ = true;
    rephase();
}

void SongMapPlayer::stop() noexcept { running_ = false; }

void SongMapPlayer::rephase() noexcept { elapsed_in_chord_ms_ = 0.0; }

bool SongMapPlayer::tick(double deltaMs) noexcept {
    if (!active_ || !running_ || mode_ != SongMapAdvance::Clock) return false;
    if (slots_.size() < 2) return false;       // one chord has nowhere to go

    const double per_chord = msPerChord();
    if (per_chord <= 0.0) return false;

    // Negative deltas are not a thing a clock should have to reason about, and a wall-clock
    // delta can go backwards if the system time is adjusted mid-set.
    if (deltaMs <= 0.0) return false;

    elapsed_in_chord_ms_ += deltaMs;
    if (elapsed_in_chord_ms_ < per_chord) return false;

    // A stalled message thread can hand over a delta covering several chords. Stepping
    // through them one at a time keeps the index right; only the last one is audible,
    // which is the correct outcome — the missed chords are already in the past.
    int steps = 0;
    while (elapsed_in_chord_ms_ >= per_chord) {
        elapsed_in_chord_ms_ -= per_chord;
        index_ = (index_ + 1) % static_cast<int>(slots_.size());
        ++steps;

        // A delta of minutes would otherwise spin here. The section is being resumed after
        // something went badly wrong; landing anywhere sensible beats burning the thread.
        if (steps > 1024) {
            elapsed_in_chord_ms_ = 0.0;
            break;
        }
    }
    return true;
}

bool SongMapPlayer::advance() noexcept {
    if (!active_ || slots_.empty()) return false;
    index_ = (index_ + 1) % static_cast<int>(slots_.size());
    // Re-phase, so a chord reached by a deliberate press gets its full length rather than
    // whatever was left of the previous one. Without this, pressing just before a clock
    // boundary would flick past the chord the performer was asking for.
    rephase();
    return true;
}

bool SongMapPlayer::retreat() noexcept {
    if (!active_ || slots_.empty()) return false;
    const int count = static_cast<int>(slots_.size());
    index_ = (index_ - 1 + count) % count;
    rephase();
    return true;
}

void SongMapPlayer::restart() noexcept {
    index_ = 0;
    rephase();
}

const std::string& SongMapPlayer::currentSymbol() const {
    if (slots_.empty()) return empty_symbol_;
    return slots_[static_cast<std::size_t>(index_)].symbol;
}

const std::string& SongMapPlayer::nextSymbol() const {
    if (slots_.empty()) return empty_symbol_;
    const int next = (index_ + 1) % static_cast<int>(slots_.size());
    return slots_[static_cast<std::size_t>(next)].symbol;
}

const std::vector<int>& SongMapPlayer::currentNotes() const {
    if (slots_.empty()) return empty_notes_;
    return slots_[static_cast<std::size_t>(index_)].notes;
}

double SongMapPlayer::progress() const noexcept {
    const double per_chord = msPerChord();
    if (per_chord <= 0.0) return 0.0;
    return std::clamp(elapsed_in_chord_ms_ / per_chord, 0.0, 1.0);
}

} // namespace ghostband::core
