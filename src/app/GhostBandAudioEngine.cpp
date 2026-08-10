// GhostBand — live AI accompaniment for singer-songwriters.
// Copyright 2026 GhostBand contributors. Licensed under Apache-2.0.
//
// ⚠️ macOS-only, NEVER COMPILED as of this commit. See KNOWN_ISSUES.md §1.

#include "GhostBandAudioEngine.h"

#include "backend/Mrt2Backend.h"
#include "backend/NullBackend.h"
#include "core/GhostBandConstants.h"
#include "core/Logging.h"

#include <mach/mach.h>

namespace ghostband::app {

using core::LogCategory;
using core::Logger;

void GhostBandAudioEngine::handleIncomingMidiMessage(juce::MidiInput*,
                                                     const juce::MidiMessage& message) {
    // MIDI thread. Atomic stores only — no allocation, no logging, no locks. A device
    // sending a dense controller stream must not be able to stall anything.
    if (message.isNoteOn()) {
        harmony_.noteOn(message.getNoteNumber(), message.getVelocity());
    } else if (message.isNoteOff()) {
        harmony_.noteOff(message.getNoteNumber());
    } else if (message.isSustainPedalOn()) {
        harmony_.setSustainPedal(true);
    } else if (message.isSustainPedalOff()) {
        harmony_.setSustainPedal(false);
    } else if (message.isAllNotesOff() || message.isAllSoundOff()) {
        harmony_.allNotesOff();
    }
}

void GhostBandAudioEngine::refreshMidiInputs() {
    const auto devices = juce::MidiInput::getAvailableDevices();

    juce::StringArray names;
    for (const auto& d : devices) {
        names.add(d.name);
        if (!device_manager_.isMidiInputDeviceEnabled(d.identifier)) {
            device_manager_.setMidiInputDeviceEnabled(d.identifier, true);
            Logger::instance().info(LogCategory::Midi, "MIDI input opened",
                                    {{"device", d.name.toStdString()}});
        }
    }

    // A device that vanished may have left notes held. Releasing them is the difference
    // between a silent unplug and a chord stuck under the band for the rest of the song.
    if (names.size() < open_midi_inputs_.size()) {
        harmony_.allNotesOff();
        Logger::instance().warn(LogCategory::Midi,
                                "MIDI device disappeared - released all notes");
    }
    open_midi_inputs_ = names;
    output_stage_.diagnostics().setMidiConnected(!names.isEmpty());
}

juce::StringArray GhostBandAudioEngine::midiInputNames() const { return open_midi_inputs_; }

bool GhostBandAudioEngine::anyMidiDeviceConnected() const noexcept {
    return !open_midi_inputs_.isEmpty();
}

GhostBandAudioEngine::GhostBandAudioEngine() {
    // Start on the honest backend: no model is loaded yet, and NullBackend reports that
    // truthfully rather than pretending a band exists.
    backend_ = std::make_shared<backend::NullBackend>();
    harmony_.setBackend(backend_.get());
    load_pool_ = std::make_unique<juce::ThreadPool>(1);
}

GhostBandAudioEngine::~GhostBandAudioEngine() {
    device_manager_.removeMidiInputDeviceCallback({}, this);
    device_manager_.removeAudioCallback(this);
    device_manager_.closeAudioDevice();
    load_pool_.reset();
    backend_.reset();
}

juce::String GhostBandAudioEngine::initialise() {
    // Stereo out, no input in Phase 0: GhostBand is additive and must not sit in the
    // performer's signal path. Guitar input arrives in Phase 3 as a tap, never an insert.
    const juce::String error = device_manager_.initialiseWithDefaultDevices(0, 2);
    if (error.isNotEmpty()) {
        Logger::instance().error(LogCategory::Audio, "audio device init failed",
                                 {{"error", error.toStdString()}});
        return error;
    }

    // Ask for 48 kHz explicitly; MRT2 generates 48 kHz and GhostBand does not resample.
    if (auto* device = device_manager_.getCurrentAudioDevice()) {
        auto setup = device_manager_.getAudioDeviceSetup();
        if (setup.sampleRate != static_cast<double>(core::kSampleRate)) {
            setup.sampleRate = static_cast<double>(core::kSampleRate);
            device_manager_.setAudioDeviceSetup(setup, true);
        }
    }

    device_manager_.addAudioCallback(this);
    device_manager_.addMidiInputDeviceCallback({}, this); // {} = all enabled inputs
    refreshMidiInputs();
    return {};
}

void GhostBandAudioEngine::audioDeviceAboutToStart(juce::AudioIODevice* device) {
    const double sr = device->getCurrentSampleRate();
    const int block = device->getCurrentBufferSizeSamples();

    current_sample_rate_.store(sr, std::memory_order_relaxed);
    current_block_size_.store(block, std::memory_order_relaxed);

    // Everything the callback touches is sized here. Deliberately generous: a device can
    // change block size at runtime, and a reallocation inside the callback would be the
    // exact real-time violation this project forbids.
    const auto capacity = static_cast<std::size_t>(
        juce::jmax(block * 4, static_cast<int>(core::kMaxBlockSamples)));
    scratch_l_.assign(capacity, 0.0f);
    scratch_r_.assign(capacity, 0.0f);

    output_stage_.prepare(sr, capacity);
    output_stage_.diagnostics().setBlockSize(static_cast<std::size_t>(block));

    Logger::instance().info(LogCategory::Audio, "audio device started",
                            {{"device", device->getName().toStdString()},
                             {"sample_rate", std::to_string(sr)},
                             {"block", std::to_string(block)}});

    if (sr != static_cast<double>(core::kSampleRate)) {
        Logger::instance().warn(LogCategory::Audio,
                                "device is not at 48 kHz - MRT2 output will be mis-pitched",
                                {{"sample_rate", std::to_string(sr)}});
    }
}

void GhostBandAudioEngine::audioDeviceStopped() {
    Logger::instance().info(LogCategory::Audio, "audio device stopped");
}

void GhostBandAudioEngine::audioDeviceError(const juce::String& errorMessage) {
    // An expected runtime condition, not an exceptional one: interfaces get unplugged.
    // Fade the AI out so a half-configured device cannot emit noise, and let the
    // performer carry on — their voice and guitar never routed through us anyway.
    Logger::instance().error(LogCategory::Audio, "audio device error",
                             {{"error", errorMessage.toStdString()}});
    output_stage_.panic();
}

void GhostBandAudioEngine::audioDeviceIOCallbackWithContext(
    const float* const*, int,
    float* const* outputChannelData, int numOutputChannels, int numSamples,
    const juce::AudioIODeviceCallbackContext&) {

    // ---- REAL-TIME SECTION -------------------------------------------------
    // No allocation, no locks, no logging, no UI, no file I/O. The only calls made from
    // here are the backend's lock-free readStereo() and our own arithmetic-only stage.

    for (int ch = 0; ch < numOutputChannels; ++ch) {
        if (outputChannelData[ch] != nullptr) {
            juce::FloatVectorOperations::clear(outputChannelData[ch], numSamples);
        }
    }

    const auto n = static_cast<std::size_t>(numSamples);
    if (n > scratch_l_.size()) {
        // The device handed us a larger block than we prepared for. Emitting silence is
        // the only safe response: growing the buffer here would allocate on the audio
        // thread. audioDeviceAboutToStart will resize on the next device change.
        return;
    }

    const bool ok = backend_->readStereo(scratch_l_.data(), scratch_r_.data(), n);
    output_stage_.process(scratch_l_.data(), scratch_r_.data(), n, /*underran=*/!ok);

    const int left = out_channel_left_.load(std::memory_order_relaxed);
    const int right = out_channel_right_.load(std::memory_order_relaxed);

    if (left < numOutputChannels && outputChannelData[left] != nullptr) {
        juce::FloatVectorOperations::copy(outputChannelData[left], scratch_l_.data(), numSamples);
    }
    if (right < numOutputChannels && outputChannelData[right] != nullptr) {
        juce::FloatVectorOperations::copy(outputChannelData[right], scratch_r_.data(), numSamples);
    }
    // ---- END REAL-TIME SECTION ---------------------------------------------
}

void GhostBandAudioEngine::loadModelAsync(const juce::File& resourceDir,
                                       const juce::File& modelPath,
                                       std::function<void(bool, juce::String)> onFinished) {
    if (!state_.transitionTo(core::EngineState::Loading)) {
        onFinished(false, "Cannot load a model from state: " + juce::String(toString(state_.state())));
        return;
    }

    const juce::String resource_str = resourceDir.getFullPathName();
    const juce::String model_str = modelPath.getFullPathName();

    load_pool_->addJob([this, resource_str, model_str, onFinished] {
        // shared_ptr, not unique_ptr: this has to be captured by a std::function for
        // callAsync, which requires a copyable capture. Mrt2Backend is neither copyable
        // nor movable — it holds a RealtimeRunner, which owns a thread and a mutex — so
        // ownership has to travel by pointer, never by value.
        auto mrt2 = std::make_shared<backend::Mrt2Backend>();

        juce::String failure;
        bool ok = mrt2->initAssets(resource_str.toStdString());
        if (!ok) {
            failure = "Could not load MRT2 resources from " + resource_str
                    + "\nRun `mrt models init` to install them.";
        } else {
            ok = mrt2->loadModel(model_str.toStdString());
            if (!ok) {
                failure = "Could not load model " + model_str
                        + "\nRun `mrt models download` to install it.";
            }
        }

        if (ok) {
            mrt2->setGenerationBufferSamples(
                static_cast<std::size_t>(buffer_frames_) * core::kFrameSamples);
        }

        juce::MessageManager::callAsync(
            [this, ok, failure, mrt2, onFinished]() mutable {
                if (!ok) {
                    state_.fail(failure.toStdString());
                    onFinished(false, failure);
                    return;
                }

                // Detaching the callback is what makes the swap safe: it guarantees the
                // audio thread is not inside readStereo() while `backend_` changes.
                // Deliberately NOT done by panicking — panic is an operator-visible
                // latched state, and silently setting then clearing it here would
                // discard a panic the performer had engaged on purpose.
                device_manager_.removeAudioCallback(this);
                backend_ = mrt2;
                harmony_.allNotesOff();          // the old backend's notes are gone
                harmony_.setBackend(backend_.get());
                device_manager_.addAudioCallback(this);

                output_stage_.diagnostics().setModelName(backend_->name());
                state_.transitionTo(core::EngineState::Ready);
                onFinished(true, {});
            });
    });
}

void GhostBandAudioEngine::startGeneration() {
    if (!state_.transitionTo(core::EngineState::Running)) return;
    backend_->start();
    // Zero both fault tallies so the diagnostics figures describe *this* run. Without
    // this, idle reads taken before START stay on the display for the whole session.
    backend_->resetDroppedFrames();
    output_stage_.diagnostics().resetFaultCounters();
    // Order matters: arm the monitor only after the backend is actually running, so the
    // priming grace window starts from the moment audio can genuinely appear.
    output_stage_.setGenerating(true);
}

void GhostBandAudioEngine::stopGeneration() {
    if (!state_.transitionTo(core::EngineState::Ready)) return;
    // Disarm FIRST. Once stopped, the backend's ring buffer drains and readStereo()
    // correctly reports underruns on every block; policing those would latch Degraded
    // while the engine is merely idle.
    output_stage_.setGenerating(false);
    backend_->stop();
    harmony_.allNotesOff();
}

void GhostBandAudioEngine::panic() {
    // 1. Our fade. This is the guarantee: it gates audio we already hold, so it works
    //    even if the MRT2 inference thread is wedged.
    output_stage_.panic();
    Logger::instance().warn(LogCategory::Panic, "PANIC engaged");

    // 2. Belt and braces, best-effort, off the critical path.
    backend_->setMute(true);
    harmony_.allNotesOff();
}

void GhostBandAudioEngine::clearPanic() {
    output_stage_.clearPanic();
    backend_->setMute(false);
    Logger::instance().info(LogCategory::Panic, "PANIC released");
}

void GhostBandAudioEngine::setTextPrompt(const juce::String& prompt) {
    base_prompt_ = prompt;

    // Encode all three density variants at once. This is the async MusicCoCa pass, and
    // paying it here — on a prompt edit — is what keeps the intensity knob instant later.
    const auto variants = intensity_.promptVariants(prompt.toStdString());
    const auto params = intensity_.compute();
    const std::vector<float> weights(params.promptWeights.begin(), params.promptWeights.end());

    backend_->setTextPrompts(variants, weights);
    Logger::instance().info(LogCategory::Generation, "prompt set",
                            {{"variants", std::to_string(variants.size())}});
}

core::ControlLatencyEstimate GhostBandAudioEngine::controlLatency() const {
    return core::estimateControlLatency(
        current_sample_rate_.load(std::memory_order_relaxed),
        static_cast<std::size_t>(buffer_frames_) * core::kFrameSamples,
        static_cast<std::size_t>(current_block_size_.load(std::memory_order_relaxed)),
        output_stage_.limiter().latencySamples());
}

void GhostBandAudioEngine::setGenerationBufferFrames(int frames) {
    buffer_frames_ = juce::jlimit(1, 4, frames);
    backend_->setGenerationBufferSamples(
        static_cast<std::size_t>(buffer_frames_) * core::kFrameSamples);
    Logger::instance().info(LogCategory::Audio, "generation buffer changed",
                            {{"frames", std::to_string(buffer_frames_)}});
}

void GhostBandAudioEngine::setAiIntensity(float intensity) {
    intensity_.setIntensity(intensity);
    // Blend weights and sampling parameters only — all atomic, no re-encode, so this is
    // safe to move continuously while the band is playing.
    intensity_.applyTo(*backend_);
}

core::PromptStatus GhostBandAudioEngine::promptStatus() const {
    return backend_->promptStatus();
}

bool GhostBandAudioEngine::hasRealBackend() const noexcept {
    return backend_->isRealBackend();
}

void GhostBandAudioEngine::recoverFromDegraded() {
    output_stage_.recoverFromDegraded();
    Logger::instance().info(LogCategory::Generation, "recovered from degraded state");
}

juce::String GhostBandAudioEngine::sampleRateWarning() const {
    const double sr = current_sample_rate_.load(std::memory_order_relaxed);
    if (sr <= 0.0 || sr == static_cast<double>(core::kSampleRate)) return {};
    return "Device is at " + juce::String(sr, 0) + " Hz. MRT2 generates 48 000 Hz and "
           "GhostBand does not resample - set the interface to 48 kHz.";
}

double GhostBandAudioEngine::residentMemoryGb() {
    // Resident set size via Mach. The brief requires a memory figure in diagnostics and
    // a 60-minute run with no serious leak; "not measured" cannot answer either. Polled
    // from the UI timer, never from the audio thread.
    mach_task_basic_info info{};
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO,
                  reinterpret_cast<task_info_t>(&info), &count) != KERN_SUCCESS) {
        return 0.0;
    }
    return static_cast<double>(info.resident_size) / 1'000'000'000.0;
}

core::DiagnosticsSnapshot GhostBandAudioEngine::diagnostics() const {
    auto snap = output_stage_.diagnostics().snapshot();
    snap.engineState = state_.state();

    const auto m = backend_->metrics();
    snap.generationTotalMs = m.totalMs;
    snap.generationTransformerMs = m.transformerMs;
    snap.generationBufferAvailable = m.bufferAvailable;
    snap.generationBufferCapacity = m.bufferCapacity;
    snap.droppedFrames = m.droppedFrames;

    snap.outputPeakDb = output_stage_.peakDb();
    snap.gainReductionDb = output_stage_.gainReductionDb();
    snap.audioCpuLoad = static_cast<float>(device_manager_.getCpuUsage());
    snap.memoryUsageGb = residentMemoryGb();
    return snap;
}

} // namespace ghostband::app
