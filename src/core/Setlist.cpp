// GhostBand — live AI accompaniment for singer-songwriters.
// Copyright 2026 GhostBand contributors. Licensed under Apache-2.0.

#include "Setlist.h"

namespace ghostband::core {

std::string Setlist::validate() const {
    if (entries.empty()) return "A setlist needs at least one song.";
    for (std::size_t i = 0; i < entries.size(); ++i) {
        if (entries[i].songFile.empty()) {
            return "Entry " + std::to_string(i + 1) + " has no song file.";
        }
    }
    return {};
}

bool SetlistController::load(Setlist list, std::vector<std::optional<Song>> songs) {
    if (list.entries.size() != songs.size()) return false;

    setlist_ = std::move(list);
    songs_ = std::move(songs);
    current_ = 0;
    return true;
}

void SetlistController::clear() {
    setlist_ = {};
    songs_.clear();
    current_ = 0;
}

const Song* SetlistController::currentSong() const noexcept {
    if (current_ < 0 || current_ >= size()) return nullptr;
    const auto& slot = songs_[static_cast<std::size_t>(current_)];
    return slot.has_value() ? &slot.value() : nullptr;
}

const SetlistEntry* SetlistController::currentEntry() const noexcept {
    if (current_ < 0 || current_ >= size()) return nullptr;
    return &setlist_.entries[static_cast<std::size_t>(current_)];
}

const SetlistEntry* SetlistController::peekNext() const noexcept {
    if (current_ + 1 >= size()) return nullptr;
    return &setlist_.entries[static_cast<std::size_t>(current_ + 1)];
}

bool SetlistController::goToNext() noexcept { return goTo(current_ + 1); }
bool SetlistController::goToPrevious() noexcept { return goTo(current_ - 1); }

bool SetlistController::goTo(int index) noexcept {
    // Refusing rather than clamping. Clamping at the last song would make a stray extra
    // press look like it worked, which is exactly when the performer stops trusting the
    // pedal.
    if (index < 0 || index >= size()) return false;
    if (index == current_) return false;
    current_ = index;
    return true;
}

bool SetlistController::isLastSong() const noexcept {
    return size() > 0 && current_ == size() - 1;
}

int SetlistController::missingCount() const noexcept {
    int n = 0;
    for (const auto& s : songs_) {
        if (!s.has_value()) ++n;
    }
    return n;
}

std::vector<std::string> SetlistController::missingTitles() const {
    std::vector<std::string> out;
    for (std::size_t i = 0; i < songs_.size(); ++i) {
        if (songs_[i].has_value()) continue;
        const auto& entry = setlist_.entries[i];
        // The cached title is the whole reason it is cached: without it this line reads
        // "verse-2-final.ghostsong is missing", which tells the performer nothing useful.
        out.push_back(entry.cachedTitle.empty() ? entry.songFile : entry.cachedTitle);
    }
    return out;
}

} // namespace ghostband::core
