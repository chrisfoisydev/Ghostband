# GhostBand — Implementation Plan

Status legend: ✅ done · 🟡 in progress · ⚠️ written but unverified · ❌ not started · 🚫 blocked

Last updated: 2026-08-08

---

## Phase 0 — Technical spike

**Goal:** *Magenta RealTime 2 generates stable real-time audio inside our standalone
application.* Not "the UI exists."

> **✅ MET, 2026-08-09.** Audible band on an Apple M2 Pro at 17.17 ms / 40 ms frame budget.

| # | Task | Status |
|---|---|---|
| 0.1 | Inspect environment, confirm target hardware | ✅ done — **result: not Apple Silicon**, see below |
| 0.2 | Inspect pinned MRT2 repo + docs + C++ examples | ✅ done — `docs/MRT2_API_NOTES.md` |
| 0.3 | Identify exact APIs for streaming, MIDI steering, prompts, audio, model load/state | ✅ done — `docs/MRT2_API_NOTES.md` §4–6 |
| 0.4 | Document native vs. GhostBand-implemented | ✅ done — `docs/MRT2_API_NOTES.md` §7 |
| 0.5 | `ARCHITECTURE.md`, `IMPLEMENTATION_PLAN.md`, `CLAUDE.md` | ✅ done |
| 0.6 | `THIRD_PARTY_NOTICES.md`, `KNOWN_ISSUES.md`, `STAGE_READINESS.md` | ✅ done |
| 0.7 | Repository + CMake setup | ✅ done |
| 0.8 | Portable `ghostband::core`: state machine, fade, limiter, safety monitor, diagnostics, logging | ✅ done, **tested and passing** |
| 0.9 | `IGenerationBackend` abstraction + `NullBackend` | ✅ done, tested |
| 0.10 | `Mrt2Backend` wrapping `magentart::core::RealtimeRunner` | ✅ **compiles and runs** — loads `mrt2_small`, streams via `RealtimeRunner` |
| 0.11 | JUCE host: audio device, 48 kHz stereo out, START/STOP/PANIC, diagnostics | ✅ **builds and runs** — `GhostBand.app`, 48 kHz/512, diagnostics live |
| 0.12 | Build + run official `hello_mrt2` | ✅ **done on target hardware** (2026-08-09) |
| 0.13 | Confirm real-time inference with `mrt2_small` | ✅ **done** — **17.17 ms per 40 ms frame (42.9%)** inside GhostBand, on an **Apple M2 Pro** |
| 0.14 | Verify 48 kHz stereo output from a generated file | ✅ **done** — `out.wav`, 4.00 s, plays correctly as music |
| 0.15 | Measure generation latency, underruns, CPU/GPU, memory | 🟡 **partial** — frame time and underruns measured; memory still unmeasured |

### Setup gotchas found on the first real build (2026-08-09)

Both cost real time and neither is in upstream's README. Recorded so the next machine is
cheaper to set up.

1. **The Metal Toolchain is a separate Xcode download.** MLX compiles its own Metal
   shaders; Xcode 16+ no longer bundles the `metal` compiler. The build dies with
   `cannot execute tool 'metal' due to missing Metal Toolchain`. Fix:
   ```bash
   xcodebuild -downloadComponent MetalToolchain
   ```

2. **Upstream's root CMake configures every example**, including SuperCollider, Max, PD
   and three npm/React UIs — even when you ask only for the `hello_mrt2` target. On a
   disk with ~22 GB free this exhausted space during configure. `scripts/trim-mrt2.py`
   comments out the subdirectories GhostBand never links against, leaving `core` and
   `hello_mrt2`. Reversible via `--restore`.

   Configure still took **649 s** after trimming; TFLite clones the whole TensorFlow
   repository and that is unavoidable. Budget ~25 GB free and an hour for a cold setup.

### The remaining blocker, stated plainly

**What we expected:** a macOS Apple Silicon machine, per the brief's §2 and §34.2.

**What we got:** `Linux 6.18.5 x86_64`, Intel Xeon, no Metal, no Objective-C toolchain,
and no ALSA/freetype headers for JUCE either.

**What MRT2 actually supports:** the C++ engine is macOS-only *by explicit design* —
upstream's root `CMakeLists.txt` calls `message(FATAL_ERROR ...)` on `NOT APPLE`, and
`magentart_core` links MLX/Metal/Accelerate/Foundation. There is no x86 or Linux
inference path in the C++ engine. (A Python JAX path exists for *offline*, non-real-time
inference on NVIDIA GPUs; there is no GPU here either, and the brief explicitly forbids
building the live engine around Python.)

**Impact:** tasks 0.12–0.15 cannot be performed *in the development container* by any
means. They are not "hard" there — they are impossible. Tasks 0.12 and 0.14 have since
been closed by running on the target Mac; 0.10, 0.11 and 0.15 still require it.

**Workaround chosen:** split the spike so that everything not requiring Metal is built
*and actually verified* now, and everything requiring Metal is written against pinned
upstream headers and clearly flagged unverified. Concretely, all failure-path logic —
PANIC, the limiter, underrun policy, the engine state machine — lives in dependency-free
`ghostband::core` and is covered by tests that run and pass here.

**Does the workaround compromise live reliability?** No — it improves it. The safety
stage is now independent of MRT2 by construction, so PANIC works even when the inference
thread is wedged. The residual risk is confined to the thin `Mrt2Backend` adapter and the
JUCE host, which must be compiled and run on a Mac before any stage use.

**To close it,** on an Apple Silicon Mac:

```bash
# 1. MRT2 resources + weights (not bundled — see THIRD_PARTY_NOTICES.md)
uv venv --python 3.12 && source .venv/bin/activate
uv pip install "magenta-rt[mlx]"
mrt models init && mrt models download

# 2. Upstream sanity check (task 0.12–0.14)
git clone https://github.com/magenta/magenta-realtime.git
cd magenta-realtime && cmake . -B build && cmake --build build --target hello_mrt2 -j10
./build/examples/hello_mrt2/hello_mrt2 \
    ~/Documents/Magenta/magenta-rt-v2/models/mrt2_small/mrt2_small.mlxfn \
    ~/Documents/Magenta/magenta-rt-v2/resources 100 \
    --prompt "warm organic indie folk ensemble, instrumental"
# expect: out.wav, 4.00 s, 48 kHz stereo float

# 3. GhostBand (tasks 0.10–0.11, 0.15)
cmake -B build -DGHOSTBAND_BUILD_APP=ON -DMAGENTA_RT_DIR=/path/to/magenta-realtime
cmake --build build -j && ctest --test-dir build --output-on-failure
```

---

## Phase 1 — Playable instrument

*End state: plug in a MIDI keyboard, hold a chord, hear MRT2 build an ensemble on it.*

> **🟡 SUBSTANTIALLY MET, 2026-08-09.** Holding a chord steers the generated band, confirmed
> by ear on an Apple M2 Pro. Reached via the on-screen keyboard rather than hardware — the
> same `MidiHarmonyState` path a controller uses, so the remaining risk is device I/O, not
> harmony logic. Latency is perceptible but musical (see below).

| # | Task | Status |
|---|---|---|
| 1.1 | MIDI input manager, device enumeration + hot-plug | ⚠️ compiles and runs; **no hardware controller has been connected yet** |
| 1.2 | MIDI note state → `set_note_on/off`, sustain pedal, all-notes-off | ✅ core done, **tested** (`MidiHarmonyState`) |
| 1.3 | Chord naming for display (labels are display-only; MRT2 gets raw notes) | ✅ core done, **tested** (`ChordNamer`) |
| 1.4 | Prompt editor + async encode status surfacing | ❌ |
| 1.5 | AI on/off, output level, model selector, audio-device selector | ❌ — selector must **gate `mrt2_base` on hardware**; not real-time on M2 Pro (see MRT2_API_NOTES §1) |
| 1.6 | `IntensityMacro` (see `ARCHITECTURE.md` §6) + retuning on real audio | ❌ |
| 1.7 | On-screen / computer-key keyboard, so harmony is testable without hardware | ✅ **works** |
| 1.8 | Measure and characterise harmony latency | ❌ — perceptible by ear, never measured |

## Phase 2 — Live performer

*End state: perform an entire set without touching the MacBook.*

Songs · Sections · per-section prompts + intensity · prev/next · Performance Mode ·
MIDI foot controller · MIDI Learn · setlists · versioned persistence with migrations ·
Song Map harmony mode. All ❌.

The prompt-slot pre-encoding strategy (`ARCHITECTURE.md` §8) is a Phase 2 prerequisite,
not an optimisation — section changes are unusable without it.

## Phase 3 — Guitar Follow (experimental)

Guitar audio tap · polyphonic chord estimation · confidence + hysteresis + temporal
smoothing · hold-last-chord under uncertainty · detected-harmony display · MIDI
conversion → MRT2 steering. All ❌.

Must not destabilise the Phase 1 MIDI path. Research note: MRT2's `prefill_state()`
seeds the model from *raw audio*, which may make audio-driven following viable without
transcription at all — worth an experiment before committing to a chord detector.

## Phase 4 — Stage reliability

Long-duration soak (10/30/60 min) · watchdogs · error recovery · device-disconnect
recovery · richer diagnostics · buffer tuning on real hardware · benchmarking. All ❌.

## Phase 5 — Product polish

Onboarding · presets · Song Editor · performance-state restore · async recording writer ·
import/export · model management · packaging + notarisation. All ❌.

---

## Immediate next actions

Phase 0 is closed. Before adding features, two cheap things are worth doing while the
setup is fresh:

1. **A 10-minute soak** — leave it generating and watch underruns, `Health`, and memory.
   The brief is blunt that "a live performance application that works for 90 seconds is
   not finished". This is the smallest version of that test and it costs nothing but time.
2. **Confirm PANIC audibly** — press Escape while the band plays. It should fade in ~32 ms
   with no click. It is unit-tested, but the pedal is the one control that must never
   surprise anyone.

Then Phase 1, starting with MIDI input (1.1–1.2) — the shortest path to the brief's stated
milestone: *hold a chord on a keyboard, hear MRT2 build an ensemble on it.*
