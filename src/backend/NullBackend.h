// GhostBand — live AI accompaniment for singer-songwriters.
// Copyright 2026 GhostBand contributors. Licensed under Apache-2.0.

#pragma once

#include "../core/GhostBandConstants.h"
#include "../core/IGenerationBackend.h"

#include <atomic>

namespace ghostband::backend {

/// A backend that generates nothing, and says so.
///
/// **This is not a mock of MRT2 and must never be presented as one.** It exists because
/// "no model installed" is a real state a user can be in — before first-run setup, or
/// after moving the resources directory — and the honest response is silence plus a clear
/// "NO MODEL" status, not a fabricated band. `isRealBackend()` returns false precisely so
/// the UI cannot accidentally show it as READY (`CLAUDE.md` rule 2).
///
/// It is also what lets the whole audio path, safety stage, and UI be exercised on
/// machines where MRT2 cannot build at all — including this project's Linux CI.
///
/// It deliberately does **not** synthesise a test tone or placeholder music. A developer
/// convenience that sounds like a working band is exactly the kind of thing that ends up
/// demoed by accident.
class NullBackend final : public core::IGenerationBackend {
public:
    NullBackend() = default;

    bool initAssets(const std::string&) override { return true; }
    bool loadModel(const std::string&) override {
        loaded_.store(true, std::memory_order_relaxed);
        return true;
    }
    void unload() override {
        stop();
        loaded_.store(false, std::memory_order_relaxed);
    }
    void start() override { running_.store(true, std::memory_order_relaxed); }
    void stop() override { running_.store(false, std::memory_order_relaxed); }
    void reset() override { allNotesOff(); }
    bool isLoaded() const override { return loaded_.load(std::memory_order_relaxed); }

    /// Writes silence. Returns **true**: silence-by-design is not an underrun, and
    /// reporting it as one would spuriously trip `SafetyMonitor` into Degraded.
    bool readStereo(float* left, float* right, std::size_t numSamples) noexcept override {
        for (std::size_t i = 0; i < numSamples; ++i) {
            left[i] = 0.0f;
            right[i] = 0.0f;
        }
        return true;
    }

    void noteOn(int midiNote) noexcept override {
        if (midiNote >= 0 && midiNote < core::kNumMidiNotes) {
            notes_[static_cast<std::size_t>(midiNote)].store(true, std::memory_order_relaxed);
        }
    }
    void noteOff(int midiNote) noexcept override {
        if (midiNote >= 0 && midiNote < core::kNumMidiNotes) {
            notes_[static_cast<std::size_t>(midiNote)].store(false, std::memory_order_relaxed);
        }
    }
    void allNotesOff() noexcept override {
        for (auto& n : notes_) n.store(false, std::memory_order_relaxed);
    }
    /// Test/inspection helper — not part of the backend interface.
    bool isNoteHeld(int midiNote) const noexcept {
        return midiNote >= 0 && midiNote < core::kNumMidiNotes
            && notes_[static_cast<std::size_t>(midiNote)].load(std::memory_order_relaxed);
    }
    int heldNoteCount() const noexcept {
        int count = 0;
        for (const auto& n : notes_) {
            if (n.load(std::memory_order_relaxed)) ++count;
        }
        return count;
    }

    void setTextPrompt(const std::string&) override {}
    void setTextPrompts(const std::vector<std::string>&, const std::vector<float>&) override {}
    void setBlendWeights(const float*, int) noexcept override {}
    /// Idle, never Ready: there is no style to encode because there is no model.
    core::PromptStatus promptStatus() const override { return core::PromptStatus::Idle; }

    void setDrumless(bool v) noexcept override { drumless_.store(v, std::memory_order_relaxed); }
    void setCfgMusicCoca(float v) noexcept override { cfg_musiccoca_.store(v, std::memory_order_relaxed); }
    void setCfgNotes(float v) noexcept override { cfg_notes_.store(v, std::memory_order_relaxed); }
    void setCfgDrums(float v) noexcept override { cfg_drums_.store(v, std::memory_order_relaxed); }
    void setTemperature(float v) noexcept override { temperature_.store(v, std::memory_order_relaxed); }
    void setTopK(int v) noexcept override { top_k_.store(v, std::memory_order_relaxed); }

    void setMute(bool v) noexcept override { muted_.store(v, std::memory_order_relaxed); }
    void setGenerationBufferSamples(std::size_t n) override { buffer_samples_ = n; }

    core::GenerationMetrics metrics() const override {
        core::GenerationMetrics m;
        m.bufferCapacity = buffer_samples_;
        m.bufferAvailable = running_.load(std::memory_order_relaxed) ? buffer_samples_ : 0;
        return m;
    }

    const char* name() const noexcept override { return "none (no model loaded)"; }
    bool isRealBackend() const noexcept override { return false; }

    /// @name Inspection for tests
    /// @{
    bool isRunning() const noexcept { return running_.load(std::memory_order_relaxed); }
    bool drumless() const noexcept { return drumless_.load(std::memory_order_relaxed); }
    float cfgDrums() const noexcept { return cfg_drums_.load(std::memory_order_relaxed); }
    float cfgMusicCoca() const noexcept { return cfg_musiccoca_.load(std::memory_order_relaxed); }
    float cfgNotes() const noexcept { return cfg_notes_.load(std::memory_order_relaxed); }
    float temperature() const noexcept { return temperature_.load(std::memory_order_relaxed); }
    bool muted() const noexcept { return muted_.load(std::memory_order_relaxed); }
    /// @}

private:
    std::atomic<bool> loaded_{false};
    std::atomic<bool> running_{false};
    std::atomic<bool> muted_{false};
    std::atomic<bool> drumless_{false};
    std::atomic<float> cfg_musiccoca_{3.0f};
    std::atomic<float> cfg_notes_{5.0f};
    std::atomic<float> cfg_drums_{1.0f};
    std::atomic<float> temperature_{1.0f};
    std::atomic<int> top_k_{100};
    std::size_t buffer_samples_ = core::kDefaultGenerationBufferSamples;
    std::atomic<bool> notes_[core::kNumMidiNotes] = {};
};

} // namespace ghostband::backend
