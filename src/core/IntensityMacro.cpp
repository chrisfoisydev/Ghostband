// GhostBand — live AI accompaniment for singer-songwriters.
// Copyright 2026 GhostBand contributors. Licensed under Apache-2.0.

#include "IntensityMacro.h"

#include <algorithm>
#include <cmath>

namespace ghostband::core {

namespace {
float lerp(float a, float b, float t) noexcept { return a + (b - a) * t; }
} // namespace

void IntensityMacro::setIntensity(float intensity) noexcept {
    intensity_.store(std::clamp(intensity, 0.0f, 1.0f), std::memory_order_relaxed);
}

int IntensityMacro::percent() const noexcept {
    return static_cast<int>(intensity() * 100.0f + 0.5f);
}

IntensityParams IntensityMacro::compute() const noexcept {
    const float x = intensity();

    IntensityParams p;
    p.cfgDrums = lerp(tuning_.cfgDrumsMin, tuning_.cfgDrumsMax, x);
    p.cfgMusicCoca = lerp(tuning_.cfgMusicCocaMin, tuning_.cfgMusicCocaMax, x);
    p.temperature = lerp(tuning_.temperatureMin, tuning_.temperatureMax, x);

    // Drumless with hysteresis. A bare threshold would drop the kit in and out around the
    // crossing point during a slow fade, which is far more noticeable than the transition
    // itself.
    bool drumless = drumless_latched_.load(std::memory_order_relaxed);
    if (!drumless && x < tuning_.drumlessOnBelow) drumless = true;
    else if (drumless && x > tuning_.drumlessOffAbove) drumless = false;
    drumless_latched_.store(drumless, std::memory_order_relaxed);
    p.drumless = drumless;

    // Crossfade sparse -> base -> full across the range. Weights always sum to 1, so the
    // conditioning strength stays constant and only its *character* moves; a blend that
    // summed to less would quietly weaken the style conditioning as well as thinning it.
    const float sparse = std::clamp(1.0f - 2.0f * x, 0.0f, 1.0f);
    const float full = std::clamp(2.0f * x - 1.0f, 0.0f, 1.0f);
    p.promptWeights = {sparse, 1.0f - sparse - full, full};

    return p;
}

void IntensityMacro::applyTo(IGenerationBackend& backend) const {
    const auto p = compute();

    backend.setDrumless(p.drumless);
    backend.setCfgDrums(p.cfgDrums);
    backend.setCfgMusicCoca(p.cfgMusicCoca);
    backend.setTemperature(p.temperature);
    backend.setBlendWeights(p.promptWeights.data(),
                            static_cast<int>(p.promptWeights.size()));

    // Deliberately absent: setCfgNotes and any gain change. Harmony must follow the
    // performer just as tightly at intensity 0.1 as at 0.9, and loudness is a separate
    // control the brief insists must not be conflated with this one.
}

std::vector<std::string> IntensityMacro::promptVariants(const std::string& basePrompt) const {
    // Slot order matches IntensityParams::promptWeights: [sparse, base, full].
    return {
        basePrompt + tuning_.sparseSuffix,
        basePrompt,
        basePrompt + tuning_.fullSuffix,
    };
}

} // namespace ghostband::core
