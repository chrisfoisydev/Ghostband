// GhostBand — live AI accompaniment for singer-songwriters.
// Copyright 2026 GhostBand contributors. Licensed under Apache-2.0.

#pragma once

#include "IGenerationBackend.h"

#include <array>
#include <atomic>
#include <string>
#include <vector>

namespace ghostband::core {

/// The parameter set one intensity value resolves to. Exposed as a value type so the
/// mapping can be tested, printed, and retuned without a backend in the loop.
struct IntensityParams {
    bool drumless = false;
    float cfgDrums = 1.0f;
    float cfgMusicCoca = 3.0f;
    float temperature = 1.0f;

    /// Blend weights over three prompt slots: [sparse, base, full]. Always sums to 1.
    /// `base` is the performer's own prompt; the other two are density variants of it.
    std::array<float, 3> promptWeights = {0.0f, 1.0f, 0.0f};
};

/// **AI Intensity — how much the band plays.** Not how loud: that is AI Output Level, and
/// the brief forbids conflating them.
///
/// MRT2 exposes no density parameter (docs/MRT2_API_NOTES.md §6.3), so this macro is
/// entirely GhostBand's invention. Everything it does is a hypothesis about which MRT2
/// controls read as "the band is doing less" — which is why the whole mapping lives in one
/// class, behind a `Tuning` struct, with no other responsibilities. Retuning is meant to
/// be editing data, not rewriting logic.
///
/// **The dominant term is the prompt blend.** Style text is by far MRT2's strongest
/// arrangement lever, so intensity mostly crossfades between a sparse and a full wording
/// of the performer's own prompt. The three slots are encoded once, up front; moving the
/// knob afterwards only changes blend weights, which are atomic and instant. Re-encoding
/// per knob movement would stall on MusicCoCa mid-performance.
///
/// **What it deliberately does not touch:** output gain (that is the level control) and
/// `cfg_notes` (that governs how tightly the band follows the performer's harmony, which
/// must not loosen just because the arrangement thinned out).
class IntensityMacro {
public:
    /// Every constant in the mapping, in one place. Defaults come from ARCHITECTURE.md §6
    /// and are centred on MRT2's own documented defaults where those exist.
    struct Tuning {
        /// Below this, drums are removed outright — the clearest "less band" signal MRT2
        /// offers. Hysteresis prevents flapping when intensity is automated across the
        /// threshold, which would otherwise drop the kit in and out on a slow fade.
        float drumlessOnBelow = 0.13f;
        float drumlessOffAbove = 0.17f;

        float cfgDrumsMin = 0.5f;   ///< at intensity 0
        float cfgDrumsMax = 2.0f;   ///< at intensity 1

        /// Guidance on the style prompt. Higher = more literal adherence, which makes a
        /// dense prompt denser. Centred on upstream's 3.0 default.
        float cfgMusicCocaMin = 2.0f;
        float cfgMusicCocaMax = 4.0f;

        /// Mild. Low intensity should mean *predictable*, which is what "restrained"
        /// implies on stage.
        float temperatureMin = 0.9f;
        float temperatureMax = 1.1f;

        /// Appended to the performer's prompt to build the two density variants. Phrased
        /// as musical attributes, never as an artist reference (brief §8).
        std::string sparseSuffix =
            ", sparse and minimal, very restrained, few instruments, leave space";
        std::string fullSuffix =
            ", full ensemble, rich and powerful arrangement, driving";
    };

    IntensityMacro() = default;

    /// 0 = barely there, 1 = full band. Clamped. Safe from any thread.
    void setIntensity(float intensity) noexcept;
    float intensity() const noexcept { return intensity_.load(std::memory_order_relaxed); }

    void setTuning(const Tuning& tuning) { tuning_ = tuning; }
    const Tuning& tuning() const noexcept { return tuning_; }

    /// Resolve the current intensity to parameters. Pure apart from drumless hysteresis,
    /// which depends on the previous state by design.
    IntensityParams compute() const noexcept;

    /// Push the current intensity to a backend. Does **not** set prompts — those are
    /// encoded once by `promptVariants()`; this only moves blend weights.
    void applyTo(IGenerationBackend& backend) const;

    /// The three prompt strings to encode up front, in slot order [sparse, base, full].
    /// Call once per prompt change, never per intensity change.
    std::vector<std::string> promptVariants(const std::string& basePrompt) const;

    /// Percentage for display, e.g. 68 for "Intensity: 68%".
    int percent() const noexcept;

private:
    Tuning tuning_;
    std::atomic<float> intensity_{0.5f};
    /// Hysteresis state for the drumless threshold. Mutable because `compute()` is
    /// conceptually a query, and the latch is an implementation detail of that query.
    mutable std::atomic<bool> drumless_latched_{false};
};

} // namespace ghostband::core
