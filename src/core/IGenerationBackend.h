// Follow — live AI accompaniment for singer-songwriters.
// Copyright 2026 Follow contributors. Licensed under Apache-2.0.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace follow::core {

/// Mirrors MRT2's `EngineMetrics` (upstream `realtime_runner.h`), minus fields that are
/// meaningful only to a DAW host.
struct GenerationMetrics {
    float totalMs = 0.0f;          ///< wall time to generate one 40 ms frame
    float transformerMs = 0.0f;
    std::size_t bufferAvailable = 0;
    std::size_t bufferCapacity = 0;
    std::uint64_t droppedFrames = 0;
};

/// Async style-encode status. Values mirror MRT2's status codes exactly
/// (0 idle, 1 fetching, 2 success, 3 error) — see docs/MRT2_API_NOTES.md §6.2.
enum class PromptStatus { Idle = 0, Encoding = 1, Ready = 2, Error = 3 };

/// The seam between Follow and Magenta RealTime 2.
///
/// **Every method here corresponds to a call verified in pinned upstream source**
/// (docs/MRT2_API_NOTES.md). Nothing is aspirational. If MRT2 gains a capability, it is
/// verified there first and added here second — never the other way round.
///
/// The interface exists for two concrete reasons, not for abstraction's own sake:
///   1. MRT2 cannot compile off Apple Silicon, so without this seam none of Follow's
///      logic could be built or tested anywhere else — including CI.
///   2. It keeps `magentart::` symbols confined to one translation unit, so an upstream
///      API break is a compile error in one file rather than a scattered rewrite.
///
/// Threading contract, which implementations must honour:
///   - `readStereo()` is called **from the audio thread** and must be lock-free and
///     allocation-free.
///   - `noteOn/noteOff/allNotesOff` may be called from the MIDI thread; must be atomic.
///   - Parameter setters may be called from any thread; must be atomic.
///   - Lifecycle calls (`initAssets`/`loadModel`/`unload`/`start`/`stop`/`reset`) are
///     control-thread only and may block.
class IGenerationBackend {
public:
    virtual ~IGenerationBackend() = default;

    /// @name Lifecycle — control thread, may block
    /// @{
    /// Load MusicCoCa/SpectroStream TFLite assets from a resource directory.
    virtual bool initAssets(const std::string& resourceDir) = 0;
    /// Load the streaming model (a `.mlxfn` directory for MRT2).
    virtual bool loadModel(const std::string& modelPath) = 0;
    virtual void unload() = 0;
    virtual void start() = 0;
    virtual void stop() = 0;
    virtual void reset() = 0;
    virtual bool isLoaded() const = 0;
    /// @}

    /// @name Audio thread — must be lock-free
    /// @{
    /// Pull `numSamples` of stereo audio. Returns **false on underrun**; the buffers are
    /// still fully written (zero-padded), matching MRT2's documented behaviour, so the
    /// caller never has to handle a partial fill.
    virtual bool readStereo(float* left, float* right, std::size_t numSamples) noexcept = 0;
    /// @}

    /// @name Harmony steering — MIDI thread, atomic
    /// @{
    virtual void noteOn(int midiNote) noexcept = 0;
    virtual void noteOff(int midiNote) noexcept = 0;
    /// Release every held note. Required on device disconnect, song change, and panic —
    /// a stuck note would otherwise pin the band to a chord forever.
    virtual void allNotesOff() noexcept = 0;
    /// @}

    /// @name Style — control thread; encoding is asynchronous
    /// @{
    virtual void setTextPrompt(const std::string& prompt) = 0;
    virtual void setTextPrompts(const std::vector<std::string>& prompts,
                                const std::vector<float>& weights) = 0;
    /// Ramp between already-encoded prompt slots. This is how section transitions avoid
    /// paying encode latency mid-song (ARCHITECTURE.md §8).
    virtual void setBlendWeights(const float* weights, int count) noexcept = 0;
    virtual PromptStatus promptStatus() const = 0;
    /// @}

    /// @name Arrangement parameters — atomic
    /// These are the raw MRT2 controls that `IntensityMacro` drives. There is no native
    /// intensity knob; see docs/MRT2_API_NOTES.md §6.3.
    /// @{
    virtual void setDrumless(bool drumless) noexcept = 0;
    virtual void setCfgMusicCoca(float v) noexcept = 0;
    virtual void setCfgNotes(float v) noexcept = 0;
    virtual void setCfgDrums(float v) noexcept = 0;
    virtual void setTemperature(float v) noexcept = 0;
    virtual void setTopK(int k) noexcept = 0;
    /// @}

    /// @name Backend-side output control
    /// Follow's user-facing AI Output Level lives in `AiOutputStage`, not here — one
    /// authority for gain. These are secondary/belt-and-braces only.
    /// @{
    virtual void setMute(bool muted) noexcept = 0;
    virtual void setGenerationBufferSamples(std::size_t samples) = 0;
    /// @}

    virtual GenerationMetrics metrics() const = 0;

    /// Identifier for diagnostics, e.g. "mrt2_small".
    virtual const char* name() const noexcept = 0;

    /// **False means this backend does not generate music.** The UI must never present a
    /// non-real backend as a working AI band — it exists for development and for honest
    /// degradation when no model is installed. Enforces `CLAUDE.md` rule 2.
    virtual bool isRealBackend() const noexcept = 0;
};

} // namespace follow::core
