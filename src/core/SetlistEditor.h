// GhostBand — live AI accompaniment for singer-songwriters.
// Copyright 2026 GhostBand contributors. Licensed under Apache-2.0.

#pragma once

#include "Setlist.h"

#include <cstddef>
#include <string>
#include <vector>

namespace ghostband::core {

/// Building a set, separately from performing one.
///
/// `SetlistController` runs a set: it tracks position, refuses to wrap, and reports gaps.
/// It deliberately has no way to change the running order, because changing it under a
/// performance is not a thing that should be possible. So the two are separate objects for
/// the same reason `SongEditor` is separate from `PerformanceEngine`, and this is the
/// editing half.
///
/// **Entries reference song files, never embedded songs.** That is `SetlistEntry`'s
/// decision (see Setlist.h) and this editor does not second-guess it: adding a song stores
/// its filename and caches its title for display. A set whose songs have since been edited
/// therefore plays the *current* arrangements, which is what a performer expects.
///
/// **The same song may appear twice.** Encores and reprises are real, and a set that
/// silently refused the second one would be wrong. Uniqueness is not enforced here — unlike
/// section names in `SongEditor`, where the constraint comes from prompt-slot allocation
/// rather than from taste.
///
/// Threading: control thread only.
class SetlistEditor {
public:
    SetlistEditor();
    explicit SetlistEditor(Setlist list);

    /// Replace everything being edited. Clears undo and the dirty flag — this is "opened a
    /// different setlist", not an edit.
    void reset(Setlist list);

    const Setlist& setlist() const noexcept { return list_; }

    void setName(const std::string& name);

    /// @name Entries
    ///
    /// Every mutator takes an index and returns false if it is out of range rather than
    /// clamping — the same rule as `SongEditor`, and for the same reason: a silent clamp
    /// edits the wrong row, and in a twelve-song set nobody notices until the gig.
    /// @{
    int size() const noexcept { return static_cast<int>(list_.entries.size()); }
    bool isEmpty() const noexcept { return list_.entries.empty(); }
    const SetlistEntry* entryAt(int index) const noexcept;

    /// Append a song to the end of the set.
    /// @param songFile   file name within the songs directory, not a path.
    /// @param cachedTitle title as of now, shown if the file later goes missing.
    /// @return the index it landed at, or -1 if `songFile` was empty.
    int addSong(const std::string& songFile, const std::string& cachedTitle);

    /// Insert at a position. `index` is where it ends up; out-of-range is refused.
    int insertSong(int index, const std::string& songFile, const std::string& cachedTitle);

    bool removeEntry(int index);

    /// Move one entry to a new position. `to` is the index it ends up at.
    ///
    /// Unlike `removeEntry`, this is allowed to empty nothing and can always be undone by
    /// the inverse move, so it is the safe way to reorder in front of an audience.
    bool moveEntry(int from, int to);

    /// Convenience for a two-button reorder UI. Both refuse at the ends rather than
    /// wrapping — a song that jumps from the closer to the opener because the performer
    /// clicked once too often is exactly the surprise `SetlistController` avoids.
    bool moveUp(int index);
    bool moveDown(int index);
    /// @}

    /// @name Undo
    /// @{
    bool canUndo() const noexcept { return !undo_.empty(); }
    bool undo();
    int undoDepth() const noexcept { return static_cast<int>(undo_.size()); }
    /// @}

    /// @name Saving
    /// @{
    bool isDirty() const noexcept { return dirty_; }
    void markSaved() noexcept { dirty_ = false; }
    /// @}

    /// Empty when the set can be performed, otherwise the reason from `Setlist::validate`.
    std::string validationError() const { return list_.validate(); }
    bool isPerformable() const { return list_.isValid(); }

private:
    /// Snapshot before a mutation. Called by every mutator, so no edit path can forget.
    void pushUndo();

    Setlist list_;
    std::vector<Setlist> undo_;
    bool dirty_ = false;

    /// Matches SongEditor::kMaxUndo. Setlists are smaller than songs, so if one depth has
    /// to be wrong it is not this one.
    static constexpr std::size_t kMaxUndo = 50;
};

} // namespace ghostband::core
