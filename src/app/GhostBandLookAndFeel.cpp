// GhostBand — live AI accompaniment for singer-songwriters.
// Copyright 2026 GhostBand contributors. Licensed under Apache-2.0.

#include "GhostBandLookAndFeel.h"

namespace ghostband::app {

GhostBandLookAndFeel::GhostBandLookAndFeel() {
    using juce::Colour;

    // Window and panel grounds.
    setColour(juce::ResizableWindow::backgroundColourId, kBackground);
    setColour(juce::DocumentWindow::textColourId, kText);

    setColour(juce::TextButton::buttonColourId, kPanel);
    setColour(juce::TextButton::buttonOnColourId, kPanelRaised);
    setColour(juce::TextButton::textColourOffId, kText);
    setColour(juce::TextButton::textColourOnId, kText);

    setColour(juce::Label::textColourId, kText);
    setColour(juce::Label::backgroundColourId, Colour{0x00000000});

    // Text fields and combo boxes are cards, not panels: near-black fill, hairline border.
    setColour(juce::TextEditor::backgroundColourId, kCard);
    setColour(juce::TextEditor::textColourId, kText);
    setColour(juce::TextEditor::outlineColourId, kCardBorder);
    setColour(juce::TextEditor::focusedOutlineColourId, kDim);
    setColour(juce::TextEditor::highlightColourId, kDim.withAlpha(0.35f));

    setColour(juce::ComboBox::backgroundColourId, kCard);
    setColour(juce::ComboBox::textColourId, kText);
    setColour(juce::ComboBox::outlineColourId, kControlBorder);
    setColour(juce::ComboBox::arrowColourId, kDim);

    setColour(juce::PopupMenu::backgroundColourId, kCard);
    setColour(juce::PopupMenu::textColourId, kText);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, kPanelRaised);
    setColour(juce::PopupMenu::highlightedTextColourId, kText);

    setColour(juce::Slider::backgroundColourId, kControlBorder);
    setColour(juce::Slider::trackColourId, kOk);
    setColour(juce::Slider::thumbColourId, kText);
    setColour(juce::Slider::textBoxTextColourId, kText);
    setColour(juce::Slider::textBoxBackgroundColourId, kPanel);
    setColour(juce::Slider::textBoxOutlineColourId, Colour{0x00000000});

    setColour(juce::ToggleButton::textColourId, kText);
    setColour(juce::ToggleButton::tickColourId, kOk);
    setColour(juce::ToggleButton::tickDisabledColourId, kDim);

    setColour(juce::ListBox::backgroundColourId, kPanel);
    setColour(juce::ListBox::textColourId, kText);

    setColour(juce::ScrollBar::thumbColourId, kPanelRaised);
    setColour(juce::ScrollBar::trackColourId, kBackground);

    // The audio device selector is built from these and GhostBand never styles it directly.
    setColour(juce::GroupComponent::outlineColourId, kPanelRaised);
    setColour(juce::GroupComponent::textColourId, kDim);
}

juce::Font GhostBandLookAndFeel::getTextButtonFont(juce::TextButton&, int buttonHeight) {
    // Bounded rather than proportional: the canvas keeps button text small and tracked at
    // every size, and JUCE's default scales it with the button, which on a 96px-tall stage
    // button would produce something enormous.
    return buttonFont(juce::jlimit(10.0f, 14.0f, static_cast<float>(buttonHeight) * 0.32f));
}

juce::Font GhostBandLookAndFeel::getLabelFont(juce::Label& label) {
    return valueFont(juce::jmax(11.0f, static_cast<float>(label.getHeight()) * 0.6f));
}

juce::Font GhostBandLookAndFeel::getComboBoxFont(juce::ComboBox& box) {
    return valueFont(juce::jlimit(11.0f, 15.0f, static_cast<float>(box.getHeight()) * 0.55f));
}

juce::Font GhostBandLookAndFeel::getPopupMenuFont() { return valueFont(14.0f); }

void GhostBandLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& button,
                                                const juce::Colour& backgroundColour,
                                                bool shouldDrawButtonAsHighlighted,
                                                bool shouldDrawButtonAsDown) {
    const auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
    // Small chamfer for controls, larger for cards. The canvas uses 14px on its primary
    // action and nothing rounded anywhere.
    const auto path = chamferedRect(bounds, juce::jmin(12.0f, bounds.getHeight() * 0.3f));

    const bool primary = static_cast<bool>(button.getProperties()[prop::kPrimary]);
    const bool danger  = static_cast<bool>(button.getProperties()[prop::kDanger]);

    if (primary || danger) {
        // The only filled buttons in the design. One inverted primary per screen, and
        // PANIC — which is filled because it must be findable without reading it.
        auto fill = danger ? kPanicRed : kText;
        if (shouldDrawButtonAsDown)             fill = fill.brighter(0.15f);
        else if (shouldDrawButtonAsHighlighted) fill = fill.brighter(0.07f);

        g.setColour(button.isEnabled() ? fill : fill.withAlpha(0.35f));
        g.fillPath(path);
        return;
    }

    // Everything else is an outline on the bare ground. This is the single biggest visual
    // difference from JUCE's stock chrome, and from the previous pass, which filled every
    // button with a panel colour and produced a wall of grey slabs — the canvas has almost
    // no filled rectangles on it at all.
    if (shouldDrawButtonAsDown || button.getToggleState()) {
        g.setColour(kCard);
        g.fillPath(path);
    } else if (shouldDrawButtonAsHighlighted) {
        g.setColour(kCard.withAlpha(0.6f));
        g.fillPath(path);
    } else if (backgroundColour != kPanel && !backgroundColour.isTransparent()) {
        // A caller that deliberately set a fill colour still gets one, so an existing
        // semantic use (a warning-amber RECOVER BAND) is not silently flattened.
        g.setColour(backgroundColour);
        g.fillPath(path);
    }

    g.setColour(button.isEnabled()
                    ? (shouldDrawButtonAsHighlighted ? kControlBorderHover : kControlBorder)
                    : kControlBorder.withAlpha(0.4f));
    g.strokePath(path, juce::PathStrokeType(1.0f));
}

void GhostBandLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
                                            bool shouldDrawButtonAsHighlighted,
                                            bool /*shouldDrawButtonAsDown*/) {
    const auto height = static_cast<float>(button.getHeight());
    const float box_size = juce::jmin(16.0f, height - 4.0f);
    auto box = juce::Rectangle<float>(0.5f, (height - box_size) * 0.5f, box_size, box_size);

    // Square, not rounded. There is not a single rounded corner on the design canvas.
    g.setColour(shouldDrawButtonAsHighlighted ? kControlBorderHover : kControlBorder);
    g.drawRect(box, 1.0f);

    if (button.getToggleState()) {
        // A filled block rather than a tick: at this size a tick turns to mush, and the
        // canvas signals state with solid blocks throughout.
        g.setColour(button.isEnabled() ? kOk : kDim);
        g.fillRect(box.reduced(box_size * 0.26f));
    }

    g.setColour(button.findColour(juce::ToggleButton::textColourId)
                      .withMultipliedAlpha(button.isEnabled() ? 1.0f : 0.5f));
    g.setFont(buttonFont(11.0f));
    g.drawText(button.getButtonText(),
               button.getLocalBounds().withTrimmedLeft(static_cast<int>(box_size) + 10),
               juce::Justification::centredLeft);
}

void GhostBandLookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int width,
                                            int height, float sliderPos,
                                            float /*minSliderPos*/, float /*maxSliderPos*/,
                                            juce::Slider::SliderStyle style,
                                            juce::Slider& slider) {
    if (style != juce::Slider::LinearHorizontal) {
        // Anything else falls back to JUCE's drawing rather than being drawn wrongly. No
        // vertical or rotary sliders exist in GhostBand today; if one appears, it should
        // look stock and obviously unstyled rather than subtly broken.
        LookAndFeel_V4::drawLinearSlider(g, x, y, width, height, sliderPos,
                                         0.0f, 0.0f, style, slider);
        return;
    }

    // 4px, square-ended. Thinner and harder-edged than the previous pass, which drew a 6px
    // pill — the canvas's meters are all flat bars.
    const auto track = juce::Rectangle<float>(static_cast<float>(x),
                                              static_cast<float>(y) + static_cast<float>(height) * 0.5f - 2.0f,
                                              static_cast<float>(width), 4.0f);
    g.setColour(slider.findColour(juce::Slider::backgroundColourId));
    g.fillRect(track);

    const float filled = juce::jlimit(track.getX(), track.getRight(), sliderPos) - track.getX();
    if (filled > 0.0f) {
        g.setColour(slider.isEnabled() ? slider.findColour(juce::Slider::trackColourId) : kDim);
        g.fillRect(track.withWidth(filled));
    }

    // A slim vertical marker rather than a round thumb — the canvas has no circular
    // controls anywhere.
    g.setColour(slider.findColour(juce::Slider::thumbColourId));
    g.fillRect(juce::Rectangle<float>(sliderPos - 1.5f,
                                      static_cast<float>(y) + 4.0f,
                                      3.0f,
                                      static_cast<float>(height) - 8.0f));
}

} // namespace ghostband::app
