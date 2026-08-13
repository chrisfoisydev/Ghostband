# GhostBand — Known Issues

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
- ✅ `Mrt2Backend` and the JUCE host **compile and run**. `GhostBand.app` opens a 48 kHz
  device at 512 samples, loads `mrt2_small`, and streams through `RealtimeRunner`.
- ✅ **Real-time throughput confirmed: 17.17 ms per 40 ms frame (42.9% of budget)** —
  roughly 2.3x faster than playback, with the generation buffer holding 3328/3840 samples.

**Still open:**

- ✅ **Audible output confirmed** — the band plays through GhostBand on an Apple M2 Pro.
- ❌ Memory growth, long-run stability, device reconnect — unmeasured.

**Structural consequence:** every change to `src/backend/` or `src/app/` is unverified
until someone builds on a Mac. CI can only ever cover `ghostband::core`. This is why the
safety-critical logic lives there — it is the part that can be regression-tested on every
commit.

---

## 2. ⚠️ `Mrt2Backend` API assumptions are read-only-verified

Every MRT2 call used is copied from pinned upstream headers at commit `694a545`
(`docs/MRT2_API_NOTES.md`). But signatures were never checked by a compiler, and upstream
is at version `0.0.1` with a `TODO(public-release)` still in the header — the API should
be assumed unstable. Pin the MRT2 checkout; do not track `main`.

---

## 3. 🟡 The AI Intensity mapping works, but its curve is untuned

MRT2 exposes no density/intensity parameter. The macro in `ARCHITECTURE.md` §6 maps
intensity onto prompt blend weights, `set_drumless`, `set_cfg_drums`,
`set_cfg_musiccoca`, and `temperature`. Those choices follow from the parameters'
documented semantics, but **no one has listened to the result**.

**Impact:** the headline performance macro may feel wrong — bunched at one end, or too
subtle to be worth a knob. **Mitigation:** every constant lives in `IntensityMacro::Tuning`,
so retuning is editing data rather than rewriting logic.

**Now implemented and unit-tested** (`IntensityMacro`, 2026-08-09). The tests guarantee the
properties that can be checked without ears: monotonic in every continuous parameter,
prompt weights bounded and always summing to 1, drumless hysteresis that cannot flap, all
values inside sane bounds, and — the two hard rules — it never touches `cfg_notes` or any
gain.

**Confirmed working on hardware (2026-08-09):** the macro is audible and usable across its
range on an Apple M2 Pro. Severity accordingly drops from "may be unusable" to "may want
refinement".

**Still unmeasured, and worth revisiting when section presets exist** — those bake
intensity values in, so curve shape starts to matter more than it does with one live knob:
1. Is the perceived change roughly even across the range, or does everything happen in the
   last 20%?
2. Is intensity 0 too sparse to be musically useful, or a good "almost nothing" setting?
3. Does the drumless threshold at 0.13/0.17 land somewhere musical?
4. Are the sparse/full prompt suffixes actually the strongest available lever, as assumed?

Not a blocker for Phase 2. The mapping is isolated in `IntensityMacro::Tuning`, so section
presets can be authored against the current curve and the curve retuned later without
touching them.

---

## 4. ⚠️ Six prompt slots is a real ceiling

`kMaxPrompts = 6`. A song with more than six *distinct* style prompts cannot hold them
all pre-encoded, so the pre-encoding strategy that makes section changes instant
(`ARCHITECTURE.md` §8) needs slot recycling — encoding the next section's prompt during
the current one.

**Now implemented** (`PromptSlotAllocator`, 2026-08-10) and unit-tested.

**The tension this exposed.** `IntensityMacro` also wants slots — it crossfades sparse /
base / full wordings of the current prompt, which is the strongest arrangement lever MRT2
offers. Sections and intensity compete for the same six slots and there is no arrangement
that gives both everything.

Resolved by **reserving** slots: 2 for intensity's density variants, leaving **4 for
section prompts**. Four distinct prompts covers Verse / Chorus / Bridge / Outro, which is
most songs. The reservation is a constructor argument, so the trade is inspectable and
adjustable rather than buried.

**Capacity is counted in distinct prompts, not sections.** Ten sections sharing two
wordings cost two slots, so a long song with a consistent arrangement is fully resident.

**Overflow is reported, not rejected.** `isFullyResident()` says whether a song can promise
stall-free changes; the editor can warn the performer before the gig rather than after.
When a non-resident section is reached, eviction never touches the playing section's slot
and otherwise drops the prompt whose *nearest* user is furthest away in the arrangement.

**Still unmeasured:** how long a MusicCoCa encode actually takes, and therefore how bad an
overflow stall sounds. Needs measuring on hardware before any song is authored past four
distinct prompts.

---

## 5. ⚠️ MRT2's recording buffer is RAM-resident, not an async disk writer

`RealtimeRunner::start_recording()` accumulates into `std::vector<float>` in memory. A
60-minute stereo set at 48 kHz float is ~1.4 GB, and the vector will reallocate while the
inference thread runs.

**Impact:** the brief (§21) requires that recording never jeopardise generation
stability; the native buffer does not meet that bar for full-set recording.
**Resolution:** GhostBand supplies its own async disk writer in Phase 5. Until then,
recording stays absent from the UI rather than shipping a version that can stall a gig.

---

## 6. ⚠️ Generation buffer size is an untuned guess

Default is 3840 samples (~80 ms, two model frames), chosen so one late inference frame
cannot underrun. The real value depends on measured `total_ms` jitter on target hardware
and is unmeasured. Exposed in diagnostics rather than hidden. Tune in Phase 4.

---

## 7. ℹ️ Upstream `set_audio_prompt(index, path)` returns a fake embedding

Upstream's header carries `TODO(public-release)`: when `path` is non-empty it "writes a
deterministic fake embedding rather than decoding the file." **GhostBand never calls it.**
Use `set_audio_prompt_samples()`. Recorded so nobody later "fixes" our omission.

---

## 8. ℹ️ MIDI pitch range is 132, not 128

`kTotalPitches = 132` — 128 pitches plus 4 drum triggers. `MODEL.md` says "128-dim
multihot", which understates it. GhostBand clamps incoming MIDI to 0–127 and reserves
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
  meant to be producing audio. Wired to start/stop in `GhostBandAudioEngine`.
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

**Status: fixed, unit-tested, and CONFIRMED ON HARDWARE (2026-08-09).** With the fixed
binary running on an Apple M2 Pro: `Health` stays `Healthy`, underruns sit at/near zero,
and the band is audible without operator intervention.

Worth recording how nearly this was mis-verified: an intermediate run *appeared* to
confirm the fix while still executing a stale binary — the source had been pulled but not
rebuilt. The tell was the UI warning text, which still read "Press PANIC to release"
instead of the new "Press RECOVER AI when stable". **When verifying a fix on hardware,
confirm the binary is actually the new one** — a visible string change is the cheapest
way to do that, and is worth deliberately including in any future fix that matters.

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

---

## 12. ℹ️ Harmony latency is perceptible and unmeasured

Confirmed by ear (2026-08-09): a chord change steers the band with a noticeable but
musically acceptable delay. Nobody has measured it.

**Where it comes from**, in descending order of size — all but the last are structural:

| Source | Approximate | Avoidable? |
|---|---|---|
| MRT2 frame quantisation | up to 40 ms | No — 25 Hz generation cadence |
| Model response to new conditioning | ~100–200 ms | No — the model card cites ~200 ms control latency |
| GhostBand generation buffer | ~80 ms | Partly — tunable, at the cost of underrun margin |
| Audio device buffer | ~11 ms @ 512 | Yes — a smaller device buffer |
| Limiter lookahead | 2 ms | No, and not worth removing |

The dominant terms belong to MRT2, not to us. Our ~80 ms buffer is the only meaningful
lever, and shrinking it trades directly against underrun margin — currently excellent
(0 underruns at 17 ms/frame), so there is room to experiment.

**Why this may not be a defect.** A human band does not respond instantly either; players
land on the change at the next beat. Whether ~250 ms reads as "sluggish" or "natural"
depends on tempo and on whether the performer anticipates. That is a musical judgement, so
it needs playing rather than analysis.

**Next step (task 1.8):** measure it properly — timestamp a note-on against the first
audible change — then decide whether to trade buffer for latency. Do not tune by feel
alone; the number matters for deciding whether foot-triggered section changes need
lookahead in Phase 2.

---

## 13. ✅ FIXED — prompt edits were silently discarded

**Found on hardware (2026-08-10). The band sounded identical regardless of what was typed
in the prompt field.**

**Diagnosis from the log**, which was decisive:

```
17:18:09 INFO [generation] prompt set variants=3
[MagentaRT] Combined Prompt (3) tokens: 447 82 681 586 547 320 606 58 838 28 238 639
17:18:12 INFO [generation] generation started
...  (generation start/stop only, for seven more minutes)
```

`prompt set` appears **once**, at model load. Every edit afterwards never reached the
engine, so the token vector could not change — MRT2 kept using its load-time prompt for
the whole session.

**Cause:** the only trigger was `TextEditor::onReturnKey`. Typing a prompt and clicking
away did nothing, with no indication. Once the on-screen keyboard existed, focus moved
between it and the text field constantly, making the failure the normal case rather than
the exception.

This is precisely the failure `CLAUDE.md` rule 2 exists to prevent: a control that looks
live and quietly does nothing. It was not a "missing feature" — it was a working feature
that could not be reached.

**Fix:**
- An explicit **APPLY PROMPT** button.
- Apply on focus loss as well as Enter, so the obvious gestures all work.
- A status line reading either `NOT APPLIED - press APPLY PROMPT` (amber) or
  `applied (ready)`, so the typed text and the encoded text can never silently disagree.

**Lesson worth keeping:** the app already logged everything needed to find this in
seconds. Structured logging earned its cost here.

---

## 14. ⚠️ A note-bound footswitch steals that note from harmony

**Impact:** one key on the MIDI keyboard stops contributing to the chord, silently, for
as long as the mapping exists.

GhostBand cannot tell a footswitch apart from a keyboard when both send note messages on
the same port — the MIDI bytes are identical. When a message matches a foot-control
binding it is consumed and does **not** reach `MidiHarmonyState`; the alternative, letting
it do both, would mean stomping a pedal also plays a note under the band.

Consequences of the choice:

- The **defaults avoid it entirely** by using CC 80-84 rather than notes. These are
  undefined in the MIDI spec, are what footswitch-to-MIDI boxes commonly send, and cannot
  be produced by playing keys.
- A performer whose controller only sends notes can still bind them via MIDI Learn, and
  the foot-control screen warns which key they have given up.
- Binding **CC 64** costs the sustain pedal for the same reason, and is warned about
  separately.

**Why not solve it properly:** the real fix is per-device routing — treat one MIDI input
as the pedal and another as the keyboard. JUCE hands `handleIncomingMidiMessage` the
source device, so the information is available; what is missing is the setup UI to
designate a device and the persistence for it. That belongs with task 2.7/2.8 rather than
being bolted on here.

**Workaround today:** keep the defaults, or bind pedals to CCs. Both are one click on the
FOOT CONTROL screen.

---

## 15. ⚠️ Foot control has never been driven by an actual pedal

**Impact:** the whole of task 2.6 is verified only by unit tests and by reading the code.

`MidiMappingSet` is covered by 33 tests that run and pass on Linux, including the press-
only rule, the 120 ms debounce, learn semantics, binding theft, and settings-file
corruption. What those tests cannot cover is everything below `core`:

- No hardware MIDI foot controller has ever been connected to this project (see also §1).
- The `GhostBandAudioEngine` wiring, `FootControlPanel`, and the mapping file have not
  been compiled — they are macOS/JUCE code and this machine cannot build them.
- The threading design (MIDI thread try-lock, PANIC latched immediately, everything else
  drained at 50 Hz) is reasoned, not observed. It is the part most worth a second look on
  hardware, because a race here shows up as a missed or doubled section change under load
  — exactly the failure mode a performer cannot recover from on stage.

**Most of it is testable without a pedal.** The on-screen keyboard sends real MIDI through
`GhostBandAudioEngine::handleMidiMessage`, the same entry point a hardware device uses, so
arming LEARN and pressing a key exercises the whole chain: match, consume, queue, drain,
act, persist. What that cannot reach is the CC decode branch — a musical keyboard sends
notes, not control changes — and the multi-device threading, since the on-screen keyboard
runs on the message thread rather than a device thread.

For one commit the on-screen keyboard bypassed this path entirely and reached
`MidiHarmonyState` directly, which made foot control untestable without hardware. Worth
recording: the header comment claiming it was "a genuine MIDI source, not a simulation"
had been true when written and quietly stopped being true when foot control landed beside
it. A shared path only stays shared if new features are added *to* it.

**To close it:** build on the Mac, open FOOT CONTROL, learn each action against the
on-screen keyboard, confirm each row lights on press. Then repeat with a real pedal on CC
and run a song end to end using only the pedal. Criterion 6 in `STAGE_READINESS.md` stays
⚠️ until the pedal half has happened.

---

## 16. ⚠️ Songs and setlists have never been written to or read from a real disk

**Impact:** the format and its parser are well covered; the file handling around them is
not covered at all.

What **is** tested, on Linux, by 67 tests in `PersistenceTests` and `SetlistTests`:
round-trip fidelity, schema versioning, refusal of files from a newer GhostBand,
line-numbered parse errors, corrupt and truncated input, CRLF, comments, unknown keys,
clamping, C-locale numbers, file-stem safety, and missing-song handling in a set.

What is **not** tested, because it is JUCE code this machine cannot compile:

- `saveSongAs` / `loadSongFile` / `saveSetlist` / `loadSetlistFile` — every path that
  touches the filesystem.
- The atomic write (temp file, then move into place). It exists because
  `File::replaceWithText` truncates first, so a failure part-way through would destroy the
  previous version of a song. That reasoning is untested.
- Directory creation under `~/Documents/GhostBand`, and macOS's permission prompt the
  first time an app writes to Documents. **This is the most likely first failure** and it
  has never been seen.
- The `FileChooser` flows, including cancellation.
- Song resolution for a setlist: the setlist's own folder first, then the shared songs
  directory.

**Deliberate non-goals for now**, so they are not mistaken for oversights:

- There is **no Song Editor**. Songs can be saved, opened and performed, but the only song
  that can be *created* is the demo. Editing is Phase 5.
- There is **no setlist editor** either. A setlist must currently be written by hand in a
  text editor — which the format is designed to make possible, but it is not a feature.
- Nothing auto-saves. A song edited in memory is lost unless SAVE SONG is pressed.

**To close it:** on the Mac, save the demo song, quit, relaunch, open it, and confirm the
sections and intensities survived. Then hand-write a setlist naming two songs plus one
that does not exist, and confirm the set loads with a visible gap and names the missing
song rather than silently shortening the running order.
