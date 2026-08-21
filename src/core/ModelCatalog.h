// GhostBand — live AI accompaniment for singer-songwriters.
// Copyright 2026 GhostBand contributors. Licensed under Apache-2.0.

#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace ghostband::core {

/// Can this machine run this model fast enough to accompany a live performer?
enum class RealtimeVerdict {
    /// Upstream lists this chip as real-time capable for this model.
    Yes,
    /// Upstream lists this chip as **not** real-time capable. The model will still load
    /// and still generate; it will underrun continuously.
    No,
    /// The chip is not in upstream's table. Not a synonym for "fine" — see the note on
    /// `verdictLabel`.
    Unknown,
};

struct ModelInfo {
    /// Directory and file stem under `models/`, e.g. `mrt2_small`.
    std::string id;
    /// What the performer sees. The brief's names, not upstream's.
    std::string displayName;
    /// Parameter count, for the one line of context that makes the choice make sense.
    std::string sizeLabel;
    std::string description;
};

/// The models GhostBand knows how to load, in the order they should be offered.
///
/// Deliberately a fixed list rather than a directory scan: a `.mlxfn` folder that happens
/// to be sitting in `models/` is not necessarily a model this app can drive, and offering
/// it because it exists would be exactly the "looks live, does nothing" control the project
/// rules forbid. Whether each one is *installed* is a separate question, answered by the
/// app layer, which is the only part that may touch the filesystem.
const std::vector<ModelInfo>& knownModels();

/// Look up a model by id. Returns nullptr for an id not in `knownModels()`.
const ModelInfo* findModel(std::string_view id);

/// Upstream's real-time capability table, encoded.
///
/// From `magenta-realtime` README at the pinned commit, recorded in
/// `docs/MRT2_API_NOTES.md` §1:
///
/// | Device | `mrt2_small` (230M) | `mrt2_base` (2.4B) |
/// |---|---|---|
/// | M5 Max / M3 Max / M2 Max / M4 Pro | ✅ | ✅ |
/// | M2 Pro / M1 Pro / M4 Air / M3 Air / M1 Air | ✅ | ❌ |
///
/// `chipName` is whatever the platform reports — on macOS,
/// `sysctlbyname("machdep.cpu.brand_string")`, which gives strings like `"Apple M2 Pro"`.
/// Matching is case-insensitive and tolerant of the `"Apple "` prefix.
///
/// **Anything not in that table returns `Unknown`, including plain M1/M2/M3/M4 and every
/// Ultra.** Extrapolating "an M2 Ultra is at least an M2 Max" would be a guess about
/// someone's gig, and the project rule is that upstream source beats inference. The UI
/// says "not verified" rather than pretending either way.
RealtimeVerdict realtimeVerdictFor(std::string_view chipName, std::string_view modelId);

/// Short, tracked-caps label for the verdict. Never says "OK" for `Unknown`.
const char* verdictLabel(RealtimeVerdict v) noexcept;

/// One sentence for the performer, naming the machine and the consequence. Empty for
/// `Yes` — a model that works needs no explanation.
std::string verdictExplanation(RealtimeVerdict v, std::string_view chipName,
                               std::string_view modelId);

/// True when choosing this model should require a second, deliberate press.
///
/// Only `No` earns a confirmation. `Unknown` gets a label but not a barrier: refusing to
/// let someone try a model on an unlisted machine would be this app deciding it knows
/// their hardware better than they do, on no evidence.
bool needsConfirmation(RealtimeVerdict v) noexcept;

} // namespace ghostband::core
