// GhostBand — live AI accompaniment for singer-songwriters.
// Copyright 2026 GhostBand contributors. Licensed under Apache-2.0.

#pragma once

#include "StageChrome.h"
#include "StagePalette.h"
#include "StageType.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace ghostband::app {

/// Applies the design canvas's look to every standard JUCE component at once.
///
/// **Why a LookAndFeel rather than per-component styling.** The first design pass changed
/// colours and words but left every `TextButton`, `Label`, `Slider` and `ComboBox` drawing
/// itself with JUCE's stock appearance — rounded-grey-gradient buttons, untracked body
/// type. The result looked identical to what it replaced, because the stock chrome *was*
/// most of what was on screen. One LookAndFeel reaches all of it, including components
/// GhostBand never touches directly, like the audio device selector.
///
/// Everything here is presentation. No behaviour, no state, nothing that could imply a
/// capability the app does not have.
class GhostBandLookAndFeel : public juce::LookAndFeel_V4 {
public:
    GhostBandLookAndFeel();

    juce::Font getTextButtonFont(juce::TextButton&, int buttonHeight) override;
    juce::Font getLabelFont(juce::Label&) override;
    juce::Font getComboBoxFont(juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;

    /// Flat, rounded, no gradient — the brief rules out "gradients everywhere", and the
    /// canvas draws buttons as plain filled rectangles.
    void drawButtonBackground(juce::Graphics&, juce::Button&,
                              const juce::Colour& backgroundColour,
                              bool shouldDrawButtonAsHighlighted,
                              bool shouldDrawButtonAsDown) override;

    /// A square box with a filled square inside, rather than JUCE's tick. Matches the
    /// canvas, and reads better than a tick at the small sizes used here.
    void drawToggleButton(juce::Graphics&, juce::ToggleButton&,
                          bool shouldDrawButtonAsHighlighted,
                          bool shouldDrawButtonAsDown) override;

    /// Linear sliders become a thin track with a filled portion, matching the
    /// SPARSE<->FULL bar drawn by hand on the stage screen — so the same control does not
    /// look like two different things on two screens.
    void drawLinearSlider(juce::Graphics&, int x, int y, int width, int height,
                          float sliderPos, float minSliderPos, float maxSliderPos,
                          juce::Slider::SliderStyle, juce::Slider&) override;
};

} // namespace ghostband::app
