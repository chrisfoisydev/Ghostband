# MRT2 API Notes — Source-Verified Inventory

**Purpose:** This file records what Magenta RealTime 2 *actually* exposes, verified by
reading the upstream source. Per `CLAUDE.md`, no GhostBand code may assume an MRT2 API that
is not listed here.

**Upstream pinned at:** `magenta/magenta-realtime` commit `694a545e4ba0b88bf1150137b129582166d3e07f`
("updating changelog and version (#88)", 2026-07-30).

**Verification method:** shallow clone of the repo, direct reading of
`core/include/magentart/*.h`, `core/src/*`, `examples/hello_mrt2/main.cpp`,
`CMakeLists.txt`, `README.md`, `MODEL.md`. Where the docs and source disagree, the
source wins and the discrepancy is noted below.

---

## 1. Platform reality (hard constraint)

The MRT2 C++ engine is **macOS / Apple Silicon only**, enforced at configure time in the
upstream root `CMakeLists.txt`:

```cmake
project(MagentaRT LANGUAGES C CXX OBJCXX)
...
if(NOT APPLE)
  message(FATAL_ERROR
    "magenta-rt-v2's C++ build is macOS-only (MLX/Metal + Apple frameworks).\n"
    "For the Python JAX backend, skip CMake and use `pip install -e \".[jax]\"`.")
endif()
```

`magentart_core` links `mlx`, `sentencepiece`, `tensorflow-lite`, and
`-framework Metal -framework Accelerate -framework Foundation`. The inference loop
requires an Objective-C autorelease pool per iteration
(`core/include/magentart/detail/autorelease_pool.h`).

**Consequence for GhostBand:** the real-time engine is inherently macOS/Apple Silicon.
This matches our V1 target, but it means *no part of the MRT2 path can be compiled or
executed on Linux/x86 CI.* See `KNOWN_ISSUES.md` §1.

### Real-time capability by device (from upstream `README.md`)

| Device | `mrt2_small` (230M) | `mrt2_base` (2.4B) |
|---|---|---|
| M5 Max / M3 Max / M2 Max / M4 Pro | ✅ | ✅ |
| M2 Pro / M1 Pro / M4 Air / M3 Air / M1 Air | ✅ | ❌ |

This directly justifies our product policy: `mrt2_small` = **Performance** (default),
`mrt2_base` = **High Quality** (opt-in, gated on hardware).

### Measured baseline (2026-08-09)

| Machine | Model | Frame time | Budget | Headroom |
|---|---|---|---|---|
| **Apple M2 Pro** (MacBook Pro) | `mrt2_small` | **17.17 ms** | 40 ms | **42.9% used, ~2.3x real time** |

Measured in-app via `EngineMetrics::total_ms` (all of it in `transformer_ms`), at a
48 kHz / 512-sample device buffer, generation buffer 3328/3840 samples.

**`mrt2_base` is NOT real-time on this machine.** Upstream's table marks M2 Pro ❌ for
base, and base is ~10x the parameters (2.4B vs 230M) against a frame budget already 43%
consumed by small. GhostBand must therefore *gate* the High Quality option on detected
hardware rather than merely offering it — offering a model that cannot keep up would be
exactly the "control that looks live and does nothing" that `CLAUDE.md` rule 2 forbids.
Tracked for Phase 1 (model selector).

---

## 2. The two C++ classes we consume

Both live in `namespace magentart::core`.

- **`MLXEngine`** (`core/include/magentart/mlx_engine.h`) — the raw inference pipeline.
  Not thread-safe as a whole. `generate_frame()` is blocking and synchronous.
- **`RealtimeRunner`** (`core/include/magentart/realtime_runner.h`) — audio-thread-safe
  wrapper around `MLXEngine`. **This is what GhostBand uses.**

`RealtimeRunner`'s own header documents that it adds:

> - An inference thread that calls `generate_frame` on a 25 Hz cadence.
> - Stereo ring buffers so the audio thread can pull arbitrary block sizes.
> - Volume / mute / bypass smoothing (one-pole) applied on the audio thread.
> - A MIDI-gate envelope that attenuates output when no notes are held.
> - PromptSurface controls: a 2D plane blends MusicCoCa prompts by inverse-distance
>   weighting of user-controlled (x, y) coordinates per prompt slot.

**This is a significant finding: MRT2 already solves a large part of what our brief
assumed we would build ourselves** (inference thread, SPSC ring buffers, underrun
counting, gain smoothing, recording). GhostBand should *not* reimplement these. See
§7 "Native vs. GhostBand-implemented".

---

## 3. Format constants (verified in `mlx_engine.h`)

```cpp
inline constexpr std::size_t kFrameSamples = 1920;  ///< 48 kHz / 25 Hz
inline constexpr std::size_t kNumChannels  = 2;
inline constexpr std::size_t kMaxPrompts   = 6;
inline constexpr std::size_t kMaxPCAComponents = 6;
inline constexpr std::size_t kNumRVQLevels = 12;
inline constexpr int kMusicCoCaEmbeddingDim = 768;
```

- **48 kHz stereo confirmed.** One model frame = 1920 samples = **40 ms** at 25 Hz.
- **Max 6 simultaneous text/audio prompts** with blend weights. This is the mechanism
  we will use for section-to-section style transitions (§6 below).

`RingBuffer::kCapacity == 8192` samples (~170 ms), with a tunable
`virtual_capacity_` defaulting to **2048 samples (~42.7 ms)** — settable via
`RealtimeRunner::set_buffer_size()`.

---

## 4. Lifecycle API (`RealtimeRunner`)

Controller-thread only, serialized internally by `lifecycle_mutex_`:

```cpp
bool init_assets(const char* resource_dir);   // loads TFLite MusicCoCa assets
bool load_musiccoca_model(const char* resource_dir, const char* subfolder);
bool load_model(const char* mlxfn_path);      // the .mlxfn streaming model
bool load_prefill_model(const char* spectrostream_mlxfn_path,
                        const char* prefill_mlxfn_path);
void unload();
void start();   // starts the 25 Hz inference thread
void stop();
bool is_loaded() const;
void reset();                 // stop, reset model state, clear rings, restart
void reset_to_factory();
bool save_state(const char* path);
bool load_state(const char* path);
```

Asset layout expected (from `hello_mrt2/main.cpp` + `examples/common/cpp/magenta_paths.h`):

```
~/Documents/Magenta/magenta-rt-v2/
  resources/musiccoca/          # TFLite assets, via `mrt models init`
  models/mrt2_small/mrt2_small.mlxfn
  models/mrt2_base/mrt2_base.mlxfn
```

Installed by the Python CLI: `mrt models init` then `mrt models download`.
**We do not bundle weights** (see `THIRD_PARTY_NOTICES.md`).

---

## 5. Audio output API

```cpp
/// Pull `count` stereo samples. Applies bypass, volume/mute smoothing, reset
/// envelope, and (if enabled) the MIDI gate envelope. Returns `false` if the
/// ring buffer underran (caller still gets `count` samples, zero-padded).
bool read_audio_stereo(float* destL, float* destR, std::size_t count,
                       bool blocking = false);
```

Critical properties, quoting the header:

- Returns **`false` on underrun**, and **zero-pads** rather than leaving garbage.
  This is our primary underrun signal.
- `blocking=true` "waits up to one ring-buffer worth of samples — intended for
  offline render only. **Never pass `true` from the audio callback.**"
- It is "Lock-free except for its internal ring buffer's atomics." → **safe to call
  directly from the JUCE audio callback.**

---

## 6. Control APIs relevant to GhostBand's product model

### 6.1 Harmony steering (MIDI) — CONFIRMED NATIVE

```cpp
void set_note_on(int n);
void set_note_off(int n);
```

From `mlx_engine.h`: *"Mark MIDI note `n` as pressed. 0 ≤ n < 132."*
From `midi_note_tracker.h`:

```cpp
constexpr int kNumStandardMidiNotes = 128;
constexpr int kNumDrumTriggers      = 4;
constexpr int kTotalPitches         = 132;  // 128 MIDI + 4 drum triggers
```

Note state machine (`NoteState`): `NOTE_IDLE`, `NOTE_ONSET`, `NOTE_SUSTAIN`,
`NOTE_ONSET_RELEASED`. The last exists so a note-off arriving before the inference
thread consumes the onset still produces exactly one observed onset — i.e. **MRT2
already latches very short notes correctly.** We must not debounce them away upstream.

`MODEL.md` confirms the model input: *"(MIDI) 128-dim multihot vector representing the
state of each MIDI pitch during this frame (0 = Off, 1 = Sustain, 2 = Onset, 3 =
Sustain or onset, model decides)."*

Onset encoding is selectable:

```cpp
/// 0 = Mask onsets (default) — off=0, on=3
/// 1 = Unmask onsets — off=0, onset=2, continuation=1
void set_onset_mode(int mode);
```

> **Product note:** mode 0 lets the model decide phrasing/rearticulation; mode 1 forces
> the model to respect our exact onsets. For a *following* band, mode 0 is the more
> musical default. Marked as a tunable, not exposed in V1 UI.

**Verdict: MIDI harmony following is natively supported. No invention required.**

### 6.2 Style / prompt control — CONFIRMED NATIVE, with a caveat

```cpp
void set_text_prompt(const std::string& t);
void set_text_prompts(const std::vector<std::string>& t, const std::vector<float>& w);
void set_blend_weight(int i, float w);          // automatable, atomic
void set_blend_weights(const float* w, int n);  // batch, bumps generation once
bool reblend_musiccoca_tokens(const float* weights, int count,
                              const float* pca_coeffs = nullptr, int pca_count = 0);
int  get_text_encoder_status() const;  // 0 idle, 1 fetching, 2 success, 3 error
int  get_quantizer_status() const;
```

Text encoding is **asynchronous** — it runs MusicCoCa (TFLite) on a worker thread.
`hello_mrt2` polls until both statuses leave state `1`:

```cpp
while (engine.get_text_encoder_status() == 1 || engine.get_quantizer_status() == 1) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
}
```

**This is the single most important fact for GhostBand's section changes.** Setting a new
text prompt at a section boundary incurs an async encode. We must not stall the
performance waiting for it.

**GhostBand's design consequence (§15 of the brief — prompt transitions):**
MRT2 does *not* expose a "crossfade to a new prompt over N ms" call. But it exposes
exactly the primitive needed to build one: up to **6 prompt slots with atomic,
automatable blend weights**. Therefore:

> GhostBand pre-loads every section's style prompt into a distinct prompt slot at
> **song load time** (when encoding latency is free), then a section change is a
> pure **blend-weight ramp** between slots — no encode, no stall, sample-accurate
> timing, and it supports the brief's Instant / 250 ms / 500 ms / 1 s / 2 s
> transition times.

Limit: `kMaxPrompts = 6` slots. Songs with more than 6 *distinct* prompt strings need
slot recycling (encode the next one during the previous section). Tracked in
`KNOWN_ISSUES.md` §4.

### 6.3 Arrangement / "intensity" — NO SINGLE NATIVE PARAMETER

There is **no** MRT2 density/intensity/energy knob. What exists:

```cpp
void set_cfg_musiccoca(float v);  // classifier-free guidance on style   (hello_mrt2 default 3.0)
void set_cfg_notes(float v);      // guidance on MIDI conditioning       (default 5.0)
void set_cfg_drums(float v);      // guidance on drums                   (default 1.0)
void set_drumless(bool on);       // hard drum removal
void set_temperature(float t);    // default 1.0
void set_top_k(int k);            // default 100
void set_unmask_width(int w);
void set_seed_rotation(int r);
```

**Verdict: the brief's `AI INTENSITY` macro must be built by GhostBand.** Its mapping is a
GhostBand-owned design decision and is specified in `ARCHITECTURE.md` §6. It is emphatically
*not* an audio gain — `AI OUTPUT LEVEL` (→ `set_volume_db`) is the gain.

### 6.4 Output control / PANIC primitives

```cpp
void set_volume_db(float v);        // one-pole smoothed on the audio thread
void set_mute(bool m);              // smoothed, not a hard gate
void set_bypass(bool b);
void set_host_bypass(bool b);
void set_midi_gate_enabled(bool e); // attenuate when no notes held
void set_latency_comp(bool c);
```

`set_mute` is smoothed by `smoothed_gain_`, but **the smoothing constant is internal and
not settable**, so we cannot guarantee the brief's 20–50 ms PANIC fade through it.

**GhostBand's design consequence:** PANIC is implemented in *our* output stage as an
explicit, specified fade (`FadeEnvelope`, default 30 ms), applied after
`read_audio_stereo`. `set_mute(true)` is issued as a *secondary* belt-and-braces step.
This also means PANIC keeps working even if the MRT2 engine is wedged, which is the
whole point of §5 of the brief.

### 6.5 Metrics — CONFIRMED NATIVE

```cpp
struct EngineMetrics {
    float transformer_ms = 0;
    float total_ms = 0;
    std::size_t buffer_available = 0;
    std::size_t buffer_capacity = RingBuffer::kCapacity;
    int transport_flags = -1;
    std::uint64_t dropped_frames = 0;  ///< cumulative real-time underruns
};
EngineMetrics get_metrics() const;
void reset_dropped_frames();
std::vector<std::string> get_logs();
```

`total_ms` vs. the 40 ms frame budget is the **real-time headroom signal** — if
`total_ms` trends above 40 ms, generation cannot keep up and we are heading for
underruns. This drives GhostBand's `SafetyMonitor` and the "Generation: Stable" indicator.

### 6.6 Recording — CONFIRMED NATIVE

```cpp
void start_recording(); void stop_recording(); void clear_recording();
bool get_recorded_audio(float* destL, float* destR, std::size_t start, std::size_t count) const;
std::size_t get_recorded_sample_count() const;
std::vector<float> get_waveform_peaks(int num_buckets) const;
```

Records the **pre-our-output-stage** AI signal into an in-memory buffer. Note this is RAM
growth, not an async disk writer — for the brief's §21 "must never jeopardise generation
stability", a long set would need our own async writer. Deferred to Phase 5; noted in
`KNOWN_ISSUES.md` §5.

### 6.7 Prefill / state seeding — NATIVE, useful later

```cpp
bool prefill_state(const float* audio, int num_samples, log_cb);
bool prefill_silence(int duration_frames = 550, log_cb);
```

`prefill_silence` gives a clean "model has only ever heard silence" state — the right
way to start a song without bleed from the previous song. `prefill_state` seeds the
model from real audio, which is a compelling future path for *acoustic* guitar
following (§13) that does not require chord transcription at all. Recorded for Phase 3
research; not used in Phase 0.

---

## 7. Native vs. GhostBand-implemented (the §1.8 deliverable)

| Capability | MRT2 native | GhostBand must build |
|---|---|---|
| 48 kHz stereo streaming generation | ✅ `RealtimeRunner` | — |
| Inference thread @ 25 Hz | ✅ | — |
| SPSC ring buffer, arbitrary block reads | ✅ | — |
| Underrun detection + count | ✅ `dropped_frames`, `read_audio_stereo` → false | Surfacing, policy, auto-recovery |
| Text-prompt style control | ✅ (async encode) | Pre-encoding strategy, slot management |
| Multi-prompt blending w/ weights | ✅ 6 slots, atomic | Timed transition ramps (250 ms…2 s) |
| MIDI note/chord steering | ✅ `set_note_on/off`, 132 pitches | Device I/O, hot-plug, sustain, all-notes-off |
| Model load / unload / reset | ✅ | State machine, error recovery, watchdog |
| Volume / mute / bypass | ✅ smoothed | **PANIC with specified 20–50 ms fade** |
| Generation metrics | ✅ `EngineMetrics` | Diagnostics screen, headroom policy |
| Audio recording | ✅ in-memory | Async disk writer |
| Model state save/load | ✅ | — |
| **Intensity / arrangement macro** | ❌ | **All of it** (§6.3) |
| **Output limiter / peak safety** | ❌ | **All of it** |
| **Audio device I/O, routing, MIDI I/O** | ❌ | **All of it** (JUCE) |
| **Songs, sections, setlists, persistence** | ❌ | **All of it** |
| **Chord detection from guitar audio** | ❌ | **All of it** (Phase 3) |
| **Chord naming / display** | ❌ | **All of it** |

---

## 8. Documentation vs. source discrepancies

1. **`set_audio_prompt(int index, const std::string& path)` is a stub.** The header
   carries an upstream `TODO(public-release)` stating that when `path` is non-empty it
   "writes a deterministic fake embedding rather than decoding the file." **GhostBand must
   not call this.** Use `set_audio_prompt_samples()` (which really encodes) instead.
   This is exactly the kind of thing our "never fake functionality" rule exists for.

2. **`README.md` C++ quickstart says `uv pip install "cmake<3.28"`** while the root
   `CMakeLists.txt` declares `cmake_minimum_required(VERSION 3.27)`. The pin appears to
   be an upper bound for an unrelated reason; 3.27 is the true floor. Use CMake ≥ 3.27
   and be prepared for the `<3.28` constraint to matter on the MRT2 subbuild.

3. **`MODEL.md` says SpectroStream has "64 RVQ depth"** for the codec, while the
   streaming model constant is `kNumRVQLevels = 12` and `MODEL.md`'s own LLM section says
   the transformer emits "12 RVQ tokens" per frame. The 64 refers to the full codec; 12
   is what the LLM generates. Not a contradiction, but the surface reading is confusing.
   GhostBand only ever deals with decoded audio, so this does not affect us.

4. **`MODEL.md` describes the MIDI input as a "128-dim multihot vector"**, but the C++
   tracker allocates **132** pitches (128 + 4 drum triggers). Source wins: valid input
   range is `0 ≤ n < 132`, with 128–131 being drum triggers rather than pitches. GhostBand
   clamps MIDI note input to 0–127 and reserves 128–131 for future explicit drum
   triggering.

---

## 9. Verification ledger

Everything in §1–§8 is verified by reading pinned upstream source. Execution status,
updated after the first build on target hardware (2026-08-09):

**Verified by execution, on an Apple Silicon Mac:**

- ✅ `hello_mrt2` builds against `magentart::core` and runs.
- ✅ `mrt2_small` loads from a `.mlxfn` directory, with MusicCoCa TFLite assets from
  `resources/`, and generates coherent instrumental music from a text prompt.
- ✅ Output is a valid 48 kHz stereo WAV — 100 frames produced exactly 4.00 s, which
  confirms the `kFrameSamples = 1920` @ 25 Hz arithmetic in §3 against real audio rather
  than against the header alone.
- ✅ The async prompt-encode poll loop in §6.2 behaves as documented: the encoder and
  quantizer statuses settle before generation begins.

**Still unverified:**

- ❌ **Real-time throughput.** `hello_mrt2` generates offline and reports no timing, so
  "faster than playback" is *not* yet demonstrated. This is the single most important
  open number for GhostBand and needs `EngineMetrics::total_ms` against the 40 ms frame
  budget — i.e. it needs the GhostBand app.
- ❌ `RealtimeRunner` (as opposed to `MLXEngine`) has never been exercised. `hello_mrt2`
  uses `MLXEngine::generate_frame` directly; GhostBand uses the runner's inference thread
  and ring buffers, which is a different code path.
- ❌ MIDI steering, prompt blending, and underrun behaviour under load.
- ❌ Long-run stability and memory growth.

**Environment note:** this repository is developed in a Linux x86-64 container where MRT2
cannot compile at all. Anything above marked ✅ was confirmed on the target Mac and
reported back; nothing in this file is inferred from a successful compile that did not
happen. See `KNOWN_ISSUES.md` §1.
