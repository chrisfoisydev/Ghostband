// Follow — live AI accompaniment for singer-songwriters.
// Copyright 2026 Follow contributors. Licensed under Apache-2.0.
//
// ⚠️ macOS-only, NEVER COMPILED as of this commit. See KNOWN_ISSUES.md §1.

#pragma once

#include "core/AiOutputStage.h"
#include "core/EngineState.h"
#include "core/IGenerationBackend.h"

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include <memory>
#include <vector>

namespace follow::app {

/// Owns the audio device, the generation backend, and the safety stage; nothing else.
///
/// The UI talks to this class and never to the backend directly, so there is exactly one
/// place where the audio callback's invariants are enforced.
class FollowAudioEngine : private juce::AudioIODeviceCallback {
public:
    FollowAudioEngine();
    ~FollowAudioEngine() override;

    /// Bring up the audio device. Stereo out, 48 kHz requested.
    /// Returns an error string on failure, or an empty string on success.
    juce::String initialise();

    juce::AudioDeviceManager& deviceManager() noexcept { return device_manager_; }

    /// @name Model lifecycle — message thread; loading happens on a worker
    /// @{
    /// Kicks off asset init + model load on a background thread and returns immediately.
    /// `onFinished` is invoked on the message thread.
    void loadModelAsync(const juce::File& resourceDir, const juce::File& modelPath,
                        std::function<void(bool, juce::String)> onFinished);
    void startGeneration();
    void stopGeneration();
    /// @}

    /// @name Performance controls
    /// @{
    /// Fade the AI to silence and latch. Safe from any thread.
    void panic();
    void clearPanic();
    bool isPanicked() const noexcept { return output_stage_.isPanicked(); }

    void setAiBandOn(bool on) { output_stage_.setMuted(!on); }
    bool isAiBandOn() const noexcept { return !output_stage_.isMuted(); }

    void setOutputLevelDb(float db) { output_stage_.setOutputLevelDb(db); }
    float outputLevelDb() const noexcept { return output_stage_.outputLevelDb(); }

    void setTextPrompt(const juce::String& prompt);
    /// @}

    core::EngineState engineState() const noexcept { return state_.state(); }
    juce::String engineError() const { return juce::String(state_.errorReason()); }
    core::PromptStatus promptStatus() const;

    /// Snapshot for the diagnostics view. Message thread.
    core::DiagnosticsSnapshot diagnostics() const;

    core::AiOutputStage& outputStage() noexcept { return output_stage_; }
    core::Health health() const noexcept { return output_stage_.safetyMonitor().health(); }
    void recoverFromDegraded();

    /// True when the loaded backend actually generates music. False for NullBackend, in
    /// which case the UI must not claim an AI band is available.
    bool hasRealBackend() const noexcept;

    /// Non-empty when the device is not at 48 kHz — MRT2 generates 48 kHz and Follow does
    /// not resample, so this is surfaced rather than silently accepted.
    juce::String sampleRateWarning() const;

private:
    // juce::AudioIODeviceCallback
    void audioDeviceIOCallbackWithContext(const float* const* inputChannelData,
                                          int numInputChannels,
                                          float* const* outputChannelData,
                                          int numOutputChannels,
                                          int numSamples,
                                          const juce::AudioIODeviceCallbackContext& context) override;
    void audioDeviceAboutToStart(juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;
    void audioDeviceError(const juce::String& errorMessage) override;

    juce::AudioDeviceManager device_manager_;
    /// shared_ptr because backend ownership has to be handed across a std::function
    /// during an async load, and Mrt2Backend is neither copyable nor movable.
    /// Swaps are serialised by detaching the audio callback first — see loadModelAsync.
    std::shared_ptr<core::IGenerationBackend> backend_;
    core::AiOutputStage output_stage_;
    core::EngineStateMachine state_;

    /// Preallocated in audioDeviceAboutToStart. The audio callback never allocates.
    std::vector<float> scratch_l_;
    std::vector<float> scratch_r_;

    std::atomic<double> current_sample_rate_{0.0};
    std::atomic<int> current_block_size_{0};

    /// Which output pair the AI bus lands on. Phase 0 is fixed at 0/1; the routing UI
    /// (FOH channels 3+4) arrives with Setup Mode.
    std::atomic<int> out_channel_left_{0};
    std::atomic<int> out_channel_right_{1};

    std::unique_ptr<juce::ThreadPool> load_pool_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FollowAudioEngine)
};

} // namespace follow::app
