// GhostBand — live AI accompaniment for singer-songwriters.
// Copyright 2026 GhostBand contributors. Licensed under Apache-2.0.
//
// Which model can this machine actually play live? The rule underneath these: upstream's
// table is the only evidence, an unlisted machine is "unknown" rather than "fine", and a
// model that cannot keep up is labelled rather than hidden.

#include "TestMain.h"
#include "core/ModelCatalog.h"

using namespace ghostband::core;

TEST_MAIN_BEGIN("ModelCatalog")

TEST("the catalogue offers the two models the brief names") {
    const auto& models = knownModels();
    CHECK(models.size() == 2);
    // Order is offer order: the live default first.
    CHECK(models[0].id == "mrt2_small");
    CHECK(models[0].displayName == "Performance");
    CHECK(models[1].id == "mrt2_base");
    CHECK(models[1].displayName == "High Quality");
}

TEST("lookup by id, and nothing invented for an unknown one") {
    CHECK(findModel("mrt2_small") != nullptr);
    CHECK(findModel("mrt2_base") != nullptr);
    CHECK(findModel("mrt2_enormous") == nullptr);
    CHECK(findModel("") == nullptr);
}

TEST("mrt2_small is real-time on every chip in upstream's table") {
    for (const char* chip : {"Apple M5 Max", "Apple M3 Max", "Apple M2 Max", "Apple M4 Pro",
                             "Apple M2 Pro", "Apple M1 Pro", "Apple M4", "Apple M3",
                             "Apple M1"}) {
        CHECK(realtimeVerdictFor(chip, "mrt2_small") == RealtimeVerdict::Yes);
    }
}

TEST("mrt2_base follows upstream's table exactly") {
    for (const char* chip : {"Apple M5 Max", "Apple M3 Max", "Apple M2 Max", "Apple M4 Pro"}) {
        CHECK(realtimeVerdictFor(chip, "mrt2_base") == RealtimeVerdict::Yes);
    }
    for (const char* chip : {"Apple M2 Pro", "Apple M1 Pro"}) {
        CHECK(realtimeVerdictFor(chip, "mrt2_base") == RealtimeVerdict::No);
    }
}

TEST("this project's own development machine is the case that matters") {
    // The measured baseline in docs/MRT2_API_NOTES.md §1 is an M2 Pro, and mrt2_base is
    // marked ❌ there. If this ever returns Yes, the table has been edited wrongly.
    CHECK(realtimeVerdictFor("Apple M2 Pro", "mrt2_base") == RealtimeVerdict::No);
    CHECK(needsConfirmation(realtimeVerdictFor("Apple M2 Pro", "mrt2_base")));
}

TEST("a bare Apple chip is treated as entry-level, not as unknown") {
    // Upstream lists Airs by machine ("M4 Air"), but the OS reports the chip ("Apple M4").
    // A tier-less part is the entry-level one, which is what those rows describe.
    CHECK(realtimeVerdictFor("Apple M4", "mrt2_base") == RealtimeVerdict::No);
    CHECK(realtimeVerdictFor("Apple M1", "mrt2_base") == RealtimeVerdict::No);
}

TEST("an unlisted chip is Unknown, never Yes") {
    // Ultras are genuinely absent from the table. Assuming an M2 Ultra beats an M2 Max is
    // an inference about someone's gig, and this project does not make those.
    CHECK(realtimeVerdictFor("Apple M2 Ultra", "mrt2_base") == RealtimeVerdict::Unknown);
    CHECK(realtimeVerdictFor("Intel Core i9", "mrt2_base") == RealtimeVerdict::Unknown);
    CHECK(realtimeVerdictFor("", "mrt2_base") == RealtimeVerdict::Unknown);
    CHECK(realtimeVerdictFor("Apple M2 Ultra", "mrt2_small") == RealtimeVerdict::Unknown);
}

TEST("chip strings are matched tolerantly") {
    // Reported strings have carried trailing NULs, double spaces and mixed case. A
    // mismatch downgrades a capable machine to unverified, which is the wrong way to fail.
    CHECK(realtimeVerdictFor("apple m2 max", "mrt2_base") == RealtimeVerdict::Yes);
    CHECK(realtimeVerdictFor("APPLE M2 MAX", "mrt2_base") == RealtimeVerdict::Yes);
    CHECK(realtimeVerdictFor("  Apple   M2  Max  ", "mrt2_base") == RealtimeVerdict::Yes);
    CHECK(realtimeVerdictFor("M2 Max", "mrt2_base") == RealtimeVerdict::Yes);
    CHECK(realtimeVerdictFor(std::string("Apple M2 Max\0", 13), "mrt2_base")
          == RealtimeVerdict::Yes);
}

TEST("an unknown model id is never declared real-time") {
    CHECK(realtimeVerdictFor("Apple M3 Max", "mrt2_enormous") == RealtimeVerdict::Unknown);
}

TEST("only a known-too-slow choice puts a barrier in the way") {
    // Unknown gets a label but not a confirmation: refusing to let someone try a model on
    // an unlisted machine would be the app claiming to know their hardware on no evidence.
    CHECK(needsConfirmation(RealtimeVerdict::No));
    CHECK(!needsConfirmation(RealtimeVerdict::Unknown));
    CHECK(!needsConfirmation(RealtimeVerdict::Yes));
}

TEST("labels never say OK for something unverified") {
    CHECK(std::string(verdictLabel(RealtimeVerdict::Yes)) == "REAL TIME");
    CHECK(std::string(verdictLabel(RealtimeVerdict::No)) == "TOO SLOW HERE");
    CHECK(std::string(verdictLabel(RealtimeVerdict::Unknown)) == "NOT VERIFIED");
}

TEST("the explanation names the machine and the consequence") {
    const auto too_slow = verdictExplanation(RealtimeVerdict::No, "Apple M2 Pro", "mrt2_base");
    CHECK(too_slow.find("Apple M2 Pro") != std::string::npos);
    CHECK(too_slow.find("High Quality") != std::string::npos);
    CHECK(too_slow.find("Rehearsal only") != std::string::npos);
    // It loads and plays badly; it is not blocked. Saying "cannot" would be false.
    CHECK(too_slow.find("cannot") == std::string::npos);

    const auto unknown = verdictExplanation(RealtimeVerdict::Unknown, "Apple M2 Ultra",
                                            "mrt2_base");
    CHECK(unknown.find("soundcheck") != std::string::npos);

    // A model that works needs no explanation at all.
    CHECK(verdictExplanation(RealtimeVerdict::Yes, "Apple M3 Max", "mrt2_base").empty());
}

TEST_MAIN_END()
