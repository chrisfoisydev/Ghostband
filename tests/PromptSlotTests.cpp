// GhostBand — live AI accompaniment for singer-songwriters.
// Copyright 2026 GhostBand contributors. Licensed under Apache-2.0.
//
// Prompt-slot allocation. This decides whether a section change is instant or stalls on
// an async MusicCoCa encode, which is the difference between a usable live instrument and
// one that hiccups mid-song.

#include "TestMain.h"
#include "core/PromptSlotAllocator.h"

using namespace ghostband::core;

namespace {

Song songWithPrompts(const std::vector<std::string>& prompts) {
    Song song;
    song.defaultStylePrompt = "default";
    for (std::size_t i = 0; i < prompts.size(); ++i) {
        SongSection s;
        s.name = "S" + std::to_string(i);
        s.stylePrompt = prompts[i];
        song.sections.push_back(s);
    }
    return song;
}

float sum(const std::array<float, kMaxPrompts>& w) {
    float t = 0.0f;
    for (float x : w) t += x;
    return t;
}

} // namespace

TEST_MAIN_BEGIN("PromptSlots")

TEST("reserved slots are withheld from sections") {
    // Sections and IntensityMacro compete for the same six slots. The reservation is how
    // that tension is resolved, so it must be explicit and inspectable.
    PromptSlotAllocator a(2);
    CHECK_EQ(a.reservedSlots(), 2);
    CHECK_EQ(a.sectionCapacity(), static_cast<int>(kMaxPrompts) - 2);
    CHECK_EQ(a.reservedBase(), a.sectionCapacity());

    PromptSlotAllocator none(0);
    CHECK_EQ(none.sectionCapacity(), static_cast<int>(kMaxPrompts));
}

TEST("a song inside capacity is fully resident — no change can ever stall") {
    const Song song = songWithPrompts({"verse", "chorus", "bridge", "outro"});
    PromptSlotAllocator a(2);   // capacity 4
    a.setSong(&song);

    CHECK(a.isFullyResident());
    for (int i = 0; i < 4; ++i) CHECK(a.slotFor(i) >= 0);
    CHECK_EQ(a.residentSections().size(), 4u);

    // And every one is reachable without an encode.
    for (int i = 0; i < 4; ++i) {
        bool changed = true;
        a.makeResident(i, 0, &changed);
        CHECK(!changed);
    }
}

TEST("sections sharing a prompt share a slot") {
    // Verse/Verse2/Verse3 with one wording costs one slot, not three. Budgeting by
    // section count instead would reject perfectly playable songs.
    const Song song = songWithPrompts({"verse", "chorus", "verse", "chorus", "verse"});
    PromptSlotAllocator a(2);
    a.setSong(&song);

    CHECK(a.isFullyResident());
    CHECK_EQ(a.slotFor(0), a.slotFor(2));
    CHECK_EQ(a.slotFor(0), a.slotFor(4));
    CHECK_EQ(a.slotFor(1), a.slotFor(3));
    CHECK(a.slotFor(0) != a.slotFor(1));
}

TEST("slots are assigned in first-appearance order") {
    // A song that overflows should degrade at its tail, not its opening — the performer
    // reaches the early sections first.
    const Song song = songWithPrompts({"first", "second", "third"});
    PromptSlotAllocator a(2);
    a.setSong(&song);

    CHECK_EQ(a.slotFor(0), 0);
    CHECK_EQ(a.slotFor(1), 1);
    CHECK_EQ(a.slotFor(2), 2);
}

TEST("an overflowing song is reported, not rejected") {
    const Song song = songWithPrompts({"a", "b", "c", "d", "e", "f", "g"});
    PromptSlotAllocator a(2);   // capacity 4
    a.setSong(&song);

    CHECK(!a.isFullyResident());   // honest: some change in this song will stall
    CHECK_EQ(a.residentSections().size(), 4u);
    CHECK(a.slotFor(0) >= 0);
    CHECK_EQ(a.slotFor(6), -1);    // the tail is not resident
}

TEST("reaching a non-resident section evicts and reports the encode") {
    const Song song = songWithPrompts({"a", "b", "c", "d", "e"});
    PromptSlotAllocator a(2);
    a.setSong(&song);
    CHECK_EQ(a.slotFor(4), -1);

    bool changed = false;
    const int slot = a.makeResident(4, /*current=*/0, &changed);

    CHECK(slot >= 0);
    CHECK(slot < a.sectionCapacity());
    CHECK(changed);                 // caller must re-encode; the change is not instant
    CHECK_EQ(a.slotFor(4), slot);
}

TEST("eviction never takes the slot that is currently sounding") {
    // Evicting the playing section's prompt would stall the band immediately — the worst
    // possible moment.
    const Song song = songWithPrompts({"a", "b", "c", "d", "e", "f"});
    PromptSlotAllocator a(2);
    a.setSong(&song);

    const int playing = 1;
    const int protected_slot = a.slotFor(playing);
    CHECK(protected_slot >= 0);

    for (int target : {4, 5, 4, 5}) {
        bool changed = false;
        const int slot = a.makeResident(target, playing, &changed);
        CHECK(slot != protected_slot);
    }
    CHECK_EQ(a.slotFor(playing), protected_slot);  // still there
}

TEST("eviction drops the section furthest from the one playing") {
    // The least likely to be reached next.
    const Song song = songWithPrompts({"a", "b", "c", "d", "e"});
    PromptSlotAllocator a(2);   // capacity 4: a,b,c,d resident; e is not
    a.setSong(&song);

    const int slot_a = a.slotFor(0);   // section 0, distance 3 from section 3
    const int slot_b = a.slotFor(1);
    const int slot_c = a.slotFor(2);

    bool changed = false;
    const int slot_e = a.makeResident(4, /*current=*/3, &changed);

    CHECK(changed);
    CHECK_EQ(slot_e, slot_a);          // 'a' was furthest from section 3
    CHECK_EQ(a.slotFor(0), -1);        // and is now gone
    CHECK_EQ(a.slotFor(1), slot_b);    // nearer ones survive
    CHECK_EQ(a.slotFor(2), slot_c);
}

TEST("a prompt shared by several sections is only as evictable as its nearest user") {
    // 'shared' is used at section 1 (adjacent to the playing section) and again at
    // section 4 (further away). Judging it by its furthest user would evict it, even
    // though the performer is one step from needing it.
    const Song song = songWithPrompts({"far", "shared", "cur", "x", "shared", "new"});
    PromptSlotAllocator a(2);   // capacity 4: far, shared, cur, x resident; 'new' is not
    a.setSong(&song);
    CHECK(!a.isFullyResident());

    const int slot_far = a.slotFor(0);
    const int slot_shared = a.slotFor(1);
    CHECK_EQ(a.slotFor(4), slot_shared);   // both users point at one slot

    bool changed = false;
    const int taken = a.makeResident(5, /*current=*/2, &changed);

    CHECK(changed);
    CHECK_EQ(taken, slot_far);             // 'far' is genuinely furthest, so it goes
    CHECK_EQ(a.slotFor(1), slot_shared);   // 'shared' survives on its nearest user
    CHECK_EQ(a.slotFor(4), slot_shared);
}

TEST("slotPrompts is slot-ordered and sized for the backend call") {
    const Song song = songWithPrompts({"a", "b"});
    PromptSlotAllocator a(2);
    a.setSong(&song);

    const auto& prompts = a.slotPrompts();
    CHECK_EQ(prompts.size(), kMaxPrompts);
    CHECK(prompts[0] == "a");
    CHECK(prompts[1] == "b");
    CHECK(prompts[2].empty());
}

TEST("no song means no slots, and no crash") {
    PromptSlotAllocator a(2);
    CHECK_EQ(a.slotFor(0), -1);
    bool changed = true;
    CHECK_EQ(a.makeResident(0, 0, &changed), -1);
    CHECK(!changed);
    CHECK(a.residentSections().empty());
}

// ---------------------------------------------------------------------------
// Blend weights
// ---------------------------------------------------------------------------

TEST("a transition crossfades between slots and always sums to 1") {
    for (int i = 0; i <= 10; ++i) {
        const float p = static_cast<float>(i) / 10.0f;
        const auto w = sectionBlendWeights(0, 2, p);
        CHECK_NEAR(sum(w), 1.0f, 1e-5);
        CHECK_NEAR(w[0], 1.0f - p, 1e-5);
        CHECK_NEAR(w[2], p, 1e-5);
    }
}

TEST("a settled transition is entirely on the incoming slot") {
    const auto w = sectionBlendWeights(0, 3, 1.0f);
    CHECK_NEAR(w[3], 1.0f, 1e-5);
    CHECK_NEAR(w[0], 0.0f, 1e-5);
}

TEST("a repeat of the same section holds full weight, not a crossfade to itself") {
    const auto w = sectionBlendWeights(2, 2, 0.5f);
    CHECK_NEAR(w[2], 1.0f, 1e-5);
    CHECK_NEAR(sum(w), 1.0f, 1e-5);
}

TEST("an invalid target still conditions the model rather than muting the style") {
    // All-zero weights would silently drop style conditioning, which sounds like a bug
    // nobody can diagnose. Falling back to slot 0 is wrong but audible and recoverable.
    const auto w = sectionBlendWeights(0, -1, 0.5f);
    CHECK_NEAR(sum(w), 1.0f, 1e-5);
    CHECK_NEAR(w[0], 1.0f, 1e-5);
}

TEST("progress outside 0..1 is clamped") {
    CHECK_NEAR(sectionBlendWeights(0, 1, -5.0f)[0], 1.0f, 1e-5);
    CHECK_NEAR(sectionBlendWeights(0, 1, 5.0f)[1], 1.0f, 1e-5);
}

TEST_MAIN_END()
