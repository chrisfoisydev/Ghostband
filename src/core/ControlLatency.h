// GhostBand — live AI accompaniment for singer-songwriters.
// Copyright 2026 GhostBand contributors. Licensed under Apache-2.0.

#pragma once

#include <cstddef>

namespace ghostband::core {

/// Breakdown of the delay between a control change and the first affected audio leaving
/// the device.
///
/// **This is transport only.** It does not include how long MRT2 takes to *musically*
/// react to new conditioning — the model card cites ~200 ms for that, it is upstream's,
/// and nothing GhostBand does changes it. Reporting the two separately matters: the
/// transport figure is the part that is ours and tunable, and mixing them would make the
/// number look immovable when most of it is not.
struct ControlLatencyEstimate {
    /// A control change is picked up by the inference loop on its next frame boundary, so
    /// this is 0 in the best case and a full frame in the worst.
    float frameQuantisationMaxMs = 0.0f;
    float generationBufferMs = 0.0f;
    float deviceBufferMs = 0.0f;
    float limiterLookaheadMs = 0.0f;

    /// Expected value: frame quantisation averages half a frame over many changes.
    float typicalMs = 0.0f;
    /// The number to quote when someone asks "how bad can it get".
    float worstCaseMs = 0.0f;

    /// Transport only — add MRT2's own response time for the figure a performer feels.
    float plusModelResponseMs(float modelResponseMs = 200.0f) const noexcept {
        return typicalMs + modelResponseMs;
    }
};

/// Compute the breakdown from live buffer state. Pure — no I/O, no globals — so the
/// arithmetic is testable without an audio device.
ControlLatencyEstimate estimateControlLatency(double sampleRate,
                                              std::size_t generationBufferSamples,
                                              std::size_t deviceBlockSamples,
                                              std::size_t limiterLookaheadSamples) noexcept;

} // namespace ghostband::core
