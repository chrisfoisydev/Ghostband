// Follow — live AI accompaniment for singer-songwriters.
// Copyright 2026 Follow contributors. Licensed under Apache-2.0.

#pragma once

#include "EngineState.h"
#include "FollowConstants.h"

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace follow::core {

/// A consistent-enough picture of runtime health, for the developer diagnostics view
/// (brief §25) and for the performer-facing status block (§6).
///
/// "Consistent-enough": fields are read from independent atomics, so a snapshot can mix
/// values from adjacent blocks. That is fine — this is telemetry, not control. Nothing
/// makes a decision from a snapshot; policy lives in `SafetyMonitor`, which reads its own
/// state coherently.
struct DiagnosticsSnapshot {
    // Engine
    EngineState engineState = EngineState::Unloaded;
    const char* modelName = "none";

    // Audio device
    double sampleRate = 0.0;
    std::size_t blockSize = 0;

    // Generation health (sourced from MRT2's own EngineMetrics)
    float generationTotalMs = 0.0f;     ///< wall time for one 40 ms model frame
    float generationTransformerMs = 0.0f;
    std::size_t generationBufferAvailable = 0;
    std::size_t generationBufferCapacity = 0;

    // Faults
    std::uint64_t audioUnderruns = 0;   ///< our count: readStereo() returned false
    std::uint64_t droppedFrames = 0;    ///< MRT2's own cumulative count
    std::uint64_t blocksProcessed = 0;

    // Levels
    float outputPeakDb = -120.0f;
    float gainReductionDb = 0.0f;

    // Host-supplied (JUCE knows these; core does not)
    float audioCpuLoad = 0.0f;          ///< 0..1, fraction of the callback budget used
    double memoryUsageGb = 0.0;

    // MIDI
    bool midiConnected = false;
    int lastMidiNotes[4] = {-1, -1, -1, -1};

    /// Fraction of the 40 ms frame budget consumed by generation. > 1.0 means MRT2
    /// cannot keep up in real time and underruns are coming.
    float generationHeadroomRatio() const noexcept {
        constexpr float kFrameBudgetMs = 1000.0f * static_cast<float>(kFrameSamples)
                                       / static_cast<float>(kSampleRate); // 40 ms
        return generationTotalMs / kFrameBudgetMs;
    }
};

/// Lock-free diagnostics counters.
///
/// Every mutator is callable from the audio thread: relaxed atomics only, no allocation,
/// no logging. Per `CLAUDE.md`, the audio thread never emits text — it only bumps
/// integers that a UI timer reads later.
class Diagnostics {
public:
    /// @name Audio-thread mutators
    /// @{
    void noteBlock(std::size_t numSamples, bool underran) noexcept {
        blocks_.fetch_add(1, std::memory_order_relaxed);
        samples_.fetch_add(numSamples, std::memory_order_relaxed);
        if (underran) underruns_.fetch_add(1, std::memory_order_relaxed);
    }
    /// @}

    /// @name Control-thread setters
    /// @{
    void setSampleRate(double sr) noexcept { sample_rate_.store(sr, std::memory_order_relaxed); }
    void setBlockSize(std::size_t n) noexcept { block_size_.store(n, std::memory_order_relaxed); }
    void setModelName(const char* n) noexcept { model_name_.store(n, std::memory_order_relaxed); }
    void setAudioCpuLoad(float f) noexcept { cpu_load_.store(f, std::memory_order_relaxed); }
    void setMemoryUsageGb(double g) noexcept { memory_gb_.store(g, std::memory_order_relaxed); }
    void setMidiConnected(bool c) noexcept { midi_connected_.store(c, std::memory_order_relaxed); }

    void setGenerationMetrics(float totalMs, float transformerMs,
                              std::size_t bufAvailable, std::size_t bufCapacity,
                              std::uint64_t droppedFrames) noexcept {
        gen_total_ms_.store(totalMs, std::memory_order_relaxed);
        gen_transformer_ms_.store(transformerMs, std::memory_order_relaxed);
        gen_buf_avail_.store(bufAvailable, std::memory_order_relaxed);
        gen_buf_cap_.store(bufCapacity, std::memory_order_relaxed);
        dropped_frames_.store(droppedFrames, std::memory_order_relaxed);
    }
    /// @}

    std::uint64_t underruns() const noexcept { return underruns_.load(std::memory_order_relaxed); }
    std::uint64_t blocksProcessed() const noexcept { return blocks_.load(std::memory_order_relaxed); }
    std::uint64_t samplesProcessed() const noexcept { return samples_.load(std::memory_order_relaxed); }

    /// Clear fault counters after surfacing them. Control thread.
    void resetFaultCounters() noexcept {
        underruns_.store(0, std::memory_order_relaxed);
        dropped_frames_.store(0, std::memory_order_relaxed);
    }

    DiagnosticsSnapshot snapshot() const noexcept {
        DiagnosticsSnapshot s;
        s.modelName = model_name_.load(std::memory_order_relaxed);
        s.sampleRate = sample_rate_.load(std::memory_order_relaxed);
        s.blockSize = block_size_.load(std::memory_order_relaxed);
        s.generationTotalMs = gen_total_ms_.load(std::memory_order_relaxed);
        s.generationTransformerMs = gen_transformer_ms_.load(std::memory_order_relaxed);
        s.generationBufferAvailable = gen_buf_avail_.load(std::memory_order_relaxed);
        s.generationBufferCapacity = gen_buf_cap_.load(std::memory_order_relaxed);
        s.audioUnderruns = underruns_.load(std::memory_order_relaxed);
        s.droppedFrames = dropped_frames_.load(std::memory_order_relaxed);
        s.blocksProcessed = blocks_.load(std::memory_order_relaxed);
        s.audioCpuLoad = cpu_load_.load(std::memory_order_relaxed);
        s.memoryUsageGb = memory_gb_.load(std::memory_order_relaxed);
        s.midiConnected = midi_connected_.load(std::memory_order_relaxed);
        return s;
    }

private:
    std::atomic<std::uint64_t> blocks_{0};
    std::atomic<std::uint64_t> samples_{0};
    std::atomic<std::uint64_t> underruns_{0};
    std::atomic<std::uint64_t> dropped_frames_{0};

    std::atomic<double> sample_rate_{0.0};
    std::atomic<std::size_t> block_size_{0};
    std::atomic<const char*> model_name_{"none"};

    std::atomic<float> gen_total_ms_{0.0f};
    std::atomic<float> gen_transformer_ms_{0.0f};
    std::atomic<std::size_t> gen_buf_avail_{0};
    std::atomic<std::size_t> gen_buf_cap_{0};

    std::atomic<float> cpu_load_{0.0f};
    std::atomic<double> memory_gb_{0.0};
    std::atomic<bool> midi_connected_{false};
};

} // namespace follow::core
