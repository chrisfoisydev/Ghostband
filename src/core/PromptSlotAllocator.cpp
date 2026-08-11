// GhostBand — live AI accompaniment for singer-songwriters.
// Copyright 2026 GhostBand contributors. Licensed under Apache-2.0.

#include "PromptSlotAllocator.h"

#include <algorithm>
#include <cstdlib>
#include <limits>

namespace ghostband::core {

PromptSlotAllocator::PromptSlotAllocator(int reservedSlots) noexcept {
    reserved_slots_ = std::clamp(reservedSlots, 0, static_cast<int>(kMaxPrompts) - 1);
    section_capacity_ = static_cast<int>(kMaxPrompts) - reserved_slots_;
    slots_.assign(kMaxPrompts, {});
}

void PromptSlotAllocator::setSong(const Song* song) {
    song_ = song;
    slots_.assign(kMaxPrompts, {});
    fully_resident_ = true;

    if (song_ == nullptr) return;

    // First-appearance order, so the sections a performer reaches first are the ones
    // guaranteed resident. A song that overflows degrades at its tail, not its opening.
    const auto distinct = song_->distinctPrompts();
    const auto count = std::min<std::size_t>(distinct.size(),
                                             static_cast<std::size_t>(section_capacity_));
    for (std::size_t i = 0; i < count; ++i) {
        slots_[i] = distinct[i];
    }
    fully_resident_ = distinct.size() <= static_cast<std::size_t>(section_capacity_);
}

int PromptSlotAllocator::findSlotForPrompt(const std::string& prompt) const {
    if (prompt.empty()) return -1;
    for (int i = 0; i < section_capacity_; ++i) {
        if (slots_[static_cast<std::size_t>(i)] == prompt) return i;
    }
    return -1;
}

int PromptSlotAllocator::slotFor(int sectionIndex) const {
    if (song_ == nullptr) return -1;
    if (sectionIndex < 0 || sectionIndex >= static_cast<int>(song_->sections.size())) return -1;

    const auto prompt = song_->sections[static_cast<std::size_t>(sectionIndex)]
                            .resolvedPrompt(song_->defaultStylePrompt);
    return findSlotForPrompt(prompt);
}

int PromptSlotAllocator::chooseEvictionSlot(int currentSectionIndex) const {
    // Prefer an empty slot.
    for (int i = 0; i < section_capacity_; ++i) {
        if (slots_[static_cast<std::size_t>(i)].empty()) return i;
    }

    // Otherwise drop the resident prompt belonging to the section furthest from the one
    // playing — the least likely to be reached next. The current section's slot is never
    // a candidate: evicting the prompt that is sounding would stall the band immediately.
    const int protect = slotFor(currentSectionIndex);

    int worst_slot = -1;
    int worst_distance = -1;
    for (int slot = 0; slot < section_capacity_; ++slot) {
        if (slot == protect) continue;

        // Nearest section using this slot's prompt: a prompt shared by several sections
        // is only as evictable as its closest user.
        int nearest = std::numeric_limits<int>::max();
        for (int s = 0; s < static_cast<int>(song_->sections.size()); ++s) {
            if (slotFor(s) != slot) continue;
            nearest = std::min(nearest, std::abs(s - currentSectionIndex));
        }
        if (nearest == std::numeric_limits<int>::max()) return slot; // unused: free to take

        if (nearest > worst_distance) {
            worst_distance = nearest;
            worst_slot = slot;
        }
    }
    // Everything is protected only when capacity is 1; fall back to slot 0.
    return worst_slot >= 0 ? worst_slot : 0;
}

int PromptSlotAllocator::makeResident(int sectionIndex, int currentSectionIndex,
                                      bool* changed) {
    if (changed != nullptr) *changed = false;
    if (song_ == nullptr) return -1;
    if (sectionIndex < 0 || sectionIndex >= static_cast<int>(song_->sections.size())) return -1;

    const auto prompt = song_->sections[static_cast<std::size_t>(sectionIndex)]
                            .resolvedPrompt(song_->defaultStylePrompt);

    if (const int existing = findSlotForPrompt(prompt); existing >= 0) {
        return existing; // already resident — this is the instant path
    }

    const int slot = chooseEvictionSlot(currentSectionIndex);
    slots_[static_cast<std::size_t>(slot)] = prompt;
    if (changed != nullptr) *changed = true;
    return slot;
}

std::vector<int> PromptSlotAllocator::residentSections() const {
    std::vector<int> out;
    if (song_ == nullptr) return out;
    for (int s = 0; s < static_cast<int>(song_->sections.size()); ++s) {
        if (slotFor(s) >= 0) out.push_back(s);
    }
    return out;
}

std::array<float, kMaxPrompts> sectionBlendWeights(int fromSlot, int toSlot,
                                                   float progress) noexcept {
    std::array<float, kMaxPrompts> w{};
    w.fill(0.0f);

    const auto valid = [](int s) { return s >= 0 && s < static_cast<int>(kMaxPrompts); };
    if (!valid(toSlot)) {
        // Nothing sensible to point at. Leaving all weights at zero would silently drop
        // the style conditioning, so fall back to the first slot.
        w[0] = 1.0f;
        return w;
    }

    const float p = std::clamp(progress, 0.0f, 1.0f);
    if (!valid(fromSlot) || fromSlot == toSlot) {
        w[static_cast<std::size_t>(toSlot)] = 1.0f;
        return w;
    }

    w[static_cast<std::size_t>(fromSlot)] = 1.0f - p;
    w[static_cast<std::size_t>(toSlot)] = p;
    return w;
}

} // namespace ghostband::core
