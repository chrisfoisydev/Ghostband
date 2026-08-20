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

**Resolved by mode, not by a fixed split.** An earlier version of this entry described
reserving 2 slots for intensity and leaving 4 for sections; that was superseded by task 2.4
and the code no longer does it. What `PerformanceEngine` actually does
(`PromptSlotAllocator{0}`):

- **Free play, no song loaded:** intensity spends 3 slots on sparse/base/full wordings,
  which is its strongest lever.
- **Song loaded:** sections take **all 6** slots. Intensity keeps only its parameter terms
  (`drumless`, `cfg_drums`, `cfg_musiccoca`, `temperature`).

Intensity is genuinely weaker in song mode, and that is the acknowledged price of instant
section changes — re-encoding its variants on every section change would reintroduce the
exact stall this design exists to remove. It is a reasonable trade because a section's
prompt text already expresses its density: a performer writes "sparse atmospheric piano"
for a verse, so the contrast that matters most is carried by the sections themselves.

`Song::fitsPromptSlots()` therefore checks against 6, matching what the engine grants.

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

**MIDI Learn confirmed working, 2026-08-13.** On an M2 Pro: arming LEARN, clicking a key
on the FOOT CONTROL screen's keyboard, and seeing the binding taken and the action fire —
the full chain of match, consume, queue, drain, act. That is the first time any of the foot
control path has been exercised outside unit tests.

Still open:

- No hardware MIDI foot controller has ever been connected to this project (see also §1).
- Whether a learnt mapping survives a restart — the fix for §17/§18 — is not yet re-checked
  on this build.
- The `GhostBandAudioEngine` wiring, `FootControlPanel`, and the mapping file now
  **compile on the target Mac and the app launches** (2026-08-13). Nothing beyond that is
  established: no mapping has been learnt, no action fired, no file written.
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

**Design flaw found on hardware, 2026-08-13 — fixed.** The FOOT CONTROL screen is a
full-window overlay, so it hid the on-screen keyboard: the only MIDI source on a machine
with no pedal was behind the screen asking you to press it. MIDI Learn was unreachable
without hardware, which defeats the point of routing the keyboard through the control path
in the first place. The panel now carries its own keyboard, bound to the same
`MidiKeyboardState`, so it is still the identical signal path.

The pattern is the same one behind §18: a feature was made testable-without-hardware, then
a later screen quietly took that away, because the two were built at different times and
nobody re-checked the combination.

**To close it:** build on the Mac, open FOOT CONTROL, learn each action against the
keyboard on that screen, confirm each row lights on press. Then repeat with a real pedal on CC
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

**Saving works on the target Mac** (2026-08-13): SAVE SONG produced
`~/Documents/GhostBand/Songs/Demo-Song.ghostsong`, 655 bytes, with the Documents
permission granted and no `.writing` temp file left behind — so directory creation, the
file-stem rule, serialisation and the atomic write are all confirmed on real hardware.

Still untested, because it is JUCE code this machine cannot build and no one has yet
exercised it:

- `loadSongFile` / `saveSetlist` / `loadSetlistFile` — the read path and both setlist
  paths. `saveSongAs` is now confirmed; the others are not.
- Overwriting an **existing** song. The first save had nothing to clobber, so the reason
  the atomic write exists — `File::replaceWithText` truncates first, destroying the
  previous version if the write fails — is still the untested half.
- ~~Directory creation and the Documents permission prompt~~ — **confirmed working**.
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

---

## 17. ✅ FIXED — every foot-control mapping was lost on restart

**Found on hardware, 2026-08-13, by chasing a 26-byte discrepancy.**

A saved song was 655 bytes on the Mac. The same `serialiseSong` output on Linux was 629.
The content was byte-identical when diffed — and the file has exactly 26 lines.

**Cause:** `juce::File::replaceWithText` takes a `lineEndings` parameter that **defaults to
`"\r\n"`**. Every file GhostBand wrote was silently converted to CRLF.

For songs this was harmless: `Persistence.cpp` strips a trailing CR, and there is a test
for it. For foot-control mappings it was not. `MidiMappingSet::deserialise` had no such
guard, so a saved line came back as `next_section=cc:0:81\r`, `parseWholeNumber("81\r")`
correctly rejected a non-digit, and the binding was dropped.

**Impact, stated plainly:** every mapping the performer made was silently discarded the
next time the app launched. `mappedCount()` went from 5 to 0 with nothing on screen to
explain it. On stage that is a pedal that worked at soundcheck and is dead by the show —
and the diagnostics line would have read `nothing mapped`, which is accurate and useless.

**Fixed at both ends:**

- Both `replaceWithText` calls now pass `"\n"` explicitly. The bytes on disk match what
  the serialiser produced, so a song can be diffed in git without line-ending noise.
- `MidiMappingSet::deserialise` strips a trailing CR, matching what `Persistence.cpp`
  already did. Files that already carry CRLF — including every mappings file written
  before this fix, and any edited on another machine — still load.

Covered by a regression test that builds a CRLF document from `serialise()` and asserts
every binding survives.

**Confirmed on hardware, 2026-08-13.** A mappings file written by a pre-fix build — and
therefore genuinely CRLF on disk — loaded with its bindings intact. Better evidence than
the synthetic test, because the file was malformed by the real bug rather than by a test
helper.

**Lesson worth keeping.** Two parsers, one hardened against CRLF and one not, because only
one had a test for it. The defensive choice in `Persistence.cpp` was made on general
principle and turned out to be load-bearing; the identical reasoning was simply not applied
to `MidiMapping.cpp`. When a defence is worth writing once, check whether the same input
reaches anywhere else.

Also worth keeping: this was found by *not* accepting a byte count that nearly matched.

---

## 18. ✅ FIXED — MIDI Learn never saved the mapping it made

**Found on hardware, 2026-08-13, immediately after §17 — same symptom, different cause.**

`~/Library/Application Support/GhostBand/midi-mappings.txt` did not exist after a session
in which mappings had been touched. `saveMidiMappings()` was only reachable from
`setMidiMappings()`, which the FOOT CONTROL screen calls for CLEAR, CLEAR ALL and RESTORE
DEFAULTS. A learn does not go through it: the binding is made inside
`MidiMappingSet::handleMessage` on the MIDI thread, and nothing persisted it.

So the three buttons nobody uses mid-setup saved correctly, and **the one action that
actually maps a pedal did not**.

It could not simply call save inline — that is filesystem I/O on a MIDI thread, which the
real-time rules forbid outright. Fixed with an atomic `mappings_dirty_` flag set where the
learn resolves, and written from the existing 50 Hz timer on the message thread. The
transition is detected by comparing `isLearning()` either side of `handleMessage`, because
the call deliberately returns `None` during a learn — so nothing is queued and the message
thread has no other way to find out.

**Why this hid behind §17.** Both bugs produce exactly the same user-visible failure: map
a pedal, restart, mappings gone. Fixing the CRLF parse would have looked like a complete
fix right up until someone learnt a mapping and restarted — at which point the file would
still have been absent, and the obvious conclusion would have been that the first fix had
not worked. Worth remembering that one symptom had two independent causes stacked behind
it, and the second was only exposed because the first was fixed.

**PANIC confirmed audibly, 2026-08-13.** Pressing Escape with the band playing silenced
it on an M2 Pro. The fade had been unit-tested since Phase 0 and never heard until now —
it was the last completely unverified link in the failure philosophy, and the one control
the whole design rests on.

**Confirmed on hardware, 2026-08-13.** A mapping learnt on the FOOT CONTROL screen
survived `pkill -x GhostBand` — a hard kill that skips `shutdown()` entirely. That is the
stronger form of the test: it proves the save happens within 20 ms of the learn rather than
at exit, which is what matters if GhostBand ever dies mid-set.

---

## 19. 🟡 The Song Editor works; some paths still unexercised

**Confirmed working on hardware, 2026-08-13.** Add a section, name it, move it, undo the
move, save, and turn a section's band off — all behave. That clears the two risks flagged
below, which were the ones most likely to bite:

`core::SongEditor` is covered by 26 tests. `SongEditorView` is JUCE code this machine
cannot build, and carried the usual app-layer risk plus two things specific to it — both
now observed working rather than merely reasoned:

- **The `updating_` re-entrancy guard.** `refresh()` writes into every control, and
  `TextEditor::setText` fires `onTextChange`. Without the guard each refresh would look
  like a user edit and push a spurious undo entry, so undo would appear to do nothing. The
  guard is reasoned, not observed.
- **`juce::ListBox` lifetime.** The view is its own `ListBoxModel` and calls
  `setModel(nullptr)` in its destructor. If that is wrong, it is wrong as a crash on close.

**Deliberately absent, so they are not read as oversights:**

- No chord-progression editing — that belongs with Song Map (2.9).
- No drag-to-reorder; MOVE UP / MOVE DOWN only. Fewer moving parts.
- No "save as" or rename-on-save: the file name is derived from the title, so retitling a
  song and saving writes a **new file** and leaves the old one. That is safe but
  surprising, and it is the first thing to fix once the screen has been used.
- No setlist editor still (`KNOWN_ISSUES.md` §16).

**Still unexercised:** the prompt-slot overflow warning. A song with seven distinct
section prompts should raise an amber line naming the fix, while editing rather than at
save time. Worth doing once, since it is the editor's only genuinely predictive feature —
everything else reports what is, that one predicts what will stall on stage.

---

## 20. ✅ FIXED — app data was written to `~/Library/GhostBand`, not Application Support

**Found on hardware, 2026-08-13, while looking for a log file that did not appear to exist.**

JUCE's `File::userApplicationDataDirectory` is **`~/Library` on macOS**, not
`~/Library/Application Support`. GhostBand appended `GhostBand/` to it directly, so both
the foot-controller mappings and every log file since 2026-08-09 were written to
`~/Library/GhostBand/` — a location no macOS app uses, and outside the paths users,
backups and Migration Assistant look in.

**Cost, beyond the wrong location:** it burned a debugging session. The app had been
logging correctly the entire time; the logs were simply being searched for somewhere else.
Launching via `open` detaches stderr, so the terminal showed nothing either, and the only
information available about a live app was "it just keeps generating".

**It also contaminated an earlier diagnosis.** When
`~/Library/Application Support/GhostBand/midi-mappings.txt` came back missing, that was
read as proof the file was never written, and §18 was written on that basis. The §18 bug
was real and confirmed by reading the code — `saveMidiMappings()` genuinely was unreachable
from a learn — but the missing-file evidence cited for it was not sound, because the
correct path was never checked.

**Fixed:**

- `GhostBandAudioEngine::supportDirectory()` appends `Application Support` on macOS and is
  now the single source for both mappings and logs.
- `migrateSupportDirectory()` copies anything an older build left in `~/Library/GhostBand`,
  recursively and file-by-file, never overwriting and never deleting. It runs before the
  log sink is installed so the merge sees a clean destination.
- Log files are named `ghostband-*.log`. They had been `follow-*.log` since the rename,
  which made them hard to find by name as well as by path.

**Confirmed on hardware, 2026-08-13.** A fresh launch created
`~/Library/Application Support/GhostBand/`, migrated all 13 legacy `follow-*.log` files
and `midi-mappings.txt` intact, and wrote a new `ghostband-*.log`. No crash reports. The
recovered mappings file also settles the §18 question: mappings *had* been saved — they
were landing where nothing looked for them.

**Lesson worth keeping:** a platform API whose name reads like the obvious answer
(`userApplicationDataDirectory`) is exactly the kind of thing to check rather than assume,
and "the file is missing" is evidence about *a path*, not about *a write*.

**Second lesson, from the same session.** `moreThanOneInstanceAllowed()` returns false, so
`open` on the .app silently focuses a running instance instead of launching the new build.
It looks exactly like a build that did nothing. This caught us **four separate times**,
including once where the binary had been compiled seconds earlier and the app on screen was
still the old one.

Use **`./scripts/run.sh`**, which builds, runs the core tests, quits any running instance,
waits for it to release the audio device, and launches. It exists because the manual
sequence is three commands and forgetting the middle one is invisible.

**A related PATH trap on this machine:** `cmake` was symlinked out of the venv onto
`/usr/local/bin` and `ctest` was not, so `cmake --build` works and `ctest` reports
"command not found". The script now resolves the `cmake` symlink and looks for `ctest`
beside it. A *missing* ctest warns and continues — the point of the script is to get the
new binary on screen, and being unable to run the tests is a different thing from the tests
failing. A test *failure* still stops it.

---

## 21. ✅ FIXED — the AI BAND toggle was invisible

**Found on hardware, 2026-08-13: "It doesn't have an AI Band checkbox anymore."**

The setup screen's first button row was laid out by hand in pixels. Adding EDIT SONG and
the file-status line pushed the row past the window width:

```
available 932px, wanted 1050
  PANIC     180 (right)   RECOVER 130 (right)
  LOAD MODEL 130  LOAD DEMO SONG 150  PERFORMANCE MODE 170
  START 90   STOP 90 -> squeezed to 82   AI BAND 110 -> 0
```

`Rectangle::removeFromLeft` on an exhausted rectangle returns a **zero-width rectangle**
rather than failing. So the toggle was still constructed, still enabled, still "visible",
and simply never drawn. The performer lost the band on/off switch with nothing on screen to
suggest anything was wrong — a silent removal of a control, which is the same category of
failure as `CLAUDE.md` rule 2's "a button that looks live and does nothing", inverted.

**Fixed structurally, not by re-tuning.** A `layoutRow` helper flows controls left to right
and **wraps to a new row when the next will not fit**, so a control that runs out of space
moves down instead of disappearing. Right-edge items (PANIC, RECOVER) are placed first so
PANIC keeps its isolated position however many buttons appear beside it.

Rows were also rebalanced by purpose: row 1 is what gets reached for while the band plays
(PERFORMANCE MODE / START / STOP / AI BAND, with RECOVER and PANIC at the right edge), row
2 is setup (LOAD MODEL, LOAD DEMO SONG, EDIT SONG, OPEN SONG, OPEN SETLIST, SAVE SONG,
FOOT CONTROL). Moving the two LOAD buttons down is what freed the space.

**Confirmed fixed on hardware, 2026-08-13.** The AI BAND toggle is back on the top row.

**Process note.** This exact failure was predicted when the second button row was added —
"the setup screen may now overflow" — and then dropped after the screen was reported as
looking fine. The prediction was right and the follow-up was wrong: the overflow was
horizontal, not the vertical squeeze that had been looked for. A predicted failure that has
not been *specifically* checked is not a cleared one.

---

## 22. ✅ Soak test passed — 55 minutes, memory flat, 5 underruns

**Run on an Apple M2 Pro, 2026-08-13, mains power, MacBook Pro speakers, demo song,
untouched throughout.** Duration derived from `Blocks processed` (312,239 x 512 / 48000 =
3,331 s), which is the app's own clock and does not depend on anyone timing it.

| | 16:15 | 36:22 | 55:31 |
|---|---|---|---|
| Memory | 1.18 GB | 1.18 GB | 1.18 GB |
| Underruns / dropped frames | 3 / 3 | 4 / 4 | 5 / 5 |
| Output peak | -7.4 dBFS | -19.0 dBFS | -5.5 dBFS |
| Output RMS | -14.8 dBFS | -28.5 dBFS | -15.1 dBFS |
| Gain reduction | 0.0 dB | 0.0 dB | 0.0 dB |
| Health | Healthy | Healthy | Healthy |

**Memory: flat.** 1.18 GB at all three readings, over 39 minutes of observation. The +20 MB
against the 1.16 GB baseline appeared before the first reading and never grew — early
allocator settling, not a leak. Stated precisely: the two-decimal readout bounds growth at
**under ~10 MB per 20 minutes**, which is a bound rather than a proof of zero. A 2 MB/hour
drip would be invisible here and would also be harmless.

**Underruns: 5 in 312,239 blocks (0.0016%).** The rate *fell*: three in the first 16
minutes, two across the next 39. Falling matters more than the total — a rising or
clustering rate late in a run is what thermal throttling looks like, and is the specific
failure a long soak exists to catch. It did not happen.

**A false alarm worth recording.** At 36 minutes the output was 11.6 dB quieter than at 16
(-19.0 peak vs -7.4), which raised a real concern: a band that fades over a long set would
have the performer pushing BAND VOLUME up until the limiter starts working. By 55 minutes
it was back to -5.5. **A decaying output cannot recover**, so it was musical variation and
peak/RMS are near-instantaneous readings of whatever happens to be playing. The lesson is
about the metric, not the engine: two samples of an instantaneous value never establish a
trend, and it was right not to conclude anything from them at the time.

**What this does not establish:** one run, on mains, through laptop speakers, with an
untouched demo song. Not tested on battery, not through an interface, and nothing was
performed during it — no section changes, no harmony, no pedal. The pre-gig checklist in
`STAGE_READINESS.md` asks for battery as well, and a set is not 55 minutes of one song.

---

## §23 — The design was a *shell*, and two passes restyled the contents instead

**Status:** shell implemented, written but NOT COMPILED. Impact: cosmetic, but it consumed
three rounds of work and one wrong diagnosis, which is worth recording as a method failure
rather than a styling one.

The `GhostBand · Standalone` canvas was implemented three times before it looked like
itself.

| Pass | What I changed | Why it still looked identical |
|---|---|---|
| 1 | Palette, vocabulary, copy | The old palette was already dark. Nothing moved. |
| 2 | Typography (tracking, scale), a LookAndFeel | Right type, still in JUCE-shaped boxes. |
| 3 | Layout: a nav shell, a hairline row list, chamfered outlined controls | — |

The mistake in passes 1 and 2 was treating "the design" as a set of *attributes* — colour,
face, tracking — applied to whatever structure already existed. What actually distinguishes
the canvas is its **structure and its shapes**:

- a persistent 68px application bar, with the screens hung inside it;
- screens as one scrolling 880px column, not a window packed to its edges;
- information as **hairline-separated rows on bare ground**, not as filled panels;
- **outlined** controls, with exactly one filled primary per screen;
- **chamfered** corners — two opposite corners cut — and not one rounded corner anywhere.

Pass 2 had used a 6px corner *radius* throughout. That is not a near-miss of a chamfer; it
is the wrong shape family, and it is the single detail that most made the result read as
"a dark JUCE app" rather than as the design.

**The check that would have caught it in one round:** before styling anything, extract the
design's DOM and read its *layout* properties — `display`, `grid-template-columns`,
`border`, `clip-path` — not its colours. Ten minutes of that produced pass 3. It was
available before pass 1 and I did not do it.

**What is still not implemented:** HOME and SETLISTS. Both are omitted from the nav rather
than stubbed, because both need data the app does not have — see `StageHeader.h` and
`docs/DESIGN_GAP_ANALYSIS.md` §1. The design's SETUP screen also has no audio-device
picker at all, so GhostBand keeps JUCE's `AudioDeviceSelectorComponent` under an AUDIO
DEVICE heading: a screen that matched the canvas exactly there would be a screen on which
you cannot choose an output.

---

## §24 — The shell landed; three findings from the first run that reached a screen

**Status:** shell verified running on the M2 Pro (build 2026-08-20 08:48). The three fixes
below are written, NOT COMPILED.

Two rounds of "still looks exactly the same" after §23 were **not** a design problem. The
binary was never rebuilt:

1. The build command I supplied ended in a `#` comment. This shell is **zsh**, where
   `interactive_comments` is off by default, so `#` is not a comment — `tail` received
   `#`, `keep`, `this`, `output` as filenames, failed to open them, and exited. Closing the
   pipe sent `cmake` SIGPIPE partway through the build, which died silently. `run.sh
   --no-build` then launched the previous binary, which was 11 hours old.
2. Nothing on screen said which binary was running, so the failure was indistinguishable
   from a design that had not changed.

Fixed by `scripts/check-build.sh` and by printing the executable's own link time on the
setup screen. **Never put a `#` comment on a command line intended to be pasted into zsh.**

Once it did run, three things were visibly wrong and none had been predictable from the
source:

| Finding | Cause | Fix |
|---|---|---|
| Headings and the wordmark were not condensed | Anton is not installed on macOS and the named fallback silently resolved to the system sans | Anton embedded via `juce_add_binary_data` (OFL-1.1, §5.1 of the notices) |
| Every button was chamfered, turning quiet controls into a row of arrows | I applied the chamfer globally; the canvas puts `clip-path` only on the primary action and on cards — secondary buttons carry a border and no clip | Chamfer is now emphasis only |
| Disabled PERFORMANCE MODE read as a heavy grey slab | A translucent light fill on black is a solid mid-grey — heavier than the live outlined buttons beside it | Disabled primary draws as an outline; `drawButtonText` picks the matching text colour |

The pattern from §23 repeated in miniature: each of these is a property of the *rendered
result*, not of the source, and the fallback comment in `StageType.h` even said the
fallback had never been seen rendered. It had been sitting there, correctly flagged and
unchecked, for two commits.

---

## §25 — The Song Editor's NAME field discarded renames silently

**Status:** fixed, written, NOT COMPILED. Found by the save→quit→open round trip that
`STAGE_READINESS.md` criteria 13 and 14 had been flagging as never exercised.

Rename a section, press SAVE SONG, and the name reverted. The file on disk never had it.

**Cause — two halves, both required.** NAME commits on Enter or focus loss rather than per
keystroke, which is correct: `SongEditor` enforces unique section names, so a per-keystroke
commit renames a section to "Vers 2" while the performer is still typing "Verse". The cost
is that typed text is not in the model until one of those events fires, and clicking a
button does not reliably move keyboard focus.

1. `saveToDisk()` read `editor_.song()` without flushing the field, so it wrote the *old*
   name.
2. `refreshSectionFields()` then overwrote the field with the model's old name — erasing
   the evidence that anything had been typed.

Either alone would have been survivable. Together they made a silent, self-concealing data
loss: the edit vanished, and the UI looked as though it had never happened.

**Fix.** `commitPendingEdits()` flushes the field, and is called before every action that
*reads* the model — `saveToDisk`, `applyToBand`, and `requestClose` (before `isDirty()` is
consulted, so the DISCARD CHANGES guard cannot wave a typed rename through as "nothing to
lose"). `refreshSectionFields()` no longer writes into a control that currently has
keyboard focus.

**This is the third instance of the same bug.** The main prompt field swallowed every edit
on the first real run; MIDI Learn never reached disk (§18); and now this. The shape each
time: **a deferred commit with no flush before the read.** Any control that does not commit
per keystroke needs a flush at every point the model is read, and that rule belongs in
review, not in three separate comments after the fact.

**What core tests could not have caught.** `serialiseSong`/`deserialiseSong` round-trip a
renamed section correctly — verified directly while diagnosing this. The defect was
entirely in UI wiring, in the app layer, on the Mac. Consistent with every other bug found
in this project so far.


---

## §26 — Audio device recovery exists but has never lost a device

**Status:** written 2026-08-20, core tested, **app path unexercised**. Impact: unknown until
someone unplugs an interface mid-song, which is exactly the test that has not been run.

Until now, an audio device disappearing put GhostBand into a state it could not leave:
`audioDeviceError` faded the band out and stopped there. Correct as far as it went, and a
dead app for the rest of the night.

**What now happens.** `core::DeviceRecovery` schedules reconnection attempts at 400 ms,
800 ms, 1.6 s, 3.2 s, 6.4 s, then every 8 s indefinitely — eight attempts inside the first
40 seconds, and one every 8 s for as long as the set lasts. It never gives up by default,
because a recovery loop that stops after five tries has decided the gig is over.

**The decision worth arguing about.** A successful reconnect lands in `Restored`, not
`Running`: the device comes back automatically, the **band does not**. Restoring output is
safe — without a device there is no output at all, so reopening one cannot surprise anyone.
Restarting the accompaniment is not: the performer has kept singing through the dropout, and
a band reappearing mid-phrase is the failure this project refuses to ship. It mirrors how
`Health::Degraded` already requires an explicit RECOVER BAND.

**Two traps found while wiring it, both worth keeping in mind elsewhere:**

1. **`audioDeviceError` may be called on the audio thread.** JUCE does not promise
   otherwise. The previous implementation logged from there, which allocates a
   `std::string` and takes the log sink's mutex — a real-time violation that had been
   sitting in an error path since Phase 0, where it would fire exactly when the machine was
   least able to afford it. It now sets an atomic flag and calls `panic()`, both lock-free,
   and the message thread does the rest. The cost is the verbatim CoreAudio message, which
   cannot cross that boundary without allocating.

2. **PANIC has two causes and one flag.** A device-loss fade and a deliberate PANIC both
   raise `AiOutputStage::isPanicked()`. Without `performer_panic_`, acknowledging a device
   recovery would silently release a PANIC someone had pressed on purpose — the one control
   in the app that must never be undone as a side effect of anything.

**What has NOT been tested:** everything above the core state machine. No interface has been
unplugged, no sleep/wake, no sample-rate change from another app, no hub power cycle. The
retry schedule is tested; whether `setAudioDeviceSetup` actually reopens a returning USB
interface on macOS is not. The `STAGE_READINESS.md` entry says ⚠️, not 🟡, for that reason.

**How to test it** (needs hardware): start the band through a USB interface, unplug it
mid-song, confirm the stage screen reads `NO AUDIO OUTPUT / YOUR GUITAR AND VOCAL ARE CLEAR
- RECONNECTING` and that your own signal is genuinely unaffected. Plug it back in. Confirm
it comes back **on that interface and not the laptop speakers**, that the banner changes to
`AUDIO IS BACK`, and that the band stays silent until RESUME is pressed. Then repeat with
PANIC pressed first, and confirm RESUME does not release it.
