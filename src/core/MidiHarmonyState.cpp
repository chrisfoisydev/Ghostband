// GhostBand — live AI accompaniment for singer-songwriters.
// Copyright 2026 GhostBand contributors. Licensed under Apache-2.0.

#include "MidiHarmonyState.h"

namespace ghostband::core {

void MidiHarmonyState::noteOn(int note, int velocity) noexcept {
    if (!inRange(note)) return;

    // MIDI permits note-on with velocity 0 as a note-off, and controllers genuinely
    // differ. Normalising here means no caller has to remember.
    if (velocity <= 0) {
        noteOff(note);
        return;
    }

    const auto i = static_cast<std::size_t>(note);
    key_down_[i].store(true, std::memory_order_relaxed);
    sustained_[i].store(false, std::memory_order_relaxed);

    // Forwarded unconditionally, even if the note was already sounding under the pedal.
    // MRT2 treats each `set_note_on` as an onset, and re-striking a held key is a
    // deliberate re-articulation the player expects to hear.
    if (backend_ != nullptr) backend_->noteOn(note);

    updateSoundingCount();
}

void MidiHarmonyState::noteOff(int note) noexcept {
    if (!inRange(note)) return;

    const auto i = static_cast<std::size_t>(note);
    key_down_[i].store(false, std::memory_order_relaxed);

    if (pedal_.load(std::memory_order_relaxed)) {
        // Pedal down: the key is up but the note keeps sounding. Defer the note-off.
        sustained_[i].store(true, std::memory_order_relaxed);
        updateSoundingCount();
        return;
    }

    sustained_[i].store(false, std::memory_order_relaxed);
    if (backend_ != nullptr) backend_->noteOff(note);

    updateSoundingCount();
}

void MidiHarmonyState::setSustainPedal(bool down) noexcept {
    const bool was = pedal_.exchange(down, std::memory_order_relaxed);
    if (was == down) return;

    if (down) {
        updateSoundingCount();
        return;
    }

    // Pedal released: everything sustained now actually stops, unless the key is still
    // physically down.
    for (int n = 0; n < kNumMidiNotes; ++n) {
        const auto i = static_cast<std::size_t>(n);
        if (!sustained_[i].exchange(false, std::memory_order_relaxed)) continue;
        if (key_down_[i].load(std::memory_order_relaxed)) continue;
        if (backend_ != nullptr) backend_->noteOff(n);
    }
    updateSoundingCount();
}

void MidiHarmonyState::allNotesOff() noexcept {
    for (int n = 0; n < kNumMidiNotes; ++n) {
        const auto i = static_cast<std::size_t>(n);
        const bool was_down = key_down_[i].exchange(false, std::memory_order_relaxed);
        const bool was_sustained = sustained_[i].exchange(false, std::memory_order_relaxed);
        if ((was_down || was_sustained) && backend_ != nullptr) backend_->noteOff(n);
    }
    // The pedal is cleared too. A disconnect can strand it down, and a stuck pedal would
    // silently re-sustain the next note played.
    pedal_.store(false, std::memory_order_relaxed);
    updateSoundingCount();
}

bool MidiHarmonyState::isSounding(int note) const noexcept {
    if (!inRange(note)) return false;
    const auto i = static_cast<std::size_t>(note);
    return key_down_[i].load(std::memory_order_relaxed)
        || sustained_[i].load(std::memory_order_relaxed);
}

bool MidiHarmonyState::isKeyDown(int note) const noexcept {
    if (!inRange(note)) return false;
    return key_down_[static_cast<std::size_t>(note)].load(std::memory_order_relaxed);
}

std::vector<int> MidiHarmonyState::soundingNotes() const {
    std::vector<int> notes;
    notes.reserve(16); // more than any playable chord; avoids reallocation in practice
    for (int n = 0; n < kNumMidiNotes; ++n) {
        if (isSounding(n)) notes.push_back(n);
    }
    return notes;
}

void MidiHarmonyState::updateSoundingCount() noexcept {
    int count = 0;
    for (int n = 0; n < kNumMidiNotes; ++n) {
        if (isSounding(n)) ++count;
    }
    sounding_count_.store(count, std::memory_order_relaxed);
    generation_.fetch_add(1, std::memory_order_relaxed);
}

} // namespace ghostband::core
