# GhostBand — Design vs. Implementation Gap Analysis

Source: `GhostBand · Standalone` design canvas, reviewed 2026-08-13.

The design describes a **complete shipping product**. What exists today is roughly the
engine and two of its screens. This file records, screen by screen, what is already real,
what is named differently, what needs building, and — the part that matters most — **what
the design implies that Magenta RealTime 2 cannot actually do.**

Nothing here is a criticism of the design. It is a good design. The point of the file is
that `CLAUDE.md` rule 2 forbids shipping chrome that looks live and is not, so every
control has to be traced to a real capability before it is drawn.

---

## 1. The blocking finding: per-instrument "BAND" parts

The design shows a **BAND** row of instrument chips (`{{ p.name }}`) in three places — the
song builder, the stage screen, and onboarding — implying the performer can choose which
instruments play.

**MRT2 cannot do this.** It generates a single mixed stereo stem. There are no separable
instrument tracks, no per-instrument gain, no solo/mute. The complete instrument-facing API
surface is:

```cpp
void set_drumless(bool on);   // hard drum removal — the ONLY per-instrument control
```

`docs/MRT2_API_NOTES.md` §5. Everything else about instrumentation is expressed through
prompt text.

**What is actually achievable:**

| Chip | Achievable? | How |
|---|---|---|
| DRUMS on/off | ✅ genuinely | `set_drumless(true)` — a hard removal, reliable |
| Any other instrument | ⚠️ **suggestion only** | prompt wording, e.g. "no piano, upright bass and brushed percussion" |

A prompt asking for "no piano" **usually** removes piano and **sometimes** does not. That
is a hint to a generative model, not a mixer channel. Drawing it as a toggle beside a
DRUMS toggle that genuinely works would teach the performer to trust a control that will
eventually fail them mid-song.

**Recommended resolutions**, in order of preference:

1. **Drop the parts chips**, and keep the design's excellent DESCRIBE THE BAND field as the
   instrumentation control. It is honest about being a description.
2. **Keep DRUMS only** as a real toggle, and treat the rest as prompt text.
3. **Keep the chips but label the section a suggestion** — e.g. "ASK THE BAND FOR" with
   chips that visibly compose a prompt rather than presenting as switches. Requires that
   the chips never look like on/off state.

### How this gets settled: `scripts/instrument-removal-test.py`

Arguing about it is not the way — `CLAUDE.md` says to build a minimal isolated experiment
and choose from observed behaviour. The script does that:

- Generates clips through `hello_mrt2` with and without "no <instrument>" in the prompt.
- **Six trials per instrument**, because MRT2 is stochastic and one clip proves nothing.
- **One control clip per instrument** with nothing removed. Without it there is no way to
  separate "the prompt worked" from "that generation happened not to use piano anyway",
  which is an easy and invalidating mistake.
- **Blind**: clips are shuffled and named by index, with the answer key written to a file
  the listener is asked not to open until scored. Knowing which clip is the "no piano" one
  makes you hear its absence, and that effect is not small.
- Tests DRUMS as well, which has the real API — so the result also calibrates how the
  prompt route compares against the guaranteed one.

**The threshold is committed before listening**, because a test without a pre-agreed bar
is not a decision procedure, just listening followed by rationalising:

| Removed | Verdict |
|---|---|
| ≥ 90% | Build as clearly-labelled "ASK THE BAND FOR" chips that visibly compose prompt text. **Never as switches** — even 9 in 10 fails on stage, where there is no recourse mid-song. |
| 60–90% | Chips only if DRUMS is visually distinguished as the guaranteed one, with softer wording still. |
| < 60% | **Drop the chips.** The DESCRIBE THE BAND field is better and is already honest about being a description. |

Run it, listen, score, and record the numbers here. This decision needs making before the
song builder is built, because it changes the shape of the screen.

---

## 2. Screen by screen

Legend: ✅ exists · 🟡 exists in different form · ❌ needs building · 🚫 blocked by MRT2

### HOME

| Design element | Status | Notes |
|---|---|---|
| TONIGHT card — set name, venue, time | ❌ | no venue/time concept anywhere in the model |
| Set count / set length | ❌ | songs have no duration; sections deliberately have none (`Song.h`) |
| PERFORM | ✅ | PERFORMANCE MODE |
| NEW SONG | 🟡 | the editor always opens on the loaded song; no blank-song entry point |
| NEW SETLIST | ❌ | no setlist editor exists |
| RECENT SONGS + metadata | ❌ | nothing tracks recency |

**Note on set length.** `SongSection` has no duration *by design* — the brief (§30.5)
requires that repeating a chorus or ending early stay possible, and a duration field works
against that. A displayed set length would have to be an explicit per-song estimate the
performer types, not something derived. Worth deciding deliberately rather than inheriting
from the design.

### SONG BUILDER

| Design element | Status | Notes |
|---|---|---|
| Section list | ✅ | `SongEditorView` |
| MOVE UP / DOWN / DUPLICATE / RENAME / DELETE | ✅ | all present and working on hardware |
| SAVE NAME | ✅ | rename applies on Enter/focus-loss |
| Section % | ✅ | AI Intensity |
| BAND STYLE (e.g. ORGANIC INDIE FOLK) | 🟡 | maps to the song prompt; design implies a **preset picker**, which does not exist |
| BAND parts chips | 🚫 | see §1 |
| BAND INTENSITY, SPARSE ↔ FULL | ✅ | exactly the existing macro, better named |
| FEEL chips | ❌ | undefined; presumably prompt presets |
| DESCRIBE THE BAND (+ preview, CLEAR) | ✅ | this is the section prompt field |
| "parts label" per section | 🚫 | see §1 |
| REHEARSE THIS SONG | ❌ | no rehearse mode |
| DONE | ✅ | |

### SETLISTS

| Design element | Status | Notes |
|---|---|---|
| Track list, numbered | 🟡 | `Setlist`/`SetlistController` exist and are tested; never shown in UI |
| Drag to reorder | ❌ | no setlist editor at all |
| ADD SONG | ❌ | |
| Track "stage" and "length" columns | ❌ | neither concept exists |
| PERFORM SET | 🟡 | a setlist can be opened; there is no perform-set entry point |
| STAGE NOTES | ❌ | `SongSection::notes` exists; setlist-level notes do not |

### PERFORM (stage screen)

The best-mapped screen in the design — nearly everything is real state.

| Design element | Status |
|---|---|
| Song title, position in set | ✅ |
| NOW / section name | ✅ |
| NEXT / next name | ✅ |
| BAND state + display | ✅ |
| Intensity SPARSE ↔ FULL | ✅ |
| LESS BAND / MORE BAND | ✅ `IntensityUp` / `IntensityDown` |
| PREVIOUS / NEXT / PANIC | ✅ |
| BAND VOLUME | ✅ AI Output Level |
| EXIT | ✅ |
| FOLLOWING / chord | 🟡 `ChordNamer` exists; currently on the setup screen only |
| STRONG SIGNAL | ❌ guitar-follow confidence — Phase 3, not built |
| Pedal legend "A / B — PREVIOUS · NEXT" | 🟡 actions exist; **switch letters do not** — bindings are MIDI note/CC, not lettered switches |
| Rehearse label | ❌ |

### PANIC / FAILURE STATES

The design's copy here is **better than what is implemented** and should be adopted
regardless of anything else:

- **"BAND STOPPED — YOUR GUITAR AND VOCAL ARE CLEAR"** — this states the one thing the
  performer needs to know in that moment, and it is exactly the brief's failure philosophy
  written down. Current text is "PANIC - AI SILENT", which says less.
- **"Audio and pedal unaffected / Your guitar and vocal are still passing through"** —
  same virtue.
- RESUME ✅ (PANIC is already a toggle)
- "Band engine stopped responding · restarted 2 times" — 🟡 `Degraded` health exists, but
  **nothing auto-restarts and nothing counts restarts**. That is real Phase 4 work.

### SETUP / RIG / FOLLOW / PEDAL / ADVANCED

| Design element | Status | Notes |
|---|---|---|
| YOUR RIG rows | 🟡 | JUCE device selector exists; not as a status list |
| SAMPLE RATE 48 kHz | ✅ | shown in diagnostics |
| BUFFER "128 samples · 3.2 ms" | 🟡 | device buffer is shown; not selectable in GhostBand's own UI |
| BAND HEADROOM -6 dB | 🟡 | AI Output Level, differently framed |
| PANIC RELEASE "80 ms fade" | 🟡 | **implemented as 30 ms** — the design asks for 80. See below |
| FOLLOW RESPONSE STABLE ↔ FAST | 🟡 | **maps well onto the existing generation-buffer control** (1–4 frames = 40–160 ms). A genuinely better name than "GEN BUFFER" |
| MIDI CLOCK Internal | ❌ | no clock handling at all |
| RUN FIRST-TIME SETUP | ❌ | no wizard |
| CONNECTED MIDI INSTRUMENTS + status | 🟡 | names exist; rich per-device status does not |
| Instrument detection, LISTENING…, RUN AGAIN | ❌🚫 | guitar Follow — Phase 3, experimental, and the hardest unbuilt thing here |
| PEDAL: "Morningstar MC6 Pro · CONNECTED" | 🟡 | device names are read; no pedal-specific identification |
| Pedal switches A–F with per-switch assignment | 🟡 | MIDI Learn works, but binds **messages, not lettered switches** — see below |
| ACCOUNT / email | ❌ | no account system |
| LICENSE / key / status / LOG OUT | ❌ | no licensing, no backend, no trial |

**PANIC RELEASE — decided 2026-08-13: 30 ms stands, the design's 80 ms is not adopted.**
The performer has now heard PANIC fire and judged it clean. 30 ms sits inside the brief's
20–50 ms range and reaches silence faster, which is the point of the control. The design's
80 ms is gentler but slower, and nothing observed suggests 30 ms clicks. Revisit only if a
click is ever heard through a PA at volume — not to match a mock.

The original note, kept for the reasoning: currently 30 ms, with a 20–50 ms range recorded in
`ARCHITECTURE.md` §5 as the brief's requirement. The design says 80 ms. These disagree and
someone should decide by ear — 80 ms is gentler and less likely to click, 30 ms gets to
silence faster. This is a one-constant change either way, but it is a **stage-safety
parameter** and should not be changed silently to match a mock.

**Pedal letters.** The design assumes a known pedal with lettered switches. GhostBand binds
whatever MIDI message arrives, which is more general and works with any controller. To show
"SWITCH A" the app would need either a device profile for known pedals, or to let the
performer label each binding. The latter is small and keeps the generality.

### ONBOARDING

Entirely unbuilt: sign-in, Apple sign-in, licence activation, 14-day trial, rig selection,
"HEAR GHOSTBAND" test-tone flow, follow-mode choice, pedal setup, completion.

The licensing and account portions are not merely UI — they need a backend, a payment
relationship, and the legal review already flagged in `THIRD_PARTY_NOTICES.md` §3 (JUCE's
GPL/commercial split, and the CC-BY-4.0 model weights). **Do not build the sign-in screen
before that decision is made**; a sign-in form that cannot authenticate is the worst
possible version of rule 2.

---

## 3. What the design gets right that the build should adopt now

Independent of any feature work:

1. **The failure copy.** "YOUR GUITAR AND VOCAL ARE CLEAR" is better than anything
   currently on screen.
2. **The vocabulary.** BAND INTENSITY / BAND VOLUME is clearer than AI INTENSITY / AI
   OUTPUT LEVEL, and keeps the two distinct as the brief requires. LESS BAND / MORE BAND
   is better than a percentage. FOLLOW RESPONSE beats GEN BUFFER.
3. **The palette** — warmer and darker than the current one (see §4).
4. **Anton for display type**, which is what the GHOSTBAND wordmark already specifies.

## 4. Palette

| Role | Design | Current | Note |
|---|---|---|---|
| Background | `#0a0a0a` | `#0e0f11` | design is darker, truly black |
| Panel | `#1e2126` / `#23262b` | `#17191c` | design is lighter than the ground, not darker |
| Text | `#edeae4` | `#e8e8e8` | design is warm off-white, not neutral grey |
| Dim | `#8a8f97` | `#8a8f96` | effectively identical |
| Muted warm | `#c9c5bd` | — | new: secondary warm text |
| OK | `#46b96b` | `#37c871` | design is less saturated |
| Warn | `#b98b2e` | `#e0a020` | design is much more muted |
| Fault / Panic | `#d93a2b` | `#e0453e` / `#b3231c` | design uses one red for both |

---

## 5. Recommended order

1. **Palette and typography.** Contained, no functionality implied. *(palette done in the
   first pass; typography and chrome missed entirely and done in a second — see below)*

### What the first design pass got wrong

The first pass changed the palette, the wording and the PANIC copy, and the result **looked
identical** on screen. Two reasons, both worth recording:

- **The typography is the design.** The canvas tracks nearly every label between 0.14em and
  0.28em against a scale dominated by 10–13px, with display type jumping to 22–40px. Wide
  tiny upper-case labels under big condensed display type *is* the visual identity. The
  build had no letter-spacing anywhere and a flatter, larger scale. Changing colours while
  leaving that alone changes almost nothing perceptible.
- **Stock JUCE chrome was most of what was on screen.** Every `TextButton`, `Slider`,
  `ComboBox` and `TextEditor` was drawing itself with JUCE's default appearance —
  grey gradient buttons, round slider thumbs, untracked body type. Restyling the handful of
  hand-drawn elements left the majority untouched. A `LookAndFeel` reaches all of it at
  once, including components GhostBand never touches directly such as the audio device
  selector.

The underlying mistake was scoping the pass to "changes that imply no functionality that
does not exist". That is a real constraint about not drawing fake controls, and it never
implied leaving the visual language alone — restyling implies no capability whatsoever. The
two were conflated, and the safe, valuable half went undone.
2. **Failure and PANIC copy.** Pure improvement to the most important screen.
3. **Rename to the design's vocabulary** — BAND INTENSITY, BAND VOLUME, FOLLOW RESPONSE.
4. ~~**Restructure Performance Mode**~~ *(done — NOW label, FOLLOWING/chord moved onto the
   stage screen, BAND state with a SPARSE↔FULL bar, LESS/MORE BAND buttons, a pedal legend
   built from real bindings, RESUME on a latched PANIC. `STRONG SIGNAL` deliberately
   omitted — it is guitar-follow confidence and does not exist.)*
5. **Decide the parts-chips question (§1)** before touching the song builder.
6. **Setlist editor** — the largest genuinely-buildable gap, and criterion 14 needs it.
7. **Phase 4**: engine auto-restart with a count, device-disconnect recovery.
8. **Guitar Follow** (Phase 3) — everything in the FOLLOW screen depends on it.
9. **Accounts / licensing / onboarding** — needs a product and legal decision first, not
   an engineering one.


---

## 9. SETLISTS — built 2026-08-20, and where it departs from the canvas

The screen exists (`SetlistView`, `core::SetlistEditor`, 22 tests). Four deliberate
departures, each because the canvas asks for something GhostBand does not know:

| Canvas | GhostBand | Why |
|---|---|---|
| "DRAG A SONG TO REORDER" | MOVE UP / MOVE DOWN buttons | Drag is implementable, but a set is reordered at soundcheck on a trackpad, and a mis-drag that silently drops a song two places away is worse than two clicks. The hint line says what is true. |
| `{{ t.len }}` per song, set length in the header | omitted | **GhostBand does not know how long a song is.** Sections have no duration — the performer decides when a section ends. "4:12" would be invented. |
| "THE FOLD · 9:30PM" | omitted | Venue and start time are HOME's data, which does not exist. |
| — | **MISSING** in fault red, per row and in the details panel | Not in the canvas. A set referencing a song no longer on disk is the most important thing this screen can say, and it has to be visible at soundcheck rather than between songs. |

**Still outstanding for HOME:** venue, set time, set length, and a recently-played list.
Set length depends on song duration, which is the same thing the setlist rows cannot show —
so HOME cannot be built honestly without either tracking real elapsed time per song or
letting the performer type an estimate. Neither is decided.
