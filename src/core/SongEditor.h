// GhostBand — live AI accompaniment for singer-songwriters.
// Copyright 2026 GhostBand contributors. Licensed under Apache-2.0.

#pragma once

#include "PromptSlotAllocator.h"
#include "Song.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace ghostband::core {

/// Authoring a song, separately from performing one.
///
/// **Edits a copy, never the performing song.** `PerformanceEngine` holds a `const Song*`
/// and `SectionController` indexes into its sections; mutating that vector underneath them
/// would invalidate both mid-song. So the editor owns its own `Song` and the caller
/// commits explicitly — the same shape as APPLY PROMPT, and for the same reason: an edit
/// that reaches the band the instant a character is typed is not something to have on
/// stage.
///
/// **Undo is not a luxury here.** Deleting a section is one click and destroys work that
/// only exists in this file. A bounded snapshot stack costs a few kilobytes — songs are
/// small — and removes the only irreversible action in the editor.
///
/// Threading: control thread only.
class SongEditor {
public:
    SongEditor();
    explicit SongEditor(Song song);

    /// Replace everything being edited. Clears undo and the dirty flag — this is "opened a
    /// different song", not an edit.
    void reset(Song song);

    const Song& song() const noexcept { return song_; }

    /// @name Song-level fields
    /// @{
    void setTitle(const std::string& title);
    void setDefaultPrompt(const std::string& prompt);
    void setMasterLevelDb(float db);
    void setHarmonySource(HarmonySource source);
    /// @}

    /// @name Sections
    ///
    /// Every mutator takes an index and returns false if it is out of range, rather than
    /// clamping. A silent clamp would edit the wrong section, which in a song with eight
    /// verses is a mistake nobody notices until the gig.
    /// @{
    int sectionCount() const noexcept { return static_cast<int>(song_.sections.size()); }
    const SongSection* sectionAt(int index) const noexcept;

    /// Append a new section. Named "Section N" where N makes the name unique.
    int addSection();
    /// Insert a copy of an existing section directly after it — the fastest way to build
    /// Verse / Verse 2 / Verse 3, which is most of what authoring a song actually is.
    int duplicateSection(int index);

    /// Refused when it would leave the song with no sections: a song with none cannot be
    /// performed, and `Song::validate` would reject it on save anyway. Better to refuse
    /// the click than to accept an unsaveable state.
    bool removeSection(int index);

    /// Move a section to a new position. `to` is the index it ends up at.
    bool moveSection(int from, int to);

    bool setSectionName(int index, const std::string& name);
    bool setSectionPrompt(int index, const std::string& prompt);
    bool setSectionIntensity(int index, float intensity);
    bool setSectionAiEnabled(int index, bool enabled);
    bool setSectionTransitionMs(int index, int ms);
    bool setSectionNotes(int index, const std::string& notes);
    bool setSectionChords(int index, const std::vector<std::string>& chords);

    /// Set or clear a section's tempo.
    ///
    /// `std::nullopt` clears it, and clearing is a real musical choice rather than a
    /// missing value: a section with no tempo puts Song Map into `Manual`, where the chart
    /// moves only when the performer says so. See `core::SongMapPlayer`.
    ///
    /// A tempo outside 20..300 BPM is refused rather than clamped. Clamping a typo'd 1200
    /// to 300 would give the performer a tempo they did not ask for and no sign anything
    /// was wrong.
    bool setSectionTempoBpm(int index, std::optional<double> bpm);
    /// @}

    /// @name Undo
    /// @{
    bool canUndo() const noexcept { return !undo_.empty(); }
    /// @return false when there is nothing to undo.
    bool undo();
    int undoDepth() const noexcept { return static_cast<int>(undo_.size()); }
    /// @}

    /// @name Saving
    ///
    /// Dirty means "differs from the last save or open". The editor does not save
    /// anything itself — file I/O is the app layer's — but it is what can honestly answer
    /// "are you sure you want to close?".
    /// @{
    bool isDirty() const noexcept { return dirty_; }
    void markSaved() noexcept { dirty_ = false; }
    /// @}

    /// @name Warnings shown while editing, not at save time
    ///
    /// The brief's point about the prompt-slot ceiling (`KNOWN_ISSUES.md` §4) is that the
    /// performer should learn a song may stall *before* the gig. That means surfacing it
    /// in the editor, where it can still be fixed by rewording a prompt.
    /// @{
    /// Empty when the song is performable. Otherwise the reason, from `Song::validate`.
    std::string validationError() const { return song_.validate(); }
    bool isPerformable() const { return song_.isValid(); }

    /// True when every distinct prompt fits MRT2's slots, so no section change can stall.
    bool fitsPromptSlots() const { return song_.fitsPromptSlots(); }
    int distinctPromptCount() const { return static_cast<int>(song_.distinctPrompts().size()); }
    /// How many distinct prompts a song may hold before changes can stall.
    static int promptCapacity() noexcept;
    /// @}

private:
    /// Snapshot before a mutation. Called by every mutator, so no edit path can forget.
    void pushUndo();

    Song song_;
    std::vector<Song> undo_;
    bool dirty_ = false;

    /// Deep enough to cover a run of edits a performer would want to walk back, shallow
    /// enough that the memory stays irrelevant.
    static constexpr std::size_t kMaxUndo = 50;
};

} // namespace ghostband::core
