# Follow — Known Issues

Every known gap, with impact. Nothing here is hidden from the UI: anything unimplemented
is absent or labelled, per `CLAUDE.md` rule 2.

---

## 1. 🚫 BLOCKER — MRT2 and the JUCE host have never been compiled or run

**Severity: highest. Blocks every acceptance criterion in `STAGE_READINESS.md`.**

The development environment is Linux x86-64. MRT2's C++ engine is macOS/Apple-Silicon
only by explicit upstream design (`message(FATAL_ERROR)` on `NOT APPLE`; links
MLX/Metal/Accelerate/Foundation; requires an Objective-C++ toolchain). JUCE's Linux
audio/GUI dependencies (ALSA, freetype) are also absent here.

Status after the first build on target hardware (2026-08-09):

- ✅ `hello_mrt2` — **built and run.** `mrt2_small` generates coherent instrumental music;
  output is a valid 4.00 s 48 kHz stereo WAV. MRT2 itself is proven on this machine.
- `src/backend/Mrt2Backend.{h,cpp}` — written against pinned upstream headers,
  **still never compiled**.
- `src/app/` (JUCE host) — written, **still never compiled**.
- **Real-time throughput — still unverified.** This is the important one. `hello_mrt2`
  renders offline and reports no timing, so "generates faster than playback" has *not*
  been demonstrated. It needs `EngineMetrics::total_ms` measured against the 40 ms frame
  budget, which needs the Follow app running.
- `RealtimeRunner` — **never exercised.** `hello_mrt2` calls `MLXEngine::generate_frame`
  directly; Follow uses the runner's inference thread and ring buffers. Different path,
  independent risk.
- Latency, CPU/GPU load, memory growth — **unmeasured**.

**What *is* verified:** `follow::core` and `NullBackend` build with `-Wall -Wextra` and
pass their unit tests on this machine. That covers PANIC, the limiter, the underrun
policy, the engine state machine, diagnostics, and logging.

**Resolution:** run the commands in `IMPLEMENTATION_PLAN.md` §"To close it" on an Apple
Silicon Mac. Expect compile errors in `Mrt2Backend` on first attempt — it is adapter code
written without a compiler.

---

## 2. ⚠️ `Mrt2Backend` API assumptions are read-only-verified

Every MRT2 call used is copied from pinned upstream headers at commit `694a545`
(`docs/MRT2_API_NOTES.md`). But signatures were never checked by a compiler, and upstream
is at version `0.0.1` with a `TODO(public-release)` still in the header — the API should
be assumed unstable. Pin the MRT2 checkout; do not track `main`.

---

## 3. ⚠️ The AI Intensity mapping is an untested hypothesis

MRT2 exposes no density/intensity parameter. The macro in `ARCHITECTURE.md` §6 maps
intensity onto prompt blend weights, `set_drumless`, `set_cfg_drums`,
`set_cfg_musiccoca`, and `temperature`. Those choices follow from the parameters'
documented semantics, but **no one has listened to the result**.

**Impact:** the headline performance macro may feel wrong — non-monotonic, or bunched at
one end. **Mitigation:** the mapping is isolated in one class with no other
responsibilities, so retuning is local. Must be evaluated on real audio before Phase 2.

---

## 4. ⚠️ Six prompt slots is a real ceiling

`kMaxPrompts = 6`. A song with more than six *distinct* style prompts cannot hold them
all pre-encoded, so the pre-encoding strategy that makes section changes instant
(`ARCHITECTURE.md` §8) needs slot recycling — encoding the next section's prompt during
the current one.

**Impact:** a rapid jump to a section whose prompt is not resident incurs an async
MusicCoCa encode. **Mitigation (Phase 2):** recycle on section change with one section of
lookahead, and prefer the previous/next section in the arrangement. Long-tail risk: an
unplanned jump backwards through a >6-prompt song. Needs a measured encode time before we
can size the risk — unknown until Phase 0 closes.

---

## 5. ⚠️ MRT2's recording buffer is RAM-resident, not an async disk writer

`RealtimeRunner::start_recording()` accumulates into `std::vector<float>` in memory. A
60-minute stereo set at 48 kHz float is ~1.4 GB, and the vector will reallocate while the
inference thread runs.

**Impact:** the brief (§21) requires that recording never jeopardise generation
stability; the native buffer does not meet that bar for full-set recording.
**Resolution:** Follow supplies its own async disk writer in Phase 5. Until then,
recording stays absent from the UI rather than shipping a version that can stall a gig.

---

## 6. ⚠️ Generation buffer size is an untuned guess

Default is 3840 samples (~80 ms, two model frames), chosen so one late inference frame
cannot underrun. The real value depends on measured `total_ms` jitter on target hardware
and is unmeasured. Exposed in diagnostics rather than hidden. Tune in Phase 4.

---

## 7. ℹ️ Upstream `set_audio_prompt(index, path)` returns a fake embedding

Upstream's header carries `TODO(public-release)`: when `path` is non-empty it "writes a
deterministic fake embedding rather than decoding the file." **Follow never calls it.**
Use `set_audio_prompt_samples()`. Recorded so nobody later "fixes" our omission.

---

## 8. ℹ️ MIDI pitch range is 132, not 128

`kTotalPitches = 132` — 128 pitches plus 4 drum triggers. `MODEL.md` says "128-dim
multihot", which understates it. Follow clamps incoming MIDI to 0–127 and reserves
128–131 for future explicit drum triggering. No impact today.

---

## 9. ℹ️ No licence review has been performed by a human

`THIRD_PARTY_NOTICES.md` records what the licences say. It is not legal advice, and the
CC-BY-4.0 weights plus JUCE's licensing model both carry real commercial-distribution
questions flagged there for human/legal review before any release.
