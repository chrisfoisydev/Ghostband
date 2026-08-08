# Follow

**Your band follows you.**

A macOS / Apple Silicon standalone app that turns [Magenta RealTime 2](https://github.com/magenta/magenta-realtime)
into a live backing band for a solo singer-songwriter. The performer keeps control of
harmony, arrangement, section changes, dynamics, repeats and endings. The AI follows.

This is **not** playing along to backing tracks.

---

## Status: Phase 0 (technical spike), partially complete

| | |
|---|---|
| ✅ MRT2 API researched and documented against pinned upstream source | [`docs/MRT2_API_NOTES.md`](docs/MRT2_API_NOTES.md) |
| ✅ Architecture and plan | [`ARCHITECTURE.md`](ARCHITECTURE.md) · [`IMPLEMENTATION_PLAN.md`](IMPLEMENTATION_PLAN.md) |
| ✅ Portable safety core — **built and tested** (PANIC, limiter, underrun policy, state machine) | `src/core/`, `tests/` |
| ⚠️ MRT2 backend adapter — **written, never compiled** | `src/backend/Mrt2Backend.*` |
| ⚠️ JUCE host — **written, never compiled** | `src/app/` |
| 🚫 MRT2 streaming verified on hardware | **blocked: requires Apple Silicon** |

**Read [`KNOWN_ISSUES.md`](KNOWN_ISSUES.md) §1 before trusting anything here.** This
repository was developed on Linux x86-64, where MRT2's C++ engine refuses to build by
upstream design. The safety-critical logic was therefore built dependency-free so it
*could* be tested; the MRT2 and JUCE layers have not been through a compiler.

Nothing is on stage until [`STAGE_READINESS.md`](STAGE_READINESS.md) says so. It
currently reports **1 of 17** acceptance criteria verified.

---

## Build

### Portable core + tests — any platform

```bash
cmake -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j
ctest --test-dir build --output-on-failure
```

No dependencies beyond a C++20 compiler. This is what CI runs.

### Full application — macOS on Apple Silicon

```bash
# 1. Install MRT2 resources and weights (never bundled — see THIRD_PARTY_NOTICES.md)
uv venv --python 3.12 && source .venv/bin/activate
uv pip install "magenta-rt[mlx]"
mrt models init
mrt models download

# 2. Build
git clone https://github.com/magenta/magenta-realtime.git ../magenta-realtime
cmake -B build -DFOLLOW_BUILD_APP=ON -DMAGENTA_RT_DIR=../magenta-realtime
cmake --build build -j
```

Expect compile errors in `Mrt2Backend` on the first attempt — it is adapter code written
without a compiler available.

---

## Architecture in one paragraph

A JUCE audio callback pulls already-generated 48 kHz stereo audio out of MRT2's lock-free
ring buffer, passes it through Follow's own fade/limiter safety stage, and writes it to a
chosen output pair. A separate control plane translates MIDI, footswitches and section
changes into atomic MRT2 parameter writes. Nothing in the audio path allocates, locks or
waits. `src/core/` depends on **nothing but the C++20 standard library** — that is what
makes PANIC testable off-Mac and what makes it work when MRT2 is hung.

See [`ARCHITECTURE.md`](ARCHITECTURE.md).

---

## Design commitments

- **The musician leads.** The AI is additive and never the star.
- **Voice and guitar never route through Follow.** If the app dies, the show continues.
- **One stomp beats ten clicks.** Performance Mode must work without the trackpad.
- **Fail silently.** PANIC fades the AI out in ~30 ms and always works.
- **Nothing is faked.** A control that does not work is absent, disabled, or labelled.

---

## Licences

Follow is Apache-2.0. MRT2 code is Apache-2.0; MRT2 **model weights are CC-BY-4.0 and are
not distributed with this repository**. JUCE is dual GPLv3 / commercial and carries an
unresolved commercial-distribution question.

See [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md) — items marked 🚩 need human legal
review before any release.
