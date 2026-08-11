// GhostBand — live AI accompaniment for singer-songwriters.
// Copyright 2026 GhostBand contributors. Licensed under Apache-2.0.

#include "PerformanceEngine.h"

#include "Logging.h"

namespace ghostband::core {

PerformanceEngine::PerformanceEngine() {
    weights_.fill(0.0f);
    weights_[0] = 1.0f;
}

bool PerformanceEngine::loadSong(const Song* song) {
    if (song == nullptr) {
        unloadSong();
        return false;
    }
    if (const auto problem = song->validate(); !problem.empty()) {
        Logger::instance().error(LogCategory::Performance, "song is not performable",
                                 {{"title", song->title}, {"reason", problem}});
        return false;
    }

    allocator_.setSong(song);
    controller_.setSong(song);

    // Encode every resident section prompt up front, while nobody is playing. This is the
    // whole point of the design: paying MusicCoCa here means a section change later is
    // only a blend-weight write.
    pushPrompts();
    applyCurrentSection(/*snapTransition=*/true);

    Logger::instance().info(LogCategory::Performance, "song loaded",
                            {{"title", song->title},
                             {"sections", std::to_string(song->sections.size())},
                             {"distinct_prompts", std::to_string(song->distinctPrompts().size())},
                             {"all_resident", allocator_.isFullyResident() ? "yes" : "no"}});

    if (!allocator_.isFullyResident()) {
        // Said plainly and early. The performer can still play the song; they just need to
        // know one of its changes may hesitate, and they should hear about it now rather
        // than on stage.
        Logger::instance().warn(LogCategory::Performance,
                                "song has more distinct prompts than prompt slots - "
                                "some section changes will not be instant",
                                {{"title", song->title}});
    }
    return true;
}

void PerformanceEngine::unloadSong() {
    controller_.setSong(nullptr);
    allocator_.setSong(nullptr);
    last_change_encoded_ = false;
    Logger::instance().info(LogCategory::Performance, "song unloaded");
}

bool PerformanceEngine::currentSectionAiEnabled() const noexcept {
    const auto* section = controller_.current();
    return section == nullptr || section->aiEnabled;
}

bool PerformanceEngine::goToNext() {
    if (!controller_.goToNext()) return false;
    applyCurrentSection(false);
    return true;
}

bool PerformanceEngine::goToPrevious() {
    if (!controller_.goToPrevious()) return false;
    applyCurrentSection(false);
    return true;
}

bool PerformanceEngine::goTo(int index) {
    if (!controller_.goTo(index)) return false;
    applyCurrentSection(false);
    return true;
}

bool PerformanceEngine::retriggerCurrent() {
    if (!controller_.retriggerCurrent()) return false;
    applyCurrentSection(false);
    return true;
}

void PerformanceEngine::applyCurrentSection(bool snapTransition) {
    const auto* section = controller_.current();
    if (section == nullptr) return;

    // Where the crossfade is coming from, captured before residency may move things.
    from_slot_ = allocator_.slotFor(controller_.previousIndex());

    bool needs_encode = false;
    to_slot_ = allocator_.makeResident(controller_.currentIndex(),
                                       controller_.previousIndex(), &needs_encode);
    last_change_encoded_ = needs_encode;
    if (needs_encode) {
        ++encoded_changes_;
        // The eviction may have taken the slot we were crossfading from. Fading out of a
        // slot that now holds a different prompt would be worse than not fading at all.
        if (from_slot_ == to_slot_) from_slot_ = to_slot_;
        pushPrompts();
        Logger::instance().warn(LogCategory::Performance,
                                "section change required a prompt encode - not instant",
                                {{"section", section->name}});
    }

    if (snapTransition) controller_.snapTransition();

    // Per-section intensity. The live knob is overwritten by design: the section's value
    // is what the performer authored for this part of the song, and a stale knob position
    // from the previous section would silently override it.
    intensity_.setIntensity(section->aiIntensity);
    if (backend_ != nullptr) intensity_.applyParametersTo(*backend_);

    pushBlend();

    Logger::instance().info(LogCategory::Performance, "section change",
                            {{"section", section->name},
                             {"index", std::to_string(controller_.currentIndex())},
                             {"intensity", std::to_string(section->aiIntensity)},
                             {"instant", needs_encode ? "no" : "yes"}});
}

void PerformanceEngine::pushPrompts() {
    if (backend_ == nullptr) return;

    const auto& prompts = allocator_.slotPrompts();
    // Weights go alongside the prompts so the backend never sees an encoded set without a
    // blend; sending prompts alone would leave the previous weights pointing at slots
    // whose contents just changed.
    const std::vector<float> weights(weights_.begin(), weights_.end());
    backend_->setTextPrompts(prompts, weights);
}

void PerformanceEngine::pushBlend() {
    weights_ = sectionBlendWeights(from_slot_, to_slot_, controller_.transitionProgress());
    if (backend_ != nullptr) {
        backend_->setBlendWeights(weights_.data(), static_cast<int>(weights_.size()));
    }
}

void PerformanceEngine::tick(double deltaMs) {
    if (!hasSong()) return;

    const bool was_transitioning = controller_.isTransitioning();
    controller_.tick(deltaMs);

    // Only touch the backend while something is actually moving. A settled section should
    // not cost a parameter write every timer tick.
    if (was_transitioning) pushBlend();
}

} // namespace ghostband::core
