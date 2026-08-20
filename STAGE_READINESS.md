# GhostBand — Stage Readiness Checklist

The question this file answers: **would I take this to a paying gig tonight?**

**Today: NO.** Phase 0 is not closed — see `KNOWN_ISSUES.md` §1.

Legend: ✅ verified by execution · ⚠️ implemented but unverified · ❌ not implemented ·
🚫 cannot be verified in the current environment

---

## V1 acceptance criteria (brief §28)

| # | Criterion | Status | Evidence / blocker |
|---|---|---|---|
| 1 | App launches reliably | 🟡 | launches and runs; "reliably" needs repetition + soak |
| 2 | MRT2 Small loads reliably | 🟡 | loads inside GhostBand (once); repetition untested |
| 3 | Continuous streaming works | 🟡 | **audible, 0 underruns, 0 dropped frames** over ~2 min on M2 Pro at 17.23/40 ms; no 60-min soak |
| 4 | MIDI chords steer generated harmony | 🟡 | **confirmed by ear** on M2 Pro via the on-screen keyboard: held chords steer the band, changes follow. Latency perceptible but musical. Hardware controller untested. |
| 5 | Section changes work | 🟡 | **confirmed by ear** on M2 Pro: Verse<->Chorus changes sound musical, arrangement moves with the section. Single song, short run. |
| 6 | MIDI footswitch control works | 🟡 | **MIDI Learn confirmed working on M2 Pro (2026-08-13)**: LEARN bound a key, the action fired, logged end to end. Mapping also **survived `pkill`**, confirming §17 and §18 against real files. Via the on-screen keyboard — **no hardware pedal has ever been connected** — `KNOWN_ISSUES.md` §15 |
| 7 | AI mute works | 🟡 | unit-tested + wired to AI BAND; audible path confirmed (peak -12.6 dBFS, RMS -20.1), mute itself not yet A/B'd |
| 8 | **PANIC always works** | 🟡 | **confirmed audibly on M2 Pro (2026-08-13)** — Escape silenced the band. Fade unit-tested, app reports 32.0 ms. Not yet tested through a PA at performance volume, with a foot, or repeatedly — which is what "always" requires |
| 9 | No persistent audio glitches | 🟡 | **55-minute soak, M2 Pro, 2026-08-13: 5 underruns in 312,239 blocks (0.0016%)**, rate *falling* over the run (3 in the first 16 min, 2 in the next 39) so no thermal clustering. Health Healthy throughout; limiter never engaged (0.0 dB GR). One run, on mains, speakers only |
| 10 | No serious memory leak over 60 min | 🟡 | **flat across a 55-minute soak (2026-08-13): 1.18 GB at 16, 36 and 55 minutes.** The +20 MB over the 1.16 GB baseline was early settling, not a trend. Readout resolution bounds growth at <10 MB/20 min rather than proving zero. Ran 55:31, not the full 60 |
| 11 | Audio device reconnect handled gracefully | ❌ | Phase 4 |
| 12 | Model errors do not crash the app | ⚠️ | `EngineState` error path **tested**; real MRT2 errors unobserved |
| 13 | Songs persist | 🟡 | **save confirmed on disk** (M2 Pro, 2026-08-13): `Demo-Song.ghostsong`, 655 bytes, Documents permission granted, atomic write left no temp file. **Open not yet exercised** — no quit/relaunch round trip |
| 14 | Setlists persist | ⚠️ | same as 13; missing-song handling **tested**, never exercised on disk |
| 15 | Performance Mode works without a mouse | 🟡 | **driven by arrow keys on hardware**; foot control is now written but unverified (see 6), which is what the criterion ultimately means |
| 16 | Generated band stays instrumental where practical | ❌ | prompt policy only; unverified |
| 17 | **No feature claims something that isn't implemented** | ✅ | enforced by `CLAUDE.md` rule 2; this file is the audit |

**Score: 1 / 17 fully verified, 6 partial, 4 written-but-unverified.** Criterion 17 remains the only one ticked
outright, and ticking it honestly is what makes the other sixteen trustworthy.

**Phase 0 is closed (2026-08-09).** The full chain is confirmed on an Apple M2 Pro: MRT2
generates → `RealtimeRunner` streams → GhostBand's safety stage processes → CoreAudio outputs
→ **the band is audible**. Frame time 17.17 ms against a 40 ms budget, ~2.3x real time.
The underrun-policy fix (`KNOWN_ISSUES.md` §10) is confirmed working on hardware, not just
in tests.

**A 55-minute soak passed (2026-08-13)** — memory flat at 1.18 GB across three readings,
5 underruns in 312,239 blocks at a *falling* rate, Health never left Healthy, limiter never
engaged. `KNOWN_ISSUES.md` §22 has the table and what it does not cover.

What that does *not* yet establish: the soak was one run, on mains, through laptop
speakers, on an untouched demo song — nothing was performed during it. PANIC has been heard
but not A/B'd through a PA at volume or pressed with a foot. No device has been unplugged
mid-stream. No hardware pedal has ever been connected. Those are what still separate "it
works" from "I would take it to a gig".

---

## What *is* verified today

These run and pass on the development machine (`ctest`, Linux x86-64, `-Wall -Wextra`):

- Engine state machine: legal transitions, illegal transitions rejected, error recovery.
- PANIC fade: reaches silence within the configured 20–50 ms, monotonic, no discontinuity
  at the start of the ramp, correct across arbitrary block sizes.
- Mute/unmute fades and AI output level in dB.
- Safety limiter: never exceeds the configured ceiling, passes low-level signal
  untouched, no NaN/Inf propagation.
- Safety monitor: trips to `Degraded` on sustained underruns, ignores isolated ones,
  requires explicit recovery.
- Diagnostics counters: lock-free, monotonic, snapshot consistency.
- `NullBackend`: honest silence, reports "no model loaded" rather than pretending.

That is the failure-handling core — deliberately the part built first, because it is the
part that decides whether a bad night is a glitch or a ruined set.

---

## Pre-gig checklist (for when the app actually runs)

Not yet usable, kept here so it is written before it is needed.

**Day before**
- [ ] `mrt models init` / `download` complete; `mrt2_small` present
- [ ] 60-minute soak passed on *this* laptop, on battery and on mains
- [ ] Diagnostics: 0 underruns, `total_ms` < 30 ms against the 40 ms frame budget
- [ ] Setlist loaded, every song opened once, every section triggered once
- [ ] Foot controller mapped; PANIC tested with the foot, not the trackpad

**At soundcheck**
- [ ] Interface at 48 kHz, buffer 128, AI on outputs 3+4
- [ ] FOH confirms AI channels are on separate faders and can be killed independently
- [ ] PANIC tested through the PA at performance volume
- [ ] Limiter status SAFE with the loudest section at full intensity
- [ ] Laptop: sleep disabled, notifications off, power connected

**Non-negotiable**
- [ ] Voice and guitar are routed **around** GhostBand, and the show works with the laptop
      closed. If this is not true, do not use GhostBand.

---

## Path to stage-ready

1. Close Phase 0 on Apple Silicon (`IMPLEMENTATION_PLAN.md`) — unblocks criteria 1–3.
2. Phase 1 — criterion 4, and wires 7/8 to real controls.
3. Phase 2 — criteria 5, 6, 13, 14, 15.
4. Phase 4 soak + recovery — criteria 9, 10, 11, 12.
5. Re-audit this file honestly. A criterion moves to ✅ only with evidence.
