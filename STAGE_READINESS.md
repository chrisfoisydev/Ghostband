# Follow — Stage Readiness Checklist

The question this file answers: **would I take this to a paying gig tonight?**

**Today: NO.** Phase 0 is not closed — see `KNOWN_ISSUES.md` §1.

Legend: ✅ verified by execution · ⚠️ implemented but unverified · ❌ not implemented ·
🚫 cannot be verified in the current environment

---

## V1 acceptance criteria (brief §28)

| # | Criterion | Status | Evidence / blocker |
|---|---|---|---|
| 1 | App launches reliably | 🚫 | JUCE host never compiled (Linux dev box) |
| 2 | MRT2 Small loads reliably | 🟡 | loads and generates music via upstream `hello_mrt2`; **not yet inside Follow** |
| 3 | Continuous streaming works | 🚫 | `RealtimeRunner` never exercised; throughput unmeasured |
| 4 | MIDI chords steer generated harmony | ❌ | Phase 1; MRT2 API verified, not wired |
| 5 | Section changes work | ❌ | Phase 2 |
| 6 | MIDI footswitch control works | ❌ | Phase 2 |
| 7 | AI mute works | ⚠️ | `AiOutputStage` mute fade **tested**; not wired to a UI |
| 8 | **PANIC always works** | ⚠️ | fade logic **tested** (20–50 ms, click-free); not wired to UI/MIDI |
| 9 | No persistent audio glitches | 🚫 | unmeasured |
| 10 | No serious memory leak over 60 min | 🚫 | soak test not run |
| 11 | Audio device reconnect handled gracefully | ❌ | Phase 4 |
| 12 | Model errors do not crash the app | ⚠️ | `EngineState` error path **tested**; real MRT2 errors unobserved |
| 13 | Songs persist | ❌ | Phase 2 |
| 14 | Setlists persist | ❌ | Phase 2 |
| 15 | Performance Mode works without a mouse | ❌ | Phase 2 |
| 16 | Generated band stays instrumental where practical | ❌ | prompt policy only; unverified |
| 17 | **No feature claims something that isn't implemented** | ✅ | enforced by `CLAUDE.md` rule 2; this file is the audit |

**Score: 1 / 17 verified, 1 partial.** Criterion 17 is still the only one that can be
honestly ticked, and ticking it is what makes the other sixteen trustworthy.

MRT2 is now proven to generate music on the target Mac (criterion 2, partial). That is a
real milestone — but it was proven via *upstream's* example, not through Follow. Nothing
in `src/backend/` or `src/app/` has yet been compiled, so no criterion moves to ✅ on the
strength of it.

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
- [ ] Voice and guitar are routed **around** Follow, and the show works with the laptop
      closed. If this is not true, do not use Follow.

---

## Path to stage-ready

1. Close Phase 0 on Apple Silicon (`IMPLEMENTATION_PLAN.md`) — unblocks criteria 1–3.
2. Phase 1 — criterion 4, and wires 7/8 to real controls.
3. Phase 2 — criteria 5, 6, 13, 14, 15.
4. Phase 4 soak + recovery — criteria 9, 10, 11, 12.
5. Re-audit this file honestly. A criterion moves to ✅ only with evidence.
