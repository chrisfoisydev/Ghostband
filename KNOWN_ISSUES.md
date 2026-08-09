# Follow — Known Issues

Every known gap, with impact. Nothing here is hidden from the UI: anything unimplemented
is absent or labelled, per `CLAUDE.md` rule 2.

---

## 1. ⚠️ The MRT2 and JUCE layers cannot be built or tested in CI

**Severity: moderate and structural. This is a permanent property of the project, not a
temporary state.**

The development environment is Linux x86-64. MRT2's C++ engine is macOS/Apple-Silicon
only by explicit upstream design (`message(FATAL_ERROR)` on `NOT APPLE`; links
MLX/Metal/Accelerate/Foundation; requires an Objective-C++ toolchain). JUCE's Linux
audio/GUI dependencies (ALSA, freetype) are also absent here.

**Closed on target hardware (2026-08-09):**

- ✅ `hello_mrt2` built and run; `mrt2_small` generates coherent instrumental music as a
  valid 4.00 s 48 kHz stereo WAV.
- ✅ `Mrt2Backend` and the JUCE host **compile and run**. `Follow.app` opens a 48 kHz
  device at 512 samples, loads `mrt2_small`, and streams through `RealtimeRunner`.
- ✅ **Real-time throughput confirmed: 17.17 ms per 40 ms frame (42.9% of budget)** —
  roughly 2.3x faster than playback, with the generation buffer holding 3328/3840 samples.

**Still open:**

- ❌ **Audible output from Follow has not been heard** — the first run was silenced by the
  bug in §10. Fixed and unit-tested, not yet re-run.
- ❌ Memory growth, long-run stability, device reconnect — unmeasured.

**Structural consequence:** every change to `src/backend/` or `src/app/` is unverified
until someone builds on a Mac. CI can only ever cover `follow::core`. This is why the
safety-critical logic lives there — it is the part that can be regression-tested on every
commit.

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

---

## 10. ✅ FIXED — underrun policy muted the AI before generation ever started

**Found on the first real hardware run (2026-08-09). Severity was high: the AI never
produced audible output.**

**Symptom:** diagnostics showed generation healthy — frame time 17.17 ms against a 40 ms
budget, generation buffer 3328/3840 — yet 2373 audio underruns, `Health: Degraded`, and
output pinned at −120 dBFS. Silence.

**Cause:** the audio callback runs from app launch. Between LOAD MODEL and START, the
backend is a loaded-but-not-generating `RealtimeRunner`, so `readStereo()` correctly
returns `false` on every block. `SafetyMonitor` counted each as a fault, crossed its
threshold within ~2 s, and latched `Degraded` — muting the band *before the performer
ever pressed START*. The 2373 count matched MRT2's own `dropped_frames` exactly, which
confirmed both were observing the same (expected) empty-buffer condition.

The logic error was treating "buffer empty" as a fault unconditionally. When generation
is not running, an empty buffer is the expected state.

**Fix:**
- `SafetyMonitor::setGenerating(bool)` — underruns are only policed while generation is
  meant to be producing audio. Wired to start/stop in `FollowAudioEngine`.
- A **priming grace window** (default 1 s) on the rising edge, because even a healthy
  engine underruns while the ring buffer fills. Absorbed underruns are still counted and
  surfaced via `primingUnderruns()` so a struggling start stays visible.
- `recover()` re-arms the grace, so recovering into an empty buffer cannot instantly
  re-trip.
- Added a **RECOVER AI** control. `Degraded` latches by design, but the UI previously
  offered no way out and the warning text wrongly suggested PANIC would clear it — a dead
  end.

**Covered by five new tests**, including the exact observed scenario (5000 underrunning
blocks while stopped must not trip Degraded).

**Status: fixed and unit-tested; NOT yet re-run on hardware.** Until someone hears audio
from Follow, criteria 3, 7 and 8 in `STAGE_READINESS.md` stay partial.

---

## 11. ⚠️ `mrt2_base` ("High Quality") is not real-time on the reference machine

The development/reference machine is an **Apple M2 Pro**. Upstream's own capability table
marks M2 Pro as ❌ for `mrt2_base` real-time streaming, and our measurement supports it:
`mrt2_small` already consumes 42.9% of the 40 ms frame budget, and base is ~10x the
parameters (2.4B vs 230M).

**Impact:** the Phase 1 model selector must not simply list both models. Offering a
"High Quality" option that cannot keep up would produce continuous underruns mid-set —
precisely the "control that looks live but does nothing" `CLAUDE.md` rule 2 forbids.

**Resolution (Phase 1):** detect the chip, and either hide `mrt2_base` or show it disabled
with an explicit reason ("requires M-series Pro Max"). Never switch models while
performing, per the brief. A user who installs base anyway should get a clear warning
rather than a degraded show.
