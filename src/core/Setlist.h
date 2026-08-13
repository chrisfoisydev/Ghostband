// GhostBand — live AI accompaniment for singer-songwriters.
// Copyright 2026 GhostBand contributors. Licensed under Apache-2.0.

#pragma once

#include "Song.h"

#include <optional>
#include <string>
#include <vector>

namespace ghostband::core {

/// One slot in a setlist.
///
/// **References a song file rather than embedding the song.** Embedding would make a
/// setlist self-contained, which is tempting for stage reliability — but it also means
/// editing a song leaves the setlist playing a stale copy, and the performer gets no
/// signal that the two have diverged. A missing file is a loud, fixable failure; a
/// silently stale arrangement is neither.
struct SetlistEntry {
    /// File name within the songs directory. Not a path: a setlist that survives being
    /// copied to another machine is worth more than one that can point anywhere.
    std::string songFile;

    /// Title as of the last save. Cached purely so a missing song still shows a name
    /// instead of a filename — "Ghost Light (MISSING)" tells the performer what is gone.
    std::string cachedTitle;
};

/// An ordered list of songs for one performance.
struct Setlist {
    /// Bumped whenever the persisted shape changes. See Persistence.h.
    static constexpr int kSchemaVersion = 1;

    std::string name = "Untitled Setlist";
    std::vector<SetlistEntry> entries;

    /// Human-readable reason it cannot be performed. Empty when valid.
    std::string validate() const;
    bool isValid() const { return validate().empty(); }
};

/// Tracks which song of a set is loaded, and which entries failed to load.
///
/// **Nothing advances on its own**, for the same reason `SectionController` does not: the
/// performer decides when a song ends. The set also does not wrap — landing back on song
/// one after the closer would be a surprise nobody wants on stage.
///
/// Songs are owned here so that `currentSong()` can be handed to `SectionController`,
/// which requires a pointer that outlives it. The vector is never resized after `load()`,
/// so those pointers stay valid for the life of the set.
///
/// Threading: control thread only.
class SetlistController {
public:
    /// @param list   the setlist as saved.
    /// @param songs  parallel to `list.entries`. An empty optional means that file could
    ///               not be loaded; the entry is **kept** rather than dropped, so the gap
    ///               is visible in the running order instead of silently closing up.
    /// @return false if the sizes disagree, which is a programming error, not bad input.
    bool load(Setlist list, std::vector<std::optional<Song>> songs);
    void clear();

    const Setlist& setlist() const noexcept { return setlist_; }
    bool isLoaded() const noexcept { return !songs_.empty(); }
    int size() const noexcept { return static_cast<int>(songs_.size()); }

    int currentIndex() const noexcept { return current_; }
    /// Null when the current entry's file could not be loaded. Callers must handle that
    /// rather than assume a set is fully present.
    const Song* currentSong() const noexcept;
    const SetlistEntry* currentEntry() const noexcept;
    /// What a stage screen shows as "up next". Null at the end — the set does not wrap.
    const SetlistEntry* peekNext() const noexcept;

    /// @name Navigation — all return false if they cannot move
    /// @{
    bool goToNext() noexcept;
    bool goToPrevious() noexcept;
    bool goTo(int index) noexcept;
    /// @}

    bool isFirstSong() const noexcept { return current_ == 0; }
    bool isLastSong() const noexcept;

    /// @name Missing songs
    ///
    /// Surfaced rather than swallowed. A set that is 11 of 12 songs is still playable, but
    /// only if the performer finds out at soundcheck instead of between songs.
    /// @{
    bool hasMissingSongs() const noexcept { return missingCount() > 0; }
    int missingCount() const noexcept;
    /// Cached titles of the entries that failed to load, in running order.
    std::vector<std::string> missingTitles() const;
    /// @}

private:
    Setlist setlist_;
    std::vector<std::optional<Song>> songs_;
    int current_ = 0;
};

} // namespace ghostband::core
