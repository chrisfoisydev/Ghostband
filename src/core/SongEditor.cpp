// GhostBand — live AI accompaniment for singer-songwriters.
// Copyright 2026 GhostBand contributors. Licensed under Apache-2.0.

#include "SongEditor.h"

#include "GhostBandConstants.h"

#include <algorithm>
#include <utility>

namespace ghostband::core {

namespace {

/// "Verse" taken? Try "Verse 2", "Verse 3", ... Duplicating a section is the common way to
/// build a song, and two sections sharing a name makes the stage screen ambiguous at
/// exactly the moment it must not be.
std::string uniqueName(const std::vector<SongSection>& sections, const std::string& base,
                       int ignoreIndex) {
    auto taken = [&](const std::string& candidate) {
        for (std::size_t i = 0; i < sections.size(); ++i) {
            if (static_cast<int>(i) == ignoreIndex) continue;
            if (sections[i].name == candidate) return true;
        }
        return false;
    };

    if (!taken(base)) return base;
    for (int n = 2; n < 1000; ++n) {
        const auto candidate = base + " " + std::to_string(n);
        if (!taken(candidate)) return candidate;
    }
    return base;   // 999 sections of one name: give up rather than loop
}

} // namespace

SongEditor::SongEditor() : song_(makeDemoSong()) {}

SongEditor::SongEditor(Song song) : song_(std::move(song)) {}

void SongEditor::reset(Song song) {
    song_ = std::move(song);
    undo_.clear();
    dirty_ = false;
}

int SongEditor::promptCapacity() noexcept { return static_cast<int>(kMaxPrompts); }

void SongEditor::pushUndo() {
    undo_.push_back(song_);
    if (undo_.size() > kMaxUndo) undo_.erase(undo_.begin());
    dirty_ = true;
}

bool SongEditor::undo() {
    if (undo_.empty()) return false;
    song_ = std::move(undo_.back());
    undo_.pop_back();
    // Still dirty: undoing back to the saved state is indistinguishable from editing to
    // it, and claiming "no unsaved changes" when we are not certain is the wrong way to
    // be wrong.
    dirty_ = true;
    return true;
}

const SongSection* SongEditor::sectionAt(int index) const noexcept {
    if (index < 0 || index >= sectionCount()) return nullptr;
    return &song_.sections[static_cast<std::size_t>(index)];
}

// --- Song-level ---------------------------------------------------------------------

void SongEditor::setTitle(const std::string& title) {
    if (song_.title == title) return;   // no undo entry for a no-op
    pushUndo();
    song_.title = title;
}

void SongEditor::setDefaultPrompt(const std::string& prompt) {
    if (song_.defaultStylePrompt == prompt) return;
    pushUndo();
    song_.defaultStylePrompt = prompt;
}

void SongEditor::setMasterLevelDb(float db) {
    const float clamped = std::clamp(db, -60.0f, 6.0f);
    if (song_.masterAiLevelDb == clamped) return;
    pushUndo();
    song_.masterAiLevelDb = clamped;
}

void SongEditor::setHarmonySource(HarmonySource source) {
    if (song_.harmonySource == source) return;
    pushUndo();
    song_.harmonySource = source;
}

// --- Sections -----------------------------------------------------------------------

int SongEditor::addSection() {
    pushUndo();
    SongSection s;
    s.name = uniqueName(song_.sections, "Section", -1);
    song_.sections.push_back(std::move(s));
    return sectionCount() - 1;
}

int SongEditor::duplicateSection(int index) {
    if (index < 0 || index >= sectionCount()) return -1;
    pushUndo();

    SongSection copy = song_.sections[static_cast<std::size_t>(index)];
    copy.name = uniqueName(song_.sections, copy.name, -1);
    // Inserted directly after the original, not appended: duplicating a verse to make
    // "Verse 2" means it belongs next to the verse, not at the end of the song.
    song_.sections.insert(song_.sections.begin() + index + 1, std::move(copy));
    return index + 1;
}

bool SongEditor::removeSection(int index) {
    if (index < 0 || index >= sectionCount()) return false;
    if (sectionCount() <= 1) return false;   // a song with no sections cannot be performed

    pushUndo();
    song_.sections.erase(song_.sections.begin() + index);
    return true;
}

bool SongEditor::moveSection(int from, int to) {
    if (from < 0 || from >= sectionCount()) return false;
    if (to < 0 || to >= sectionCount()) return false;
    if (from == to) return false;

    pushUndo();
    auto section = std::move(song_.sections[static_cast<std::size_t>(from)]);
    song_.sections.erase(song_.sections.begin() + from);
    song_.sections.insert(song_.sections.begin() + to, std::move(section));
    return true;
}

bool SongEditor::setSectionName(int index, const std::string& name) {
    if (index < 0 || index >= sectionCount()) return false;

    // An unnamed section shows as a blank on the stage screen, and Song::validate rejects
    // it on save. Substituting a name is kinder than refusing the keystroke that empties
    // the field.
    const std::string wanted = name.empty() ? "Section" : name;
    const auto unique = uniqueName(song_.sections, wanted, index);

    auto& s = song_.sections[static_cast<std::size_t>(index)];
    if (s.name == unique) return true;
    pushUndo();
    s.name = unique;
    return true;
}

bool SongEditor::setSectionPrompt(int index, const std::string& prompt) {
    if (index < 0 || index >= sectionCount()) return false;
    auto& s = song_.sections[static_cast<std::size_t>(index)];
    if (s.stylePrompt == prompt) return true;
    pushUndo();
    // Empty is meaningful: it means "inherit the song default". Not normalised away.
    s.stylePrompt = prompt;
    return true;
}

bool SongEditor::setSectionIntensity(int index, float intensity) {
    if (index < 0 || index >= sectionCount()) return false;
    const float clamped = std::clamp(intensity, 0.0f, 1.0f);
    auto& s = song_.sections[static_cast<std::size_t>(index)];
    if (s.aiIntensity == clamped) return true;
    pushUndo();
    s.aiIntensity = clamped;
    return true;
}

bool SongEditor::setSectionAiEnabled(int index, bool enabled) {
    if (index < 0 || index >= sectionCount()) return false;
    auto& s = song_.sections[static_cast<std::size_t>(index)];
    if (s.aiEnabled == enabled) return true;
    pushUndo();
    s.aiEnabled = enabled;
    return true;
}

bool SongEditor::setSectionTransitionMs(int index, int ms) {
    if (index < 0 || index >= sectionCount()) return false;
    const int clamped = std::clamp(ms, 0, kMaxTransitionMs);
    auto& s = song_.sections[static_cast<std::size_t>(index)];
    if (s.transitionMs == clamped) return true;
    pushUndo();
    s.transitionMs = clamped;
    return true;
}

bool SongEditor::setSectionNotes(int index, const std::string& notes) {
    if (index < 0 || index >= sectionCount()) return false;
    auto& s = song_.sections[static_cast<std::size_t>(index)];
    if (s.notes == notes) return true;
    pushUndo();
    s.notes = notes;
    return true;
}

bool SongEditor::setSectionChords(int index, const std::vector<std::string>& chords) {
    if (index < 0 || index >= sectionCount()) return false;
    auto& s = song_.sections[static_cast<std::size_t>(index)];
    if (s.chordProgression == chords) return true;
    pushUndo();
    s.chordProgression = chords;
    return true;
}

bool SongEditor::setSectionTempoBpm(int index, std::optional<double> bpm) {
    if (index < 0 || index >= sectionCount()) return false;

    // Refused, not clamped. A typo'd 1200 silently becoming 300 hands the performer a
    // tempo they did not choose, with nothing on screen to say so.
    if (bpm.has_value() && (*bpm < 20.0 || *bpm > 300.0)) return false;

    auto& s = song_.sections[static_cast<std::size_t>(index)];
    if (s.tempoBpm == bpm) return true;
    pushUndo();
    s.tempoBpm = bpm;
    return true;
}

} // namespace ghostband::core
