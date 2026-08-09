// GhostBand — live AI accompaniment for singer-songwriters.
// Copyright 2026 GhostBand contributors. Licensed under Apache-2.0.
//
// ⚠️ macOS-only and NEVER COMPILED as of this commit. See Mrt2Backend.h.

#include "Mrt2Backend.h"

#include "../core/Logging.h"

#include <magentart/mlx_engine.h>

#include <filesystem>

namespace ghostband::backend {

using core::LogCategory;
using core::Logger;

// If upstream ever changes the audio format, these fire at build time rather than
// letting GhostBand quietly detune itself against a model it no longer matches.
static_assert(magentart::core::kFrameSamples == core::kFrameSamples,
              "MRT2 frame size changed — update ghostband::core::kFrameSamples");
static_assert(magentart::core::kNumChannels == core::kNumChannels,
              "MRT2 channel count changed — GhostBand assumes stereo throughout");
static_assert(magentart::core::kMaxPrompts == core::kMaxPrompts,
              "MRT2 prompt-slot count changed — section pre-encoding strategy depends on it");

Mrt2Backend::~Mrt2Backend() {
    // Order matters: stop the inference thread before tearing down the model.
    stop();
    runner_.unload();
}

bool Mrt2Backend::initAssets(const std::string& resourceDir) {
    if (!std::filesystem::exists(resourceDir)) {
        Logger::instance().error(LogCategory::Model, "MRT2 resource directory not found",
                                 {{"path", resourceDir},
                                  {"fix", "run `mrt models init`"}});
        return false;
    }

    const bool ok = runner_.init_assets(resourceDir.c_str());
    if (!ok) {
        Logger::instance().error(LogCategory::Model, "failed to init MRT2 TFLite assets",
                                 {{"path", resourceDir}});
    } else {
        Logger::instance().info(LogCategory::Model, "MRT2 assets ready",
                                {{"path", resourceDir}});
    }
    assets_ready_.store(ok, std::memory_order_release);
    return ok;
}

bool Mrt2Backend::loadModel(const std::string& modelPath) {
    if (!assets_ready_.load(std::memory_order_acquire)) {
        Logger::instance().error(LogCategory::Model,
                                 "loadModel called before initAssets succeeded");
        return false;
    }
    if (!std::filesystem::exists(modelPath)) {
        Logger::instance().error(LogCategory::Model, "model not found",
                                 {{"path", modelPath},
                                  {"fix", "run `mrt models download`"}});
        return false;
    }

    const bool ok = runner_.load_model(modelPath.c_str());
    if (ok) {
        model_name_ = std::filesystem::path(modelPath).stem().string();
        Logger::instance().info(LogCategory::Model, "model loaded", {{"model", model_name_}});
    } else {
        Logger::instance().error(LogCategory::Model, "model load failed",
                                 {{"path", modelPath}});
    }
    return ok;
}

void Mrt2Backend::unload() {
    allNotesOff();
    runner_.unload();
    model_name_ = "mrt2 (unloaded)";
    Logger::instance().info(LogCategory::Model, "model unloaded");
}

void Mrt2Backend::start() {
    runner_.start();
    Logger::instance().info(LogCategory::Generation, "generation started",
                            {{"model", model_name_}});
}

void Mrt2Backend::stop() {
    runner_.stop();
    Logger::instance().info(LogCategory::Generation, "generation stopped");
}

void Mrt2Backend::reset() {
    // Upstream `reset()` stops the inference thread, resets model state, clears the ring
    // buffers, and restarts if it was running. Clearing our held-note mirror alongside it
    // keeps the two in step — otherwise a note held across a reset would never be
    // released, because the note-off would go to a runner that no longer thinks it is on.
    allNotesOff();
    runner_.reset();
    Logger::instance().info(LogCategory::Generation, "generation reset");
}

bool Mrt2Backend::isLoaded() const {
    return runner_.is_loaded();
}

bool Mrt2Backend::readStereo(float* left, float* right, std::size_t numSamples) noexcept {
    // `blocking = false` is not a default we accept passively: upstream's header states
    // that blocking "waits up to one ring-buffer worth of samples — intended for offline
    // render only. Never pass `true` from the audio callback."
    return runner_.read_audio_stereo(left, right, numSamples, /*blocking=*/false);
}

void Mrt2Backend::noteOn(int midiNote) noexcept {
    // MRT2 accepts 0..131 (128 pitches + 4 drum triggers). GhostBand clamps musical input to
    // the pitch range and reserves the triggers; an out-of-range note is dropped rather
    // than wrapped, because a wrapped note is a wrong chord.
    if (midiNote < 0 || midiNote >= core::kNumMidiNotes) return;
    held_notes_[static_cast<std::size_t>(midiNote)].store(true, std::memory_order_relaxed);
    runner_.set_note_on(midiNote);
}

void Mrt2Backend::noteOff(int midiNote) noexcept {
    if (midiNote < 0 || midiNote >= core::kNumMidiNotes) return;
    held_notes_[static_cast<std::size_t>(midiNote)].store(false, std::memory_order_relaxed);
    runner_.set_note_off(midiNote);
}

void Mrt2Backend::allNotesOff() noexcept {
    for (int n = 0; n < core::kNumMidiNotes; ++n) {
        // exchange(): only send note-offs for notes we believe are held, and clear the
        // flag atomically so a concurrent noteOff cannot double-send or be lost.
        if (held_notes_[static_cast<std::size_t>(n)].exchange(false, std::memory_order_relaxed)) {
            runner_.set_note_off(n);
        }
    }
}

void Mrt2Backend::setTextPrompt(const std::string& prompt) {
    // Encoding is asynchronous (MusicCoCa on a TFLite worker thread). We deliberately do
    // NOT block here — a section change must never stall the performance. Callers poll
    // promptStatus(); Phase 2 pre-encodes every section prompt at song load so that a
    // live section change is only a blend-weight ramp. See ARCHITECTURE.md §8.
    runner_.set_text_prompt(prompt);
}

void Mrt2Backend::setTextPrompts(const std::vector<std::string>& prompts,
                                 const std::vector<float>& weights) {
    runner_.set_text_prompts(prompts, weights);
}

void Mrt2Backend::setBlendWeights(const float* weights, int count) noexcept {
    runner_.set_blend_weights(weights, count);
}

core::PromptStatus Mrt2Backend::promptStatus() const {
    // Upstream codes: 0 idle, 1 fetching, 2 success, 3 error — for BOTH the text encoder
    // and the quantizer. hello_mrt2 waits on both, so a prompt is only truly ready when
    // neither is still in flight.
    const int encoder = runner_.get_text_encoder_status();
    const int quantizer = runner_.get_quantizer_status();

    if (encoder == 3 || quantizer == 3) return core::PromptStatus::Error;
    if (encoder == 1 || quantizer == 1) return core::PromptStatus::Encoding;
    if (encoder == 2 && quantizer == 2) return core::PromptStatus::Ready;
    return core::PromptStatus::Idle;
}

void Mrt2Backend::setDrumless(bool drumless) noexcept { runner_.set_drumless(drumless); }
void Mrt2Backend::setCfgMusicCoca(float v) noexcept { runner_.set_cfg_musiccoca(v); }
void Mrt2Backend::setCfgNotes(float v) noexcept { runner_.set_cfg_notes(v); }
void Mrt2Backend::setCfgDrums(float v) noexcept { runner_.set_cfg_drums(v); }
void Mrt2Backend::setTemperature(float v) noexcept { runner_.set_temperature(v); }
void Mrt2Backend::setTopK(int k) noexcept { runner_.set_top_k(k); }

void Mrt2Backend::setMute(bool muted) noexcept {
    // Secondary only. GhostBand's authoritative mute/PANIC is AiOutputStage's fade, which
    // gates audio we have already read and therefore still works if this thread is hung.
    runner_.set_mute(muted);
}

void Mrt2Backend::setGenerationBufferSamples(std::size_t samples) {
    runner_.set_buffer_size(samples);
}

core::GenerationMetrics Mrt2Backend::metrics() const {
    const auto m = runner_.get_metrics();
    core::GenerationMetrics out;
    out.totalMs = m.total_ms;
    out.transformerMs = m.transformer_ms;
    out.bufferAvailable = m.buffer_available;
    out.bufferCapacity = m.buffer_capacity;
    out.droppedFrames = m.dropped_frames;
    return out;
}

void Mrt2Backend::resetDroppedFrames() noexcept {
    runner_.reset_dropped_frames();
}

std::vector<std::string> Mrt2Backend::drainEngineLogs() {
    return runner_.get_logs();
}

} // namespace ghostband::backend
