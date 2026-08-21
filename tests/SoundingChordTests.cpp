// GhostBand — live AI accompaniment for singer-songwriters.
// Copyright 2026 GhostBand contributors. Licensed under Apache-2.0.
//
// Chord changes as a diff. The rule underneath these: MRT2 treats every note-on as an
// onset, so a note common to both chords must be left ringing rather than re-struck.

#include "TestMain.h"
#include "core/SoundingChord.h"

#include <algorithm>

using namespace ghostband::core;

namespace {
const std::vector<int> kG{67, 71, 74};        // G B D
const std::vector<int> kEm{64, 67, 71};       // E G B  - shares G and B with kG
const std::vector<int> kC{60, 64, 67};        // C E G
} // namespace

TEST_MAIN_BEGIN("SoundingChord")

TEST("a fresh chord holds nothing") {
    SoundingChord c;
    CHECK(c.isEmpty());
    CHECK(c.sounding().empty());
}

TEST("the first chord starts everything and stops nothing") {
    SoundingChord c;
    const auto change = c.moveTo(kG);
    CHECK(change.toStop.empty());
    CHECK(change.toStart == kG);
    CHECK(c.sounding() == kG);
}

TEST("common tones are left ringing") {
    // The whole reason this class exists. G -> Em shares G and B; re-striking them would
    // make the band re-articulate notes a real player would let ring, on every bar line.
    SoundingChord c;
    c.moveTo(kG);
    const auto change = c.moveTo(kEm);

    CHECK(change.toStop == std::vector<int>({74}));    // D leaves
    CHECK(change.toStart == std::vector<int>({64}));   // E arrives
    // 67 and 71 appear in neither list: untouched, still sounding.
    CHECK(c.sounding() == std::vector<int>({64, 67, 71}));
}

TEST("moving to the same chord is a no-op") {
    SoundingChord c;
    c.moveTo(kG);
    const auto change = c.moveTo(kG);
    CHECK(change.isEmpty());
    CHECK(c.sounding() == kG);
}

TEST("clearing releases everything") {
    SoundingChord c;
    c.moveTo(kC);
    const auto change = c.clear();
    CHECK(change.toStart.empty());
    CHECK(change.toStop == kC);
    CHECK(c.isEmpty());
}

TEST("clearing when nothing sounds does nothing") {
    SoundingChord c;
    CHECK(c.clear().isEmpty());
}

TEST("unsorted and duplicated input is normalised") {
    // Chord notes are assembled from an interval table plus an optional slash bass, so
    // duplicates and out-of-order entries are both reachable.
    SoundingChord c;
    const auto change = c.moveTo({74, 67, 71, 67, 74});
    CHECK(change.toStart == std::vector<int>({67, 71, 74}));
    CHECK(c.sounding() == std::vector<int>({67, 71, 74}));
}

TEST("both lists come back in ascending order") {
    SoundingChord c;
    c.moveTo({60, 64, 67, 71});
    const auto change = c.moveTo({62, 65, 69});
    CHECK(std::is_sorted(change.toStop.begin(), change.toStop.end()));
    CHECK(std::is_sorted(change.toStart.begin(), change.toStart.end()));
    CHECK(change.toStop == std::vector<int>({60, 64, 67, 71}));
    CHECK(change.toStart == std::vector<int>({62, 65, 69}));
}

TEST("a whole progression only ever moves what changes") {
    // Walking G - Em - C - G, the shared G (67) is struck once and never re-struck.
    SoundingChord c;
    int starts_of_g = 0;

    for (const auto& chord : {kG, kEm, kC, kG}) {
        const auto change = c.moveTo(chord);
        starts_of_g += static_cast<int>(
            std::count(change.toStart.begin(), change.toStart.end(), 67));
    }
    CHECK(starts_of_g == 1);
}

TEST_MAIN_END()
