// GhostBand — live AI accompaniment for singer-songwriters.
// Copyright 2026 GhostBand contributors. Licensed under Apache-2.0.

#include "Song.h"

#include "GhostBandConstants.h"

#include <algorithm>

namespace ghostband::core {

const char* toString(HarmonySource s) noexcept {
    switch (s) {
        case HarmonySource::Midi:               return "midi";
        case HarmonySource::SongMap:            return "song_map";
        case HarmonySource::GuitarExperimental: return "guitar_experimental";
    }
    return "unknown";
}

const char* toDisplayString(HarmonySource s) noexcept {
    switch (s) {
        case HarmonySource::Midi:               return "MIDI";
        case HarmonySource::SongMap:            return "SONG MAP";
        // The label carries the warning. A performer must never discover mid-set that the
        // harmony source they chose was the experimental one.
        case HarmonySource::GuitarExperimental: return "GUITAR - EXPERIMENTAL";
    }
    return "UNKNOWN";
}

std::string SongSection::resolvedPrompt(const std::string& songDefault) const {
    return stylePrompt.empty() ? songDefault : stylePrompt;
}

std::string Song::validate() const {
    if (sections.empty()) return "Song has no sections";

    // Every section must resolve to a non-empty prompt, or MRT2 has nothing to condition
    // on and the band silently becomes whatever it was last told.
    if (defaultStylePrompt.empty()) {
        for (const auto& s : sections) {
            if (s.stylePrompt.empty()) {
                return "Section '" + s.name + "' has no style prompt and the song has no default";
            }
        }
    }

    for (const auto& s : sections) {
        if (s.name.empty()) return "A section has no name";
        if (s.aiIntensity < 0.0f || s.aiIntensity > 1.0f) {
            return "Section '" + s.name + "' has an intensity outside 0..1";
        }
        if (s.transitionMs < 0 || s.transitionMs > kMaxTransitionMs) {
            return "Section '" + s.name + "' has an out-of-range transition time";
        }
        if (s.tempoBpm && (*s.tempoBpm < 20.0 || *s.tempoBpm > 300.0)) {
            return "Section '" + s.name + "' has an implausible tempo";
        }
    }
    return {};
}

std::vector<std::string> Song::distinctPrompts() const {
    std::vector<std::string> out;
    for (const auto& s : sections) {
        auto prompt = s.resolvedPrompt(defaultStylePrompt);
        if (std::find(out.begin(), out.end(), prompt) == out.end()) {
            out.push_back(std::move(prompt));
        }
    }
    return out;
}

bool Song::fitsPromptSlots() const {
    return distinctPrompts().size() <= kMaxPrompts;
}

Song makeDemoSong() {
    // Deliberately generic: attribute-based prompts (never "in the style of"), and a
    // I-vi-IV-V-ish progression that belongs to nobody. Enough structure to exercise
    // section changes without shipping anyone's song.
    Song song;
    song.title = "Demo Song";
    song.defaultStylePrompt =
        "warm organic indie folk ensemble, upright bass, brushed percussion, "
        "atmospheric piano, supportive accompaniment, instrumental";

    SongSection verse;
    verse.name = "Verse";
    verse.stylePrompt =
        "sparse atmospheric piano, subtle cello, no percussion, "
        "spacious and restrained, instrumental";
    verse.aiIntensity = 0.18f;
    verse.transitionMs = kTransitionSlowMs;
    verse.chordProgression = {"C", "Am", "F", "G"};

    SongSection chorus;
    chorus.name = "Chorus";
    chorus.stylePrompt =
        "warm bass, live drums, piano and cello, broad organic ensemble, "
        "supportive but full, instrumental";
    chorus.aiIntensity = 0.68f;
    chorus.transitionMs = kTransitionFastMs;
    chorus.chordProgression = {"F", "G", "C", "Am"};

    song.sections = {verse, chorus};
    return song;
}

} // namespace ghostband::core
