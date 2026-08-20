// GhostBand — live AI accompaniment for singer-songwriters.
// Copyright 2026 GhostBand contributors. Licensed under Apache-2.0.
//
// ⚠️ macOS-only. Builds and runs on macOS/Apple Silicon; cannot be compiled on
// this Linux development machine at all. See KNOWN_ISSUES.md §1 — which means any change
// here is unverified until someone builds it on the Mac.

#pragma once

#include "GhostBandAudioEngine.h"

#include <juce_gui_extra/juce_gui_extra.h>

namespace ghostband::app {

/// The stage screen. The brief calls this the most important view in the product (§9).
///
/// **Design rule: the screen is peripheral.** The performer is looking at the audience,
/// glancing down occasionally from several feet away. So the current section name is
/// enormous, everything else is subordinate to it, and nothing moves unless the
/// performance moves. No menus, no small text, no animation, no decoration.
///
/// Everything here is reachable without the trackpad: left/right arrows change section,
/// Escape is PANIC, R repeats. Foot control (Phase 2.6) will map onto the same actions,
/// so the mouse path and the pedal path cannot diverge.
class PerformanceView : public juce::Component,
                        private juce::Timer {
public:
    explicit PerformanceView(GhostBandAudioEngine& engine);
    ~PerformanceView() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;
    /// Rebuilds the pedal legend on the way in. Mappings cannot change while this screen
    /// is up — the mapping screen is a different overlay — so once per entry is enough,
    /// and avoids copying the mapping set on every one of the 10 Hz repaints.
    void visibilityChanged() override;

    /// Invoked when the performer asks to leave the stage screen.
    std::function<void()> onExitRequested;

private:
    void timerCallback() override;
    void drawStatusRow(juce::Graphics& g, juce::Rectangle<int> area);
    void drawSectionBlock(juce::Graphics& g, juce::Rectangle<int> area);
    /// What the band is following right now: the chord being held, if any.
    void drawFollowingRow(juce::Graphics& g, juce::Rectangle<int> area);
    /// Band state and the SPARSE <-> FULL intensity bar.
    void drawBandRow(juce::Graphics& g, juce::Rectangle<int> area);
    void rebuildPedalLegend();

    GhostBandAudioEngine& engine_;

    juce::TextButton previous_button_{"PREVIOUS"};
    juce::TextButton next_button_{"NEXT"};
    /// The design names these LESS BAND / MORE BAND rather than an intensity percentage.
    /// On stage the question is "should the band do more or less", not "what number".
    juce::TextButton less_button_{"LESS BAND"};
    juce::TextButton more_button_{"MORE BAND"};
    juce::TextButton panic_button_{"PANIC"};
    juce::TextButton exit_button_{"SETUP"};

    /// Built from the real mappings, not from assumed switch letters. The design shows
    /// "PEDAL A / B", which presumes a known controller; GhostBand binds whatever MIDI
    /// message arrives, so the honest legend names the binding the performer actually made.
    juce::String pedal_legend_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PerformanceView)
};

} // namespace ghostband::app
