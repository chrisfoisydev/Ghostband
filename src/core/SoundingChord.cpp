// GhostBand — live AI accompaniment for singer-songwriters.
// Copyright 2026 GhostBand contributors. Licensed under Apache-2.0.

#include "core/SoundingChord.h"

#include <algorithm>

namespace ghostband::core {

SoundingChord::Change SoundingChord::moveTo(const std::vector<int>& notes) {
    // Normalise first: callers build these from chord tables and a slash bass, so
    // duplicates are possible and order is not guaranteed.
    std::vector<int> target = notes;
    std::sort(target.begin(), target.end());
    target.erase(std::unique(target.begin(), target.end()), target.end());

    Change change;
    // Both sides are sorted and unique, so the diff is two linear set operations rather
    // than a search per note. The common tones fall out as "in neither result", which is
    // exactly the notes that must be left ringing.
    std::set_difference(sounding_.begin(), sounding_.end(), target.begin(), target.end(),
                        std::back_inserter(change.toStop));
    std::set_difference(target.begin(), target.end(), sounding_.begin(), sounding_.end(),
                        std::back_inserter(change.toStart));

    sounding_ = std::move(target);
    return change;
}

SoundingChord::Change SoundingChord::clear() { return moveTo({}); }

} // namespace ghostband::core
