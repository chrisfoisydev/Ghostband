# Third-Party Notices

GhostBand depends on third-party software and, at runtime, on third-party model weights that
the **user downloads themselves**. Nothing in this repository bundles model weights.

> ⚠️ **This document is an engineering record, not legal advice.** Items marked
> 🚩 **LEGAL REVIEW** must be cleared by a human before any commercial distribution.

---

## 1. Magenta RealTime 2 — code

- **Project:** `magenta/magenta-realtime` (Google LLC / Google DeepMind)
- **Component used:** `magentart::core` C++ inference library (`RealtimeRunner`, `MLXEngine`)
- **Licence:** **Apache License 2.0**
- **Pinned commit:** `694a545e4ba0b88bf1150137b129582166d3e07f`
- **Obligations:** retain copyright and licence notices; state significant modifications;
  include a copy of the Apache 2.0 licence in distributions. Apache 2.0 includes a patent
  grant and permits commercial use and static linking.
- **Status:** ✅ compatible with commercial distribution, subject to notice obligations.

## 2. Magenta RealTime 2 — model weights

- **Artifacts:** `mrt2_small` (230M), `mrt2_base` (2.4B), plus MusicCoCa and SpectroStream
  resources
- **Licence:** **Creative Commons Attribution 4.0 International (CC-BY-4.0)**
- **Source:** https://huggingface.co/google/magenta-realtime-2, fetched by the user via
  `mrt models init` / `mrt models download`
- **Additional terms from `MODEL.md`:**
  - "Google claims no rights in outputs you generate using Magenta RealTime 2. You and
    your users are solely responsible for outputs and their subsequent uses."
  - Users must not generate content that infringes the rights of others.
  - Distributed "AS IS", without warranties.
- **GhostBand's policy:** **we do not bundle, mirror, or redistribute weights.** First run
  detects whether MRT2 resources are present and, if not, tells the user the exact
  official command to fetch them. This keeps us out of the redistribution question
  entirely.
- 🚩 **LEGAL REVIEW:** if a future build *does* bundle weights (e.g. for a Mac App Store
  release), CC-BY-4.0 attribution must be discharged in-product — an "About / Credits"
  screen naming Google DeepMind, the model, the licence, and a link — and the
  no-infringing-output terms must be surfaced in the EULA. Also confirm whether a
  performer's *recorded output* triggers any attribution expectation (our reading: no,
  per "Google claims no rights in outputs", but confirm).

## 3. JUCE

- **Project:** JUCE (Raw Material Software / PACE)
- **Used for:** audio device I/O, MIDI, GUI, and future AU/VST3 targets
- **Licensing model:** dual — **GPLv3** *or* a **commercial licence**. Recent JUCE
  versions also offer a royalty-free tier for small revenue, with terms that vary by
  version.
- 🚩 **LEGAL REVIEW — the significant commercial question in this project.**
  - Shipping GhostBand as **closed-source commercial software requires a paid JUCE licence**
    (or qualifying under the current free tier, subject to its revenue cap and splash-screen
    conditions).
  - Under GPLv3 instead, GhostBand itself must be GPLv3, which propagates to the whole app.
  - Interaction to check: **Apache 2.0 code (MRT2) is compatible with GPLv3** (one-way:
    Apache-2.0 → GPLv3 is fine; the reverse is not). So a GPLv3 GhostBand linking MRT2 is
    permissible; a proprietary GhostBand linking MRT2 is also permissible — but the JUCE
    choice is what decides the app's own licence.
  - **Decide before Phase 5 packaging.** The decision affects nothing architecturally —
    `ghostband::core` is JUCE-free, so a JUCE replacement is possible — but it affects
    release economics.

## 4. Transitive dependencies of MRT2 (built from source by upstream CMake)

Pulled in by upstream's `FetchContent`; we link the result. Not vendored by us.

| Dependency | Version | Licence | Notes |
|---|---|---|---|
| MLX (`ml-explore/mlx`) | v0.31.1 | MIT | Apple Silicon array/Metal framework |
| TensorFlow Lite | v2.21.0 | Apache 2.0 | runs MusicCoCa TFLite assets |
| SentencePiece | v0.2.0 | Apache 2.0 | text tokenisation for prompts |
| Abseil, FlatBuffers, et al. | transitive | Apache 2.0 / BSD-family | via TFLite |

- **Status:** ✅ all permissive (MIT / Apache 2.0 / BSD). No copyleft in this set.
- **Obligation:** aggregate their notices into the shipped "Acknowledgements". TFLite in
  particular drags in a long transitive list — generate it mechanically at packaging time
  rather than by hand.

## 5. GhostBand's own dependencies

`ghostband::core` and its tests depend on **nothing but the C++20 standard library**. The
test harness is ~60 lines in `tests/TestMain.h`, written for this project, specifically to
avoid adding a test-framework dependency and its notice obligations.

---

## 6. Attribution surface required in-product

Before any release, GhostBand must ship a Credits screen containing at minimum:

- "Powered by Magenta RealTime 2 — © 2026 Google LLC. Code under Apache 2.0; model weights
  under CC-BY-4.0." with links to both licences and to the model card.
- The JUCE notice appropriate to the licence tier chosen (§3).
- The generated transitive-dependency notice file (§4).

## 7. Open questions for legal

1. JUCE licence tier for commercial release (§3) — **decide before Phase 5**.
2. In-product CC-BY attribution wording if weights are ever bundled (§2).
3. Whether the MRT2 terms' "do not generate infringing content" clause needs to appear in
   GhostBand's own EULA, and how it is presented to a performer using the app live.
4. Whether performances/recordings made with GhostBand need any disclosure — our reading of
   "Google claims no rights in outputs" says no, but confirm before marketing claims.
