// GhostBand — live AI accompaniment for singer-songwriters.
// Copyright 2026 GhostBand contributors. Licensed under Apache-2.0.

#pragma once

#include "IGenerationBackend.h"
#include "IntensityMacro.h"
#include "PromptSlotAllocator.h"
#include "SectionController.h"
#include "Song.h"

namespace ghostband::core {

/// Drives the band from a song: which section is playing, what it should sound like, and
/// how one section becomes the next.
///
/// This is where `SectionController`, `PromptSlotAllocator` and `IntensityMacro` meet a
/// backend. Keeping it in `core` means the whole performance flow — section changes,
/// transitions, prompt residency, per-section intensity — is testable against
/// `NullBackend` without an audio device, a model, or a Mac.
///
/// **Slot policy while a song is loaded: sections take every prompt slot.**
///
/// Free play lets `IntensityMacro` spend three slots on sparse/base/full wordings, which
/// is its strongest lever. That cannot survive a loaded song: sections need those slots
/// resident or their changes stall, and re-encoding intensity's variants on every section
/// change would reintroduce exactly the stall the design exists to remove.
///
/// So in song mode intensity keeps only its parameter terms (`drumless`, `cfg_drums`,
/// `cfg_musiccoca`, `temperature`) and sections own the prompt blend. Intensity is
/// genuinely weaker here, and that is the acknowledged price of instant section changes.
/// It is a reasonable trade because a section's *prompt text* already expresses its
/// density — a performer writes "sparse atmospheric piano" for a verse — so the arrangement
/// contrast that matters most is carried by the sections themselves.
///
/// Threading: control thread. `tick()` runs on the same ~100 Hz timer that drives the UI.
class PerformanceEngine {
public:
    PerformanceEngine();

    /// Null is legal — the engine then tracks state without driving anything, which is
    /// what makes it testable and what keeps it safe before a model has loaded.
    void setBackend(IGenerationBackend* backend) noexcept { backend_ = backend; }

    /// Load a song, encode its section prompts, and settle on the first section.
    /// The song must outlive the engine. Returns false if the song is invalid.
    bool loadSong(const Song* song);
    void unloadSong();
    bool hasSong() const noexcept { return controller_.song() != nullptr; }
    const Song* song() const noexcept { return controller_.song(); }

    /// @name Section navigation. Each applies the new section immediately.
    /// @{
    bool goToNext();
    bool goToPrevious();
    bool goTo(int index);
    /// Repeat: re-enter the current section, re-running its transition.
    bool retriggerCurrent();
    /// @}

    /// Advance the transition and push the resulting blend to the backend.
    void tick(double deltaMs);

    /// True when the last section change needed a MusicCoCa encode, i.e. it was not
    /// instant. Surfaced rather than hidden so the diagnostics can show it and the song
    /// editor can warn about it.
    bool lastChangeRequiredEncode() const noexcept { return last_change_encoded_; }
    std::uint64_t encodedChangeCount() const noexcept { return encoded_changes_; }

    /// Whether the current section wants the band audible at all. A section with the band
    /// off is a musical choice — a solo verse — so the caller mutes its output stage
    /// rather than stopping generation, which would cost a restart on the way back.
    bool currentSectionAiEnabled() const noexcept;

    SectionController& sections() noexcept { return controller_; }
    const SectionController& sections() const noexcept { return controller_; }
    IntensityMacro& intensity() noexcept { return intensity_; }
    const IntensityMacro& intensity() const noexcept { return intensity_; }
    const PromptSlotAllocator& allocator() const noexcept { return allocator_; }

    /// Blend weights currently in force. Exposed for diagnostics and tests.
    std::array<float, kMaxPrompts> currentBlendWeights() const noexcept { return weights_; }

private:
    void applyCurrentSection(bool snapTransition);
    void pushPrompts();
    void pushBlend();

    IGenerationBackend* backend_ = nullptr;
    SectionController controller_;
    /// 0 reserved slots: in song mode sections take all six. See the class comment.
    PromptSlotAllocator allocator_{0};
    IntensityMacro intensity_;

    std::array<float, kMaxPrompts> weights_{};
    int from_slot_ = 0;
    int to_slot_ = 0;
    bool last_change_encoded_ = false;
    std::uint64_t encoded_changes_ = 0;
};

} // namespace ghostband::core
