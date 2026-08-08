# CLAUDE.md — Persistent project rules for Follow

Follow is a live-performance macOS app that drives Magenta RealTime 2 (MRT2) as an
AI backing band for a solo singer-songwriter. Read `ARCHITECTURE.md` before changing
anything structural, and `docs/MRT2_API_NOTES.md` before touching anything MRT2-shaped.

---

## The three rules that override everything else

1. **Never fabricate MRT2 APIs.** If a call is not listed in `docs/MRT2_API_NOTES.md`,
   verify it against pinned upstream source and add it there first. Upstream source beats
   upstream docs; when they disagree, record the discrepancy in §8 of that file.

2. **Never fake functionality.** A control that does not work must be absent, disabled,
   or explicitly labelled *unavailable* / *experimental*. This applies to UI, to docs,
   and to status text. "Marked as experimental" is fine; a button that looks live and
   does nothing is not.

3. **Never claim something works that has not been run.** If it was not compiled, say
   "written, not compiled". If a test was not executed, do not report it as passing.
   This project is currently developed on Linux where the MRT2 and JUCE targets cannot
   build at all — that makes this rule constantly relevant, not occasionally.

---

## Real-time audio rules (non-negotiable)

In the audio callback, **never**: allocate, free, lock, log, do file or network I/O,
touch the UI, load a model, or call anything with unbounded latency.

- Cross-thread state is `std::atomic` with explicit memory ordering.
- Buffers are preallocated at `prepare()` time and sized to the max block.
- The audio callback may call exactly two things: the backend's `readStereo()` (verified
  lock-free upstream) and `follow::core::AiOutputStage::process()`.
- Any non-obvious real-time decision gets a comment explaining *why*, not *what*.
- Audio-device errors and MIDI disconnects are **expected runtime conditions**, not
  exceptional ones. Handle them; do not assert on them.

## Architecture rules

- `src/core/` depends on **only the C++20 standard library**. No JUCE, no MRT2, no
  platform headers. This is what makes the safety logic testable off-Mac — do not erode it.
- MRT2 is reached only through `IGenerationBackend`. No `magentart::` symbol appears
  outside `src/backend/Mrt2Backend.*`.
- Do not reimplement what MRT2 already provides (inference thread, ring buffer, gain
  smoothing, underrun counting, recording buffer). See `docs/MRT2_API_NOTES.md` §7.
- PANIC lives in `follow::core` and must work when MRT2 is hung. Never delegate it to
  `set_mute()` alone.

## Product rules

- The musician leads; the AI is additive and never in the primary signal path.
- Voice and guitar must keep working with Follow dead. No feature may violate this.
- Foot control beats mouse control. Performance Mode must be usable without the trackpad.
- Prompts describe **musical attributes**, never "in the style of <living artist>".
- `AI Intensity` (how much the band plays) and `AI Output Level` (how loud) are different
  parameters and must never be conflated.
- Generated accompaniment should stay instrumental where practical.
- Dark, high-contrast, stage-hardware aesthetic. No gradients-everywhere, no AI sparkles,
  no chat UI, no generic purple.

## Coding rules

- C++20, RAII, no raw owning pointers, no exceptions across the RT boundary.
- Keep builds green. Build after meaningful changes; run `ctest` before committing.
- Do not silence compiler warnings — fix them. Core builds with `-Wall -Wextra`.
- Prefer simple proven structure over cleverness. Don't rewrite working subsystems.
- Check the licence before adding any dependency, and record it in
  `THIRD_PARTY_NOTICES.md`.
- Never bundle MRT2 model weights in the repo or in a distributed build.

## Maintenance rules

Keep these current as work progresses — they are part of the deliverable, not commentary:

- `IMPLEMENTATION_PLAN.md` — phase status
- `KNOWN_ISSUES.md` — every known gap, with impact
- `STAGE_READINESS.md` — the acceptance checklist, honestly ticked
- `docs/MRT2_API_NOTES.md` — the API ground truth
- `THIRD_PARTY_NOTICES.md` — licences

## When you hit an unknown

Do not swap in a mock. In order: inspect upstream source → inspect upstream examples →
search project docs → build a minimal isolated experiment → record the result → choose
based on observed behaviour. If an MRT2 limitation blocks the design, state what we
expected, what MRT2 actually supports, the impact, the best workaround, and whether that
workaround compromises live reliability. Then implement the safest reasonable option.
