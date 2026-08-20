// GhostBand — live AI accompaniment for singer-songwriters.
// Copyright 2026 GhostBand contributors. Licensed under Apache-2.0.

#include "SetlistEditor.h"

#include <utility>

namespace ghostband::core {

SetlistEditor::SetlistEditor() = default;

SetlistEditor::SetlistEditor(Setlist list) : list_(std::move(list)) {}

void SetlistEditor::reset(Setlist list) {
    list_ = std::move(list);
    undo_.clear();
    dirty_ = false;
}

void SetlistEditor::pushUndo() {
    undo_.push_back(list_);
    if (undo_.size() > kMaxUndo) undo_.erase(undo_.begin());
    dirty_ = true;
}

bool SetlistEditor::undo() {
    if (undo_.empty()) return false;
    list_ = std::move(undo_.back());
    undo_.pop_back();
    // Still dirty, matching SongEditor: undoing back to the saved state is
    // indistinguishable from editing to it, and claiming "no unsaved changes" when we are
    // not certain is the wrong way to be wrong.
    return true;
}

void SetlistEditor::setName(const std::string& name) {
    // An unnamed set shows as a blank heading on the stage screen. Substituting is kinder
    // than refusing the keystroke that empties the field — same rule as section names.
    const std::string wanted = name.empty() ? "Untitled Setlist" : name;
    if (wanted == list_.name) return;

    pushUndo();
    list_.name = wanted;
}

const SetlistEntry* SetlistEditor::entryAt(int index) const noexcept {
    if (index < 0 || index >= size()) return nullptr;
    return &list_.entries[static_cast<std::size_t>(index)];
}

int SetlistEditor::addSong(const std::string& songFile, const std::string& cachedTitle) {
    return insertSong(size(), songFile, cachedTitle);
}

int SetlistEditor::insertSong(int index, const std::string& songFile,
                              const std::string& cachedTitle) {
    // An entry with no file is what `Setlist::validate` rejects, so refuse to create one
    // rather than accept a set that cannot be saved.
    if (songFile.empty()) return -1;
    // `size()` is a legal insertion point (append); anything past it is not.
    if (index < 0 || index > size()) return -1;

    pushUndo();
    SetlistEntry entry;
    entry.songFile = songFile;
    // Falling back to the filename rather than leaving it blank: the cached title exists
    // precisely so a missing song still shows something a human can act on.
    entry.cachedTitle = cachedTitle.empty() ? songFile : cachedTitle;
    list_.entries.insert(list_.entries.begin() + index, std::move(entry));
    return index;
}

bool SetlistEditor::removeEntry(int index) {
    if (index < 0 || index >= size()) return false;

    // Unlike SongEditor::removeSection, removing the last entry IS allowed. An empty set is
    // a normal step in building one — you clear it out and start again — whereas a song
    // with no sections is never a state anyone wants. `isPerformable()` reports the
    // difference, so the UI can disable PERFORM SET without blocking the edit.
    pushUndo();
    list_.entries.erase(list_.entries.begin() + index);
    return true;
}

bool SetlistEditor::moveEntry(int from, int to) {
    if (from < 0 || from >= size()) return false;
    if (to < 0 || to >= size()) return false;
    if (from == to) return false;

    pushUndo();
    auto entry = std::move(list_.entries[static_cast<std::size_t>(from)]);
    list_.entries.erase(list_.entries.begin() + from);
    list_.entries.insert(list_.entries.begin() + to, std::move(entry));
    return true;
}

bool SetlistEditor::moveUp(int index) { return moveEntry(index, index - 1); }

bool SetlistEditor::moveDown(int index) { return moveEntry(index, index + 1); }

} // namespace ghostband::core
