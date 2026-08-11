// GhostBand — live AI accompaniment for singer-songwriters.
// Copyright 2026 GhostBand contributors. Licensed under Apache-2.0.
//
// ⚠️ macOS-only, NEVER COMPILED as of this commit. See KNOWN_ISSUES.md §1.

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

    /// Invoked when the performer asks to leave the stage screen.
    std::function<void()> onExitRequested;

private:
    void timerCallback() override;
    void drawStatusRow(juce::Graphics& g, juce::Rectangle<int> area);
    void drawSectionBlock(juce::Graphics& g, juce::Rectangle<int> area);

    GhostBandAudioEngine& engine_;

    juce::TextButton previous_button_{"PREVIOUS"};
    juce::TextButton next_button_{"NEXT"};
    juce::TextButton panic_button_{"PANIC"};
    juce::TextButton exit_button_{"SETUP"};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PerformanceView)
};

} // namespace ghostband::app
