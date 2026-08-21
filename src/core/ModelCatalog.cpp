// GhostBand — live AI accompaniment for singer-songwriters.
// Copyright 2026 GhostBand contributors. Licensed under Apache-2.0.

#include "core/ModelCatalog.h"

#include <algorithm>
#include <array>
#include <cctype>

namespace ghostband::core {

namespace {

std::string toLower(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        out.push_back(static_cast<char>(
            std::tolower(static_cast<unsigned char>(c))));
    }
    return out;
}

/// Normalise a reported chip string to the form used in upstream's table: lower case, no
/// "apple " prefix, single spaces. `"Apple M2 Pro"` -> `"m2 pro"`.
std::string normaliseChip(std::string_view chipName) {
    std::string s = toLower(chipName);

    // Collapse runs of whitespace and trim. Reported strings have had trailing NULs and
    // double spaces in the wild, and a mismatch here silently downgrades a capable machine
    // to "unverified", which is the wrong direction to fail in.
    std::string collapsed;
    collapsed.reserve(s.size());
    bool in_space = true;   // leading whitespace is skipped
    for (char c : s) {
        const bool is_space = c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\0';
        if (is_space) {
            if (!in_space) collapsed.push_back(' ');
            in_space = true;
        } else {
            collapsed.push_back(c);
            in_space = false;
        }
    }
    if (!collapsed.empty() && collapsed.back() == ' ') collapsed.pop_back();

    constexpr std::string_view kApple = "apple ";
    if (collapsed.rfind(kApple, 0) == 0) collapsed.erase(0, kApple.size());
    return collapsed;
}

/// The two rows of upstream's table. Kept as data rather than as `if` chains so the
/// correspondence with `docs/MRT2_API_NOTES.md` §1 can be checked by eye.
constexpr std::array<std::string_view, 4> kBaseCapableChips{
    "m5 max", "m3 max", "m2 max", "m4 pro",
};
constexpr std::array<std::string_view, 5> kBaseIncapableChips{
    "m2 pro", "m1 pro", "m4 air", "m3 air", "m1 air",
};

/// Upstream lists Airs by machine ("M4 Air"), but `machdep.cpu.brand_string` reports the
/// chip ("Apple M4"), so a MacBook Air never matches the table by that name alone. A
/// bare, tier-less Apple chip is the entry-level part in every case, which is what the
/// Air rows describe — so it lands on the incapable row.
///
/// This is the one inference in this file. It is made explicit rather than buried because
/// it is the only place the table is extended beyond what upstream literally wrote, and it
/// errs toward warning: the failure mode is telling an M4 mini owner their machine may not
/// keep up when it might, rather than telling an Air owner it will when it will not.
bool isBareAppleSiliconChip(const std::string& chip) {
    if (chip.size() != 2) return false;                 // "m1".."m9"
    return chip[0] == 'm' && std::isdigit(static_cast<unsigned char>(chip[1]));
}

} // namespace

const std::vector<ModelInfo>& knownModels() {
    // Names from the brief: what the model is *for*, not how big it is. The size is on the
    // next line for anyone who wants it.
    static const std::vector<ModelInfo> models = {
        {"mrt2_small", "Performance", "230M",
         "The live default. Real-time on every Apple Silicon machine upstream lists."},
        {"mrt2_base", "High Quality", "2.4B",
         "Richer playing, much heavier. Real-time only on the fastest chips."},
    };
    return models;
}

const ModelInfo* findModel(std::string_view id) {
    const auto& models = knownModels();
    const auto it = std::find_if(models.begin(), models.end(),
                                 [id](const ModelInfo& m) { return m.id == id; });
    return it == models.end() ? nullptr : &*it;
}

RealtimeVerdict realtimeVerdictFor(std::string_view chipName, std::string_view modelId) {
    if (findModel(modelId) == nullptr) return RealtimeVerdict::Unknown;

    const std::string chip = normaliseChip(chipName);
    if (chip.empty()) return RealtimeVerdict::Unknown;

    const bool listed_capable =
        std::find(kBaseCapableChips.begin(), kBaseCapableChips.end(), chip)
        != kBaseCapableChips.end();
    const bool listed_incapable =
        std::find(kBaseIncapableChips.begin(), kBaseIncapableChips.end(), chip)
        != kBaseIncapableChips.end();
    const bool known_chip = listed_capable || listed_incapable
                            || isBareAppleSiliconChip(chip);

    if (!known_chip) return RealtimeVerdict::Unknown;

    // Every chip in the table runs mrt2_small in real time — that is the whole reason it
    // is the live default.
    if (modelId == "mrt2_small") return RealtimeVerdict::Yes;

    return listed_capable ? RealtimeVerdict::Yes : RealtimeVerdict::No;
}

const char* verdictLabel(RealtimeVerdict v) noexcept {
    switch (v) {
        case RealtimeVerdict::Yes:     return "REAL TIME";
        case RealtimeVerdict::No:      return "TOO SLOW HERE";
        // Not "OK", and not blank: an unlisted machine is a thing the performer should
        // know about before a gig, not a silent default.
        case RealtimeVerdict::Unknown: return "NOT VERIFIED";
    }
    return "?";
}

std::string verdictExplanation(RealtimeVerdict v, std::string_view chipName,
                               std::string_view modelId) {
    const auto* model = findModel(modelId);
    const std::string name = model != nullptr ? model->displayName : std::string(modelId);
    const std::string chip = chipName.empty() ? std::string("this machine")
                                              : std::string(chipName);

    switch (v) {
        case RealtimeVerdict::Yes:
            return {};
        case RealtimeVerdict::No:
            // Says what will happen, not that it is forbidden. It loads; it underruns.
            return name + " is not real-time on " + chip
                 + ". It will load and play, but the band will break up continuously. "
                   "Rehearsal only - do not take this to a gig.";
        case RealtimeVerdict::Unknown:
            return name + " has not been verified on " + chip
                 + ". It may keep up or it may underrun. Try it at soundcheck, "
                   "not on stage.";
    }
    return {};
}

bool needsConfirmation(RealtimeVerdict v) noexcept {
    return v == RealtimeVerdict::No;
}

} // namespace ghostband::core
