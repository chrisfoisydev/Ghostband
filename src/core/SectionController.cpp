// GhostBand — live AI accompaniment for singer-songwriters.
// Copyright 2026 GhostBand contributors. Licensed under Apache-2.0.

#include "SectionController.h"

#include <algorithm>

namespace ghostband::core {

void SectionController::setSong(const Song* song) noexcept {
    song_ = song;
    current_.store(0, std::memory_order_relaxed);
    previous_.store(0, std::memory_order_relaxed);
    snapTransition();
    changes_.store(0, std::memory_order_relaxed);
}

int SectionController::sectionCount() const noexcept {
    return song_ ? static_cast<int>(song_->sections.size()) : 0;
}

const SongSection* SectionController::sectionAt(int index) const noexcept {
    if (song_ == nullptr) return nullptr;
    if (index < 0 || index >= sectionCount()) return nullptr;
    return &song_->sections[static_cast<std::size_t>(index)];
}

const SongSection* SectionController::current() const noexcept {
    return sectionAt(currentIndex());
}

const SongSection* SectionController::peekNext() const noexcept {
    return sectionAt(currentIndex() + 1);
}

bool SectionController::isLastSection() const noexcept {
    const int count = sectionCount();
    return count == 0 || currentIndex() >= count - 1;
}

bool SectionController::goToNext() noexcept {
    return moveTo(currentIndex() + 1, /*allowSame=*/false);
}

bool SectionController::goToPrevious() noexcept {
    return moveTo(currentIndex() - 1, /*allowSame=*/false);
}

bool SectionController::goTo(int index) noexcept {
    return moveTo(index, /*allowSame=*/false);
}

bool SectionController::retriggerCurrent() noexcept {
    return moveTo(currentIndex(), /*allowSame=*/true);
}

bool SectionController::moveTo(int index, bool allowSame) noexcept {
    if (song_ == nullptr) return false;
    if (index < 0 || index >= sectionCount()) return false;

    const int from = currentIndex();
    if (index == from && !allowSame) return false;

    previous_.store(from, std::memory_order_relaxed);
    current_.store(index, std::memory_order_relaxed);

    // Transition time belongs to the section being entered: it describes how this section
    // arrives, which is what a performer means when they set it.
    const auto* entering = sectionAt(index);
    transition_total_ms_ = entering != nullptr ? static_cast<double>(entering->transitionMs) : 0.0;
    transition_elapsed_ms_ = 0.0;
    progress_.store(transition_total_ms_ > 0.0 ? 0.0f : 1.0f, std::memory_order_relaxed);

    changes_.fetch_add(1, std::memory_order_relaxed);
    return true;
}

void SectionController::tick(double deltaMs) noexcept {
    if (transition_total_ms_ <= 0.0) {
        progress_.store(1.0f, std::memory_order_relaxed);
        return;
    }
    if (progress_.load(std::memory_order_relaxed) >= 1.0f) return;

    transition_elapsed_ms_ += std::max(0.0, deltaMs);
    const double p = transition_elapsed_ms_ / transition_total_ms_;
    progress_.store(static_cast<float>(std::clamp(p, 0.0, 1.0)), std::memory_order_relaxed);
}

void SectionController::snapTransition() noexcept {
    transition_elapsed_ms_ = 0.0;
    transition_total_ms_ = 0.0;
    progress_.store(1.0f, std::memory_order_relaxed);
    previous_.store(currentIndex(), std::memory_order_relaxed);
}

} // namespace ghostband::core
