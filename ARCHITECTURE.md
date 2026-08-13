# GhostBand — Architecture

> Your band follows you.

GhostBand is a macOS / Apple Silicon standalone application that generates live
instrumental accompaniment with **Magenta RealTime 2 (MRT2)**, steered in real time by a
performing singer-songwriter.

This document describes the intended system. It states plainly which parts exist today.
See `IMPLEMENTATION_PLAN.md` for sequencing and `docs/MRT2_API_NOTES.md` for the
source-verified MRT2 API surface that everything here is built on.

---

## 1. The one-sentence architecture

> A JUCE audio callback pulls already-generated 48 kHz stereo audio out of MRT2's
> lock-free ring buffer, passes it through GhostBand's own fade/limiter safety stage, and
> writes it to the user's chosen output pair — while a separate control plane translates
> the performer's MIDI, footswitches and section changes into MRT2 parameter writes that
> are all atomic and non-blocking.

Nothing in the audio path allocates, locks, or waits.

---

## 2. Layer map

```
┌──────────────────────────────────────────────────────────────────┐
│  UI (JUCE)              Performance · Setup · Diagnostics        │  message thread
├──────────────────────────────────────────────────────────────────┤
│  Control plane          Song/Section · Intensity · MIDI map      │  message + MIDI thread
├──────────────────────────────────────────────────────────────────┤
│  ghostband::core        portable, JUCE-free, MRT2-free           │  ← unit tested
│    EngineState · FadeEnvelope · SafetyLimiter · SafetyMonitor    │
│    AiOutputStage · Diagnostics · Logging · IGenerationBackend    │
├──────────────────────────────────────────────────────────────────┤
│  Backend                Mrt2Backend (macOS)  |  NullBackend      │
├──────────────────────────────────────────────────────────────────┤
│  magentart::core        RealtimeRunner → MLXEngine → MLX/Metal   │  MRT2 inference thread
└──────────────────────────────────────────────────────────────────┘
```

### 2.1 The load-bearing decision: `ghostband::core` is dependency-free

`src/core/` depends on **nothing but the C++20 standard library**. No JUCE, no MRT2, no
Metal, no Objective-C.

This is deliberate and it buys three things:

1. **It is testable on any machine, including CI.** MRT2 cannot build off Apple Silicon
   (`docs/MRT2_API_NOTES.md` §1). If our safety logic lived inside the JUCE app or behind
   the MRT2 headers, *none of it could be tested anywhere except a Mac*. Today the
   panic fade, limiter, underrun policy, and engine state machine are all covered by
   tests that run on Linux x86-64.
2. **The UI can be replaced without touching the engine**, which the brief explicitly
   asks for (§3).
3. **The safety stage survives MRT2 failure**, because it does not depend on MRT2 being
   alive. PANIC is our code, running on our thread, over a buffer we already own.

The price is one indirection (`IGenerationBackend`). Worth it.

---

## 3. Threading model

| Thread | Owner | Does | Must never |
|---|---|---|---|
| **Audio callback** | CoreAudio via JUCE | `backend->readStereo()` → `AiOutputStage::process()` → device | allocate, lock, log, touch UI, call MRT2 lifecycle |
| **MRT2 inference** | `magentart::core::RealtimeRunner` | `generate_frame()` @ 25 Hz, writes MRT2's ring buffer | (owned upstream) |
| **MIDI input** (one per open device) | JUCE `MidiInputCallback` | note on/off → `backend->noteOn/Off` (atomic); footswitch → action queue; PANIC latch | allocate, block, log |
| **Message/UI** | JUCE | model load, prompts, section changes, diagnostics polling | block on audio |
| **Text-encode worker** | MRT2 internal | MusicCoCa TFLite encode of prompts | (owned upstream) |

**Rule: the audio callback only ever calls two things** — `readStereo()` (documented
lock-free upstream) and our own `AiOutputStage::process()` (branch-free arithmetic over
preallocated state). Everything else is a parameter write from another thread into an
atomic.

### 3.1 Buffering

MRT2 already owns the generation ring buffer, so GhostBand does **not** add a second one —
a second buffer would only add latency and a second place for underruns to hide.

- MRT2 frame: 1920 samples = **40 ms** @ 48 kHz.
- `RingBuffer::kCapacity` = 8192 samples (~170 ms); `virtual_capacity` default
  2048 (~42.7 ms), tunable via `set_buffer_size()`.
- GhostBand's default: **3840 samples (~80 ms, two model frames)** — enough that a single
  late inference frame does not underrun, without stacking needless latency.

This is the one number most worth tuning on real hardware, and it is exposed in the
diagnostics view rather than buried.

### 3.2 Foot control crosses a thread boundary

A footswitch press arrives on a MIDI thread — and there may be several, one per open
device — but a section change touches the song model and writes prompts to the backend.
Doing that from a device callback races the 50 Hz timer that drives transitions.

The split:

| Step | Thread | Why there |
|---|---|---|
| Decode message → binding, match, debounce | MIDI | fixed-size, no allocation; `MidiMappingSet` holds one entry per action for life |
| **PANIC** | MIDI, immediately | an atomic latch over audio we already hold. Queueing the one control that must always work behind a message thread that might be busy would defeat its purpose |
| Everything else | queued, drained by the 50 Hz timer | section changes, intensity, AI on/off — all message-thread work |
| PANIC's belt-and-braces (backend mute, note release, log) | drained with the rest | idempotent; the fade has already happened |

`MidiMappingSet` is not internally synchronised. The engine guards it with a `SpinLock`
that the MIDI thread only ever **tries** — if the mapping screen holds it, that press is
dropped rather than blocking a device thread. Every critical section is a handful of
instructions over fixed-size storage, and none of them allocate.

**Cost:** up to 20 ms of extra latency on a queued action, against the ~128 ms of
transport delay a section change already carries. PANIC pays none of it.

**Consume, don't duplicate.** A message that matches a binding does not also reach
`MidiHarmonyState` — both edges of a bound note are swallowed, or harmony would see a
release with no press. That is why the defaults are CCs rather than notes; see
`KNOWN_ISSUES.md` §14.

**One entry point.** Hardware devices and the on-screen keyboard both call
`GhostBandAudioEngine::handleMidiMessage`, which matches foot control first and passes the
remainder to harmony. The on-screen keyboard exists so the control path is testable
without buying hardware, and that only holds while it is genuinely the same path — so new
control features are added *to* this function, never beside it.

---

## 4. The audio path, precisely

```
MRT2 RealtimeRunner::read_audio_stereo(L, R, n)   ── returns false on underrun
        │                                             (and zero-pads, per upstream)
        ▼
ghostband::core::AiOutputStage::process(L, R, n)
        │  1. PANIC / mute fade envelope   (30 ms default, 20–50 ms range)
        │  2. AI output level              (smoothed, dB)
        │  3. SafetyLimiter                (peak ceiling, soft knee)
        │  4. peak + RMS metering          (atomics, for UI)
        ▼
JUCE output channels  (default 1+2, routable to any stereo pair e.g. 3+4)
```

Underrun handling is a **policy in `SafetyMonitor`**, not an ad-hoc branch:

- `read_audio_stereo` returning `false` increments an atomic underrun counter.
- Sustained underruns (default: >8 within 2 s) trip `SafetyMonitor` into `Degraded`.
- `Degraded` fades the AI out over 30 ms, raises a visible warning, and stops pulling
  from a backend that is evidently not keeping up.
- Recovery is **explicit** — the performer restarts generation. We never silently
  un-mute mid-song, because a surprise band re-entry is worse than no band.

This satisfies the brief's §5 failure philosophy: the guitar and voice are untouched by
any of it, because they never route through GhostBand at all (§7 below).

---

## 5. PANIC

PANIC is `ghostband::core`, not MRT2.

```cpp
outputStage.panic();   // atomic store; safe from UI, MIDI, or key handler
```

- Fade to silence over **30 ms** (configurable 20–50 ms) — long enough to avoid a click,
  short enough to be perceived as immediate.
- Implemented in `FadeEnvelope` as a linear ramp on a preallocated per-block gain, so it
  is correct regardless of block size and costs one multiply per sample.
- Works **even if the MRT2 inference thread is hung**, because it gates audio we have
  already read. This is the entire reason it does not delegate to `set_mute()`.
- `set_mute(true)` on the backend is issued afterwards as a secondary measure, from the
  message thread, best-effort.
- Reachable from: on-screen button, keyboard, MIDI mapping, and Performance Mode.

---

## 6. AI Intensity — a GhostBand-owned macro

MRT2 exposes **no** density or intensity parameter (`docs/MRT2_API_NOTES.md` §6.3).
The brief (§16) requires one, and requires that it not be a volume control. So GhostBand
defines it, and documents the mapping rather than hiding it:

`AI Intensity ∈ [0, 1]` maps to:

| Underlying control | Low (0.0) | High (1.0) | Rationale |
|---|---|---|---|
| **Section style prompt** | sparse-leaning slot | full-leaning slot | The dominant term. Intensity biases the blend weights between the section's prompt and a sparser/denser variant. Style text is by far MRT2's strongest arrangement lever. |
| `set_drumless(bool)` | `true` below 0.15 | `false` | A binary that genuinely removes an instrument class — the clearest "less band" signal available. |
| `set_cfg_drums(float)` | 0.5 | 2.0 | Guidance weight on drums; scales drum presence continuously above the drumless threshold. |
| `set_cfg_musiccoca(float)` | 2.0 | 4.0 | Higher guidance = more literal adherence to a dense style prompt. Centred on upstream's 3.0 default. |
| `set_temperature(float)` | 0.9 | 1.1 | Mild. Keeps low intensity more predictable, which is what "restrained" should mean live. |

Explicitly **not** touched by Intensity: `set_volume_db` (that is `AI Output Level`) and
`set_cfg_notes` (that governs harmony following, which must stay locked to the performer
regardless of arrangement density).

> This mapping is a hypothesis derived from the parameter semantics, **not** a tuned
> result. It has not yet been evaluated against generated audio — that requires Apple
> Silicon. It is isolated in `IntensityMacro` precisely so it can be retuned without
> touching anything else. Tracked in `KNOWN_ISSUES.md` §3.

---

## 7. Signal routing — GhostBand is additive, never in the way

```
Vocal mic ─────────────────────────────► Audio interface ──► FOH ch 1
Acoustic guitar ───────────────────────► Audio interface ──► FOH ch 2
                                    ┌──► GhostBand (AI) ──────► FOH ch 3+4
MacBook ────────────────────────────┘
```

The performer's voice and guitar **do not pass through GhostBand**. If the app crashes, the
laptop sleeps, or MRT2 dies, the show continues at full quality. This is an architectural
guarantee, not a feature — GhostBand has no input path in the primary signal chain at all.

Guitar Follow (Phase 3) will *listen* to a guitar input, but as a **tap**, never as an
insert.

---

## 8. Prompt transitions via blend weights

MRT2 has no "crossfade prompts over N ms" call, but it has 6 prompt slots with atomic,
automatable blend weights, and text encoding is asynchronous
(`docs/MRT2_API_NOTES.md` §6.2).

GhostBand's approach:

1. **At song load**, encode every distinct section prompt into its own slot. Encoding
   latency is paid while nobody is playing.
2. **At a section change**, ramp `set_blend_weights()` from the old slot to the new one
   over the section's configured transition time (Instant / 250 ms / 500 ms / 1 s / 2 s).
   No encode, no stall, no audio discontinuity.
3. Songs with more than 6 distinct prompts recycle slots by encoding the upcoming
   section's prompt during the current section.

The ramp is driven from a timer on the control plane at ~100 Hz, not from the audio
thread — blend-weight changes affect the *next generated frame* (40 ms granularity), so
sample-accurate ramping would be false precision.

---

## 9. Module inventory and current status

| Module | Status | Notes |
|---|---|---|
| `core/GhostBandConstants.h` | ✅ implemented | 48 kHz, 1920-sample frame, mirrors MRT2 |
| `core/EngineState` | ✅ implemented, tested | Unloaded→Loading→Ready→Running→Error |
| `core/FadeEnvelope` | ✅ implemented, tested | PANIC / mute ramps |
| `core/SafetyLimiter` | ✅ implemented, tested | peak ceiling, soft knee, lookahead-free |
| `core/SafetyMonitor` | ✅ implemented, tested | underrun policy → Degraded |
| `core/AiOutputStage` | ✅ implemented, tested | fade → level → limiter → metering |
| `core/Diagnostics` | ✅ implemented, tested | lock-free counters/snapshot |
| `core/Logging` | ✅ implemented, tested | structured, RT-thread-safe (no alloc) |
| `core/IGenerationBackend` | ✅ interface defined | modelled on verified MRT2 API only |
| `backend/NullBackend` | ✅ implemented, tested | honest silence; reports "no model" |
| `backend/Mrt2Backend` | ⚠️ **written, never compiled** | macOS only; see below |
| `app/` JUCE host | ⚠️ **written, never compiled** | macOS only; see below |
| Songs / Sections | ✅ implemented, tested | `Song`, `SectionController`, `PerformanceEngine` |
| Setlists | ❌ not started | Phase 2.7 |
| MIDI harmony input | ✅ works on hardware | `MidiHarmonyState`; on-screen keyboard uses the same path |
| Foot control / MIDI Learn | 🟡 core tested, app layer **never compiled** | `MidiMappingSet`; see `KNOWN_ISSUES.md` §15 |
| Guitar Follow | ❌ not started | Phase 3, experimental |
| Persistence | 🟡 foot mappings only | versioned schema is Phase 2.8 |

**⚠️ is load-bearing.** `Mrt2Backend` and the JUCE app are macOS/Apple-Silicon targets.
This development container is Linux x86-64, where MRT2 refuses to configure by design and
JUCE's audio/GUI dependencies are absent. That code is written against the pinned upstream
headers but **has not been compiled or run**, and must not be described as working until
it is. See `KNOWN_ISSUES.md` §1.

---

## 10. Build layout

```
CMakeLists.txt              # ghostband_core + tests everywhere; app + MRT2 on APPLE only
src/core/                   # portable. no JUCE, no MRT2, no platform code.
src/backend/Mrt2Backend.*   # macOS: wraps magentart::core::RealtimeRunner
src/backend/NullBackend.*   # portable: silence + "no model loaded"
src/app/                    # macOS: JUCE host, audio device, minimal Phase 0 UI
tests/                      # portable unit tests, run on every build
```

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release   # core + tests everywhere
cmake --build build -j
ctest --test-dir build --output-on-failure
```

On macOS, add `-DGHOSTBAND_BUILD_APP=ON` and point `-DMAGENTA_RT_DIR=` at a checkout of
`magenta/magenta-realtime`.

---

## 11. Deliberate non-choices

- **No second ring buffer.** MRT2 has one; adding ours would add latency and hide faults.
- **No Python in the live path.** Python is setup/tooling only (`mrt models init`).
- **No audio-thread logging.** Counters are atomics; text logging happens on pollers.
- **No automatic model switching.** The brief forbids it and it would stall generation.
- **No `set_audio_prompt(path)`.** Upstream marks it as returning a fake embedding.
- **No re-implementation of MRT2's inference thread, gain smoothing, or recording.**

---

## 12. Persistence

Two file kinds, both line-oriented text under `~/Documents/GhostBand`:

| | Extension | Location | Why there |
|---|---|---|---|
| Songs | `.ghostsong` | `Documents/GhostBand/Songs` | the performer's own work — visible in Finder, backed up, portable to another machine |
| Setlists | `.ghostset` | `Documents/GhostBand/Setlists` | same |
| Foot-controller mappings | `.txt` | Application Support | app state, not content |

**Not JSON.** A parser is the part of a file format most likely to throw, and this one
runs when a performer opens a song — sometimes at soundcheck. `LoadResult` gives it one
failure mode: a message, a line number, and a flag distinguishing "saved by a newer
GhostBand" from corruption, because the fix differs completely. The format is also
repairable in a text editor, which matters the one time a file is half-written by a crash.

**Versioned from the first release.** The header carries a schema version; a file from a
newer build is refused rather than half-read; `migrate()` exists with one branch to add per
future version. Building this later would mean designing migration under time pressure
against files that already hold a performer's set.

**Numbers are written and read in the C locale.** `std::to_string(0.35f)` emits `0,35`
under a French system locale and reads back as `0` — a song that silently loses its
intensities when carried to another laptop.

**Writes are atomic**: temp file, then move into place. `File::replaceWithText` truncates
first, so a failure part-way through would destroy the previous version.

### 12.1 Setlists reference songs, they do not embed them

Embedding would make a setlist self-contained, which is tempting for stage reliability.
It also means editing a song leaves the set playing a stale copy with nothing saying so. A
missing file is loud and fixable; a silently stale arrangement is neither.

Consequences, all deliberate:

- A song that fails to load **keeps its place** in the running order as a gap, so the
  numbering still matches the paper setlist taped to the monitor.
- The gap is reachable, and the stage screen says `SONG FILE MISSING` in large type rather
  than showing an empty screen that looks like a crash.
- Missing songs are named using the title cached at save time, not the file name.
- A set that is 11 of 12 songs still loads. Refusing it would hide which 11 are fine.
- Songs resolve from the setlist's own folder first, then the shared songs directory, so a
  set carried to another machine in one folder still opens.
