// GhostBand — live AI accompaniment for singer-songwriters.
// Copyright 2026 GhostBand contributors. Licensed under Apache-2.0.

#pragma once

#include "GhostBandConstants.h"
#include "Song.h"

#include <array>
#include <string>
#include <vector>

namespace ghostband::core {

/// Decides which section prompts occupy MRT2's encoded prompt slots.
///
/// **The constraint.** MRT2 holds `kMaxPrompts` (6) encoded prompts at once. A section
/// whose prompt is already resident changes instantly — the change is a blend-weight
/// write. A section whose prompt is not resident must wait for an async MusicCoCa encode,
/// which is exactly the mid-performance stall the whole design exists to avoid.
///
/// **The tension, stated plainly.** `IntensityMacro` also wants slots: it crossfades
/// sparse / base / full wordings of the current prompt, and that blend is the strongest
/// arrangement lever MRT2 offers. Sections and intensity are therefore competing for the
/// same six slots. There is no arrangement that gives both everything.
///
/// GhostBand resolves it by **reserving** slots for intensity and giving the rest to
/// sections. With the default reservation of 2, four distinct section prompts stay
/// resident — enough for Verse / Chorus / Bridge / Outro, which covers most songs — while
/// intensity keeps a sparse and a full variant of whichever section is playing.
///
/// A song exceeding the section capacity still works; it just cannot promise that every
/// change is stall-free. `isFullyResident()` reports which case a song is in, so the
/// editor can tell the performer *before* the gig rather than after.
class PromptSlotAllocator {
public:
    /// `reservedSlots` are withheld from sections for IntensityMacro's density variants.
    /// 0 gives sections every slot and disables intensity's prompt-blend term.
    explicit PromptSlotAllocator(int reservedSlots = 2) noexcept;

    /// Assign slots to the song's distinct prompts, in first-appearance order — so the
    /// sections a performer reaches first are the ones guaranteed to be resident.
    void setSong(const Song* song);

    int sectionCapacity() const noexcept { return section_capacity_; }
    int reservedSlots() const noexcept { return reserved_slots_; }
    /// First slot index reserved for intensity variants.
    int reservedBase() const noexcept { return section_capacity_; }

    /// Slot holding this section's prompt, or -1 if it is not currently resident.
    int slotFor(int sectionIndex) const;

    /// True when every section's prompt is resident, so no change in this song can stall.
    bool isFullyResident() const noexcept { return fully_resident_; }

    /// Ensure `sectionIndex` has a slot, evicting if necessary.
    ///
    /// Eviction never touches the currently-playing section, and otherwise drops the
    /// resident prompt belonging to the section furthest away in the arrangement — the
    /// one least likely to be reached next.
    ///
    /// @param changed set true when an encode is required, i.e. the caller must re-send
    ///        prompts and the change will not be instant.
    /// @return the slot index, or -1 if there is no song.
    int makeResident(int sectionIndex, int currentSectionIndex, bool* changed);

    /// Full slot contents in slot order; unused slots are empty strings. This is what gets
    /// handed to `IGenerationBackend::setTextPrompts`.
    const std::vector<std::string>& slotPrompts() const noexcept { return slots_; }

    /// Sections whose prompt is resident right now.
    std::vector<int> residentSections() const;

private:
    int findSlotForPrompt(const std::string& prompt) const;
    int chooseEvictionSlot(int currentSectionIndex) const;

    const Song* song_ = nullptr;
    int reserved_slots_ = 2;
    int section_capacity_ = static_cast<int>(kMaxPrompts) - 2;
    bool fully_resident_ = true;

    /// Slot -> prompt text. Size is always `kMaxPrompts`.
    std::vector<std::string> slots_;
};

/// Blend weights across all slots for a section transition.
///
/// Crossfades the outgoing section's slot into the incoming one as `progress` runs 0 -> 1.
/// Weights always sum to 1 so the conditioning strength stays constant and only its
/// character moves.
std::array<float, kMaxPrompts> sectionBlendWeights(int fromSlot, int toSlot,
                                                   float progress) noexcept;

} // namespace ghostband::core
