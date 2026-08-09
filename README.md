# Follow

**Your band follows you.**

A macOS / Apple Silicon standalone app that turns [Magenta RealTime 2](https://github.com/magenta/magenta-realtime)
into a live backing band for a solo singer-songwriter. The performer keeps control of
harmony, arrangement, section changes, dynamics, repeats and endings. The AI follows.

This is **not** playing along to backing tracks.

---

## Status: Phase 0 (technical spike) — **complete**

| | |
|---|---|
| ✅ MRT2 API researched and documented against pinned upstream source | [`docs/MRT2_API_NOTES.md`](docs/MRT2_API_NOTES.md) |
| ✅ Architecture and plan | [`ARCHITECTURE.md`](ARCHITECTURE.md) · [`IMPLEMENTATION_PLAN.md`](IMPLEMENTATION_PLAN.md) |
| ✅ Portable safety core — **built and tested** (PANIC, limiter, underrun policy, state machine) | `src/core/`, `tests/` |
| ✅ MRT2 proven on target hardware — `mrt2_small` generates 48 kHz stereo music | via upstream `hello_mrt2` |
| ✅ MRT2 backend + JUCE host — **build and run**; Follow streams MRT2 audio | `src/backend/`, `src/app/` |
| ✅ **Real-time confirmed — 17.17 ms per 40 ms frame (42.9%)** on an **Apple M2 Pro** | measured in-app |

**Phase 0's goal is met: MRT2 generates real-time audio inside Follow.** Generation runs
at 17.17 ms against a 40 ms frame budget on an Apple M2 Pro, so `mrt2_small` produces audio
~2.3x faster than it plays back. (`mrt2_base` is not real-time on this class of chip.)

**The band is audible on an Apple M2 Pro** — the full chain from MRT2 through Follow's
safety stage to CoreAudio is confirmed working. Nothing has been soak-tested, and no
device-failure or long-run behaviour has been exercised yet.

This repository is developed in a Linux x86-64 container where MRT2 cannot build at all;
the app is compiled and run separately on an Apple Silicon Mac. Nothing is on stage until
[`STAGE_READINESS.md`](STAGE_READINESS.md) says so — it currently reports **1 of 17**
acceptance criteria fully verified.

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
# 1. Xcode's Metal compiler is a SEPARATE download. MLX builds its own shaders and
#    fails with "cannot execute tool 'metal'" without it. Not in upstream's README.
xcodebuild -downloadComponent MetalToolchain

# 2. MRT2 resources and weights (never bundled — see THIRD_PARTY_NOTICES.md).
#    `mrt models download` with no argument defaults to mrt2_base; name it explicitly.
uv venv --python 3.12 && source .venv/bin/activate
uv pip install "magenta-rt[mlx]" "cmake<3.28"
mrt models init
mrt models download mrt2_small

# 3. Upstream sanity check. The trim script drops the examples Follow never links
#    against (SuperCollider, Max, PD, the React UIs) — they cost GBs of disk to
#    configure. Reversible with --restore.
git clone https://github.com/magenta/magenta-realtime.git ~/magenta-realtime
python3 scripts/trim-mrt2.py ~/magenta-realtime
cd ~/magenta-realtime && cmake . -B build && cmake --build build --target hello_mrt2 -j10

# 4. Build Follow
cmake -B build -DFOLLOW_BUILD_APP=ON -DMAGENTA_RT_DIR=~/magenta-realtime
cmake --build build -j
```

Budget **~25 GB free disk** and about an hour for a cold setup: TFLite clones the entire
TensorFlow repository, and configure alone took 649 s on an M-series MacBook Pro.

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
