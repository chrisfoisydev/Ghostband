// GhostBand — live AI accompaniment for singer-songwriters.
// Copyright 2026 GhostBand contributors. Licensed under Apache-2.0.
//
// ⚠️ macOS-only, NEVER COMPILED as of this commit. See KNOWN_ISSUES.md §1.

#pragma once

#include "core/AiOutputStage.h"
#include "core/EngineState.h"
#include "core/IGenerationBackend.h"
#include "core/ControlLatency.h"
#include "core/IntensityMacro.h"
#include "core/MidiHarmonyState.h"
#include "core/PerformanceEngine.h"
#include "core/Song.h"

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include <memory>
#include <vector>

namespace ghostband::app {

/// Owns the audio device, the generation backend, and the safety stage; nothing else.
///
/// The UI talks to this class and never to the backend directly, so there is exactly one
/// place where the audio callback's invariants are enforced.
class GhostBandAudioEngine : private juce::AudioIODeviceCallback,
                             private juce::MidiInputCallback,
                             private juce::Timer {
public:
    GhostBandAudioEngine();
    ~GhostBandAudioEngine() override;

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

    void setAiBandOn(bool on) { ai_band_on_ = on; updateAiAudible(); }
    bool isAiBandOn() const noexcept { return ai_band_on_; }

    void setOutputLevelDb(float db) { output_stage_.setOutputLevelDb(db); }
    float outputLevelDb() const noexcept { return output_stage_.outputLevelDb(); }

    /// Encodes THREE slots — sparse / base / full density variants of this prompt — so
    /// that intensity afterwards costs only a blend-weight write. Encoding per knob
    /// movement would stall on MusicCoCa mid-performance.
    void setTextPrompt(const juce::String& prompt);

    /// The prompt currently encoded into MRT2 — not what is typed in the editor. The UI
    /// compares the two so an unapplied edit is visible rather than silently ignored.
    juce::String appliedPrompt() const { return base_prompt_; }

    /// Transport delay from a control change to affected audio leaving the device.
    /// Computed from live buffer state; excludes MRT2's own musical response time.
    core::ControlLatencyEstimate controlLatency() const;

    /// Generation buffer, in whole MRT2 frames (1 frame = 40 ms). Fewer frames means
    /// lower latency and less margin against a late inference frame.
    void setGenerationBufferFrames(int frames);
    int generationBufferFrames() const noexcept { return buffer_frames_; }

    /// **How much the band plays.** Distinct from AI Output Level, which is how loud.
    void setAiIntensity(float intensity);
    float aiIntensity() const noexcept { return intensity_.intensity(); }
    int aiIntensityPercent() const noexcept { return intensity_.percent(); }
    core::IntensityParams intensityParams() const noexcept { return intensity_.compute(); }
    /// @}

    /// @name Songs and sections (Phase 2)
    /// @{
    /// Load a song. The engine keeps its own copy, so the caller need not outlive it.
    bool loadSong(const core::Song& song);
    void loadDemoSong();
    bool hasSong() const noexcept { return performance_.hasSong(); }
    core::PerformanceEngine& performance() noexcept { return performance_; }
    const core::PerformanceEngine& performance() const noexcept { return performance_; }

    /// Section navigation. Each re-applies AI-enabled state, so a section that turns the
    /// band off mutes the output stage rather than stopping generation — stopping would
    /// cost a restart on the way back in.
    bool nextSection();
    bool previousSection();
    bool repeatSection();
    /// @}

    /// @name Harmony (Phase 1)
    ///
    /// Fed from two sources that are deliberately indistinguishable downstream: a real
    /// MIDI device, and the on-screen keyboard. Both land in the same MidiHarmonyState,
    /// so testing with the computer keyboard exercises the identical path a controller
    /// will use — not a parallel one that might diverge.
    /// @{
    core::MidiHarmonyState& harmony() noexcept { return harmony_; }
    const core::MidiHarmonyState& harmony() const noexcept { return harmony_; }

    /// Open every available MIDI input. Called at startup and on hot-plug.
    void refreshMidiInputs();
    juce::StringArray midiInputNames() const;
    bool anyMidiDeviceConnected() const noexcept;
    /// @}

    core::EngineState engineState() const noexcept { return state_.state(); }
    juce::String engineError() const { return juce::String(state_.errorReason()); }
    core::PromptStatus promptStatus() const;

    /// Snapshot for the diagnostics view. Message thread.
    core::DiagnosticsSnapshot diagnostics() const;

    /// Resident set size in GB. Message thread only — this makes a Mach syscall.
    static double residentMemoryGb();

    core::AiOutputStage& outputStage() noexcept { return output_stage_; }
    core::Health health() const noexcept { return output_stage_.safetyMonitor().health(); }
    void recoverFromDegraded();

    /// True when the loaded backend actually generates music. False for NullBackend, in
    /// which case the UI must not claim an AI band is available.
    bool hasRealBackend() const noexcept;

    /// Non-empty when the device is not at 48 kHz — MRT2 generates 48 kHz and GhostBand does
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

    // juce::Timer — drives section transitions. Control thread, ~50 Hz.
    void timerCallback() override;

    /// The band is audible only when the performer has it on AND the current section
    /// wants it. Recomputing from both avoids one silently overriding the other.
    void updateAiAudible();

    // juce::MidiInputCallback — called on the MIDI thread.
    void handleIncomingMidiMessage(juce::MidiInput* source,
                                   const juce::MidiMessage& message) override;

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

    core::MidiHarmonyState harmony_;
    core::PerformanceEngine performance_;
    /// Owned copy: Song is cheap to hold and this removes a lifetime trap where a UI
    /// component's song outlives, or fails to outlive, the engine driving it.
    core::Song loaded_song_;
    bool ai_band_on_ = false;
    double last_tick_ms_ = 0.0;
    core::IntensityMacro intensity_;
    juce::String base_prompt_;
    int buffer_frames_ = 2;
    juce::StringArray open_midi_inputs_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GhostBandAudioEngine)
};

} // namespace ghostband::app
