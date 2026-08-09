// GhostBand — live AI accompaniment for singer-songwriters.
// Copyright 2026 GhostBand contributors. Licensed under Apache-2.0.

#pragma once

// ============================================================================
//  ⚠️  macOS / Apple Silicon ONLY, and NEVER COMPILED as of this commit.
//
//  Magenta RealTime 2's C++ engine hard-fails on non-Apple platforms by upstream
//  design (MLX/Metal + Apple frameworks + Objective-C++). This file is written
//  against the pinned upstream headers at commit 694a545 — every call used is
//  recorded in docs/MRT2_API_NOTES.md — but it has not been through a compiler.
//  Expect to fix signature mistakes on the first real build.
//  See KNOWN_ISSUES.md §1 and §2.
// ============================================================================

#if !defined(__APPLE__)
#error "Mrt2Backend is macOS-only. Build ghostband_core without GHOSTBAND_BUILD_APP elsewhere."
#endif

#include "../core/GhostBandConstants.h"
#include "../core/IGenerationBackend.h"

#include <magentart/realtime_runner.h>

#include <atomic>
#include <string>

namespace ghostband::backend {

/// The **only** place in GhostBand where `magentart::` symbols appear.
///
/// Keeping the MRT2 surface in one translation unit means an upstream API break is a
/// compile error in one file rather than a scattered rewrite — and it is what allows the
/// rest of GhostBand to build and be tested on machines where MRT2 cannot compile at all.
///
/// GhostBand deliberately does **not** reimplement what `RealtimeRunner` already provides:
/// the 25 Hz inference thread, the lock-free stereo ring buffers, gain smoothing,
/// underrun counting, and the recording buffer are all upstream's. Duplicating them
/// would add latency and a second place for faults to hide.
class Mrt2Backend final : public core::IGenerationBackend {
public:
    Mrt2Backend() = default;
    ~Mrt2Backend() override;

    /// @name Lifecycle — control thread, blocking
    /// @{
    /// `resourceDir` must contain a `musiccoca/` subfolder of TFLite assets, as installed
    /// by `mrt models init`. Typically ~/Documents/Magenta/magenta-rt-v2/resources.
    bool initAssets(const std::string& resourceDir) override;
    /// `modelPath` is a `.mlxfn` model directory, e.g. …/models/mrt2_small/mrt2_small.mlxfn
    bool loadModel(const std::string& modelPath) override;
    void unload() override;
    void start() override;
    void stop() override;
    void reset() override;
    bool isLoaded() const override;
    /// @}

    /// Audio thread. Forwards to `RealtimeRunner::read_audio_stereo`, which upstream
    /// documents as lock-free and which zero-pads on underrun.
    /// `blocking` is hard-wired false: upstream explicitly warns never to pass true from
    /// an audio callback.
    bool readStereo(float* left, float* right, std::size_t numSamples) noexcept override;

    /// @name Harmony steering
    /// @{
    void noteOn(int midiNote) noexcept override;
    void noteOff(int midiNote) noexcept override;
    void allNotesOff() noexcept override;
    /// @}

    /// @name Style
    /// @{
    void setTextPrompt(const std::string& prompt) override;
    void setTextPrompts(const std::vector<std::string>& prompts,
                        const std::vector<float>& weights) override;
    void setBlendWeights(const float* weights, int count) noexcept override;
    core::PromptStatus promptStatus() const override;
    /// @}

    /// @name Arrangement parameters
    /// @{
    void setDrumless(bool drumless) noexcept override;
    void setCfgMusicCoca(float v) noexcept override;
    void setCfgNotes(float v) noexcept override;
    void setCfgDrums(float v) noexcept override;
    void setTemperature(float v) noexcept override;
    void setTopK(int k) noexcept override;
    /// @}

    void setMute(bool muted) noexcept override;
    void setGenerationBufferSamples(std::size_t samples) override;

    core::GenerationMetrics metrics() const override;

    const char* name() const noexcept override { return model_name_.c_str(); }
    bool isRealBackend() const noexcept override { return true; }

    /// Drain MRT2's internal log lines for the diagnostics view. Control thread.
    std::vector<std::string> drainEngineLogs();

private:
    magentart::core::RealtimeRunner runner_;

    /// We mirror held-note state because `RealtimeRunner` exposes `set_note_on` /
    /// `set_note_off` but no "release everything". An all-notes-off is mandatory on MIDI
    /// disconnect, song change, and panic — a stuck note would otherwise pin the band to
    /// one chord for the rest of the set.
    std::atomic<bool> held_notes_[core::kNumMidiNotes] = {};

    std::string model_name_ = "mrt2 (unloaded)";
    std::atomic<bool> assets_ready_{false};
};

} // namespace ghostband::backend
