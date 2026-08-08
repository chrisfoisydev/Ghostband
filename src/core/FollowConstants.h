// Follow — live AI accompaniment for singer-songwriters.
// Copyright 2026 Follow contributors. Licensed under Apache-2.0.

#pragma once

#include <cstddef>

/// Format constants. These MIRROR Magenta RealTime 2 and must not drift from it.
///
/// Verified against upstream `core/include/magentart/mlx_engine.h` at commit 694a545
/// (see docs/MRT2_API_NOTES.md §3). We redeclare rather than include the MRT2 header
/// because `follow::core` must stay buildable on machines where MRT2 cannot compile at
/// all. `Mrt2Backend` static_asserts these against the real MRT2 constants, so any
/// upstream change fails the build loudly on macOS rather than silently detuning us.
namespace follow::core {

/// MRT2 generates 48 kHz stereo. Not configurable: it is a property of the model.
inline constexpr int kSampleRate = 48000;

/// One MRT2 model frame = 1920 samples @ 48 kHz = 40 ms (25 Hz generation cadence).
inline constexpr std::size_t kFrameSamples = 1920;

inline constexpr std::size_t kNumChannels = 2;

/// Maximum simultaneous MusicCoCa prompt slots (upstream `kMaxPrompts`).
/// This is the ceiling on pre-encoded section styles — see KNOWN_ISSUES.md §4.
inline constexpr std::size_t kMaxPrompts = 6;

/// MRT2 accepts 132 "pitches": 128 MIDI notes + 4 drum triggers.
/// Follow clamps musical input to 0..127 and reserves the rest.
inline constexpr int kNumMidiNotes = 128;
inline constexpr int kNumDrumTriggers = 4;
inline constexpr int kTotalPitches = kNumMidiNotes + kNumDrumTriggers;

/// Largest audio block we will ever be handed. Buffers are preallocated to this so the
/// audio callback never allocates, even if the device changes block size at runtime.
inline constexpr std::size_t kMaxBlockSamples = 8192;

/// PANIC fade duration. The brief calls for ~20–50 ms: long enough that the ramp is
/// inaudible as a click, short enough to read as instantaneous on stage.
inline constexpr float kDefaultPanicFadeMs = 30.0f;
inline constexpr float kMinPanicFadeMs = 20.0f;
inline constexpr float kMaxPanicFadeMs = 50.0f;

/// Generation buffer default: two model frames (~80 ms). One late inference frame then
/// cannot underrun us. Untuned on real hardware — see KNOWN_ISSUES.md §6.
inline constexpr std::size_t kDefaultGenerationBufferSamples = 2 * kFrameSamples;

/// Limiter ceiling. -1 dBFS leaves headroom for inter-sample peaks after the converter.
inline constexpr float kDefaultLimiterCeilingDb = -1.0f;

} // namespace follow::core
