// GhostBand — live AI accompaniment for singer-songwriters.
// Copyright 2026 GhostBand contributors. Licensed under Apache-2.0.

#include "StageHeader.h"

#include <cstddef>   // std::size
#include <iterator>

namespace ghostband::app {

namespace {
// Canvas measurements, kept as named constants so the header can be checked against the
// design rather than eyeballed: padding 0 32px, gap 40px after the wordmark, 28px between
// nav entries, 22px between status lamps.
constexpr int kSidePadding = 32;
constexpr int kWordmarkGap = 40;
constexpr int kNavGap = 28;
constexpr int kLampGap = 22;
constexpr float kNavFontHeight = 12.0f;
constexpr float kNavTracking = 0.16f;
constexpr float kLampFontHeight = 11.0f;
constexpr float kLampTracking = 0.14f;
constexpr float kWordmarkHeight = 26.0f;

juce::Font navFont() {
    return juce::Font{juce::FontOptions(kNavFontHeight, juce::Font::bold)}
        .withExtraKerningFactor(kNavTracking);
}
} // namespace

StageHeader::StageHeader() {
    // PERFORM starts disabled: MainComponent enables it when a song is loaded. Starting
    // enabled would offer the stage screen before there is a set to run on it.
    items_ = {
        {Screen::Setup,   "SETUP",   true,  {}},
        {Screen::Songs,   "SONGS",   true,  {}},
        {Screen::Pedal,   "PEDAL",   true,  {}},
        {Screen::Perform, "PERFORM", false, {}},
    };
}

void StageHeader::setActiveScreen(Screen screen) {
    if (active_ == screen) return;
    active_ = screen;
    repaint();
}

void StageHeader::setScreenEnabled(Screen screen, bool enabled) {
    for (auto& item : items_) {
        if (item.screen != screen || item.enabled == enabled) continue;
        item.enabled = enabled;
        repaint();
        return;
    }
}

void StageHeader::setLampColours(juce::Colour audio, juce::Colour band, juce::Colour pedal) {
    // Called from a 10 Hz refresh, so this must not repaint unconditionally — a header that
    // invalidates itself ten times a second costs real frames on a machine that is also
    // running inference.
    if (audio == lamp_audio_ && band == lamp_band_ && pedal == lamp_pedal_) return;
    lamp_audio_ = audio;
    lamp_band_ = band;
    lamp_pedal_ = pedal;
    repaint();
}

void StageHeader::resized() {
    // Measure the wordmark the same way it will be drawn, so the nav cannot overlap it.
    const juce::Font mark = displayFont(kWordmarkHeight, 0.01f);
    wordmark_width_ =
        static_cast<float>(juce::GlyphArrangement::getStringWidthInt(mark, "GHOSTBAND"));

    int x = kSidePadding + juce::roundToInt(wordmark_width_) + kWordmarkGap;
    const auto font = navFont();
    for (auto& item : items_) {
        const int width = juce::GlyphArrangement::getStringWidthInt(font, item.text);
        item.bounds = juce::Rectangle<int>(x, 0, width, getHeight());
        x += width + kNavGap;
    }
}

const StageHeader::NavItem* StageHeader::itemAt(juce::Point<int> p) const {
    for (const auto& item : items_) {
        if (item.enabled && item.bounds.contains(p)) return &item;
    }
    return nullptr;
}

void StageHeader::mouseDown(const juce::MouseEvent& e) {
    const auto* item = itemAt(e.getPosition());
    if (item != nullptr && onNavigate) onNavigate(item->screen);
}

void StageHeader::mouseMove(const juce::MouseEvent& e) {
    int index = -1;
    for (int i = 0; i < static_cast<int>(items_.size()); ++i) {
        if (items_[static_cast<std::size_t>(i)].enabled
            && items_[static_cast<std::size_t>(i)].bounds.contains(e.getPosition())) {
            index = i;
            break;
        }
    }
    if (index == hovered_) return;
    hovered_ = index;
    setMouseCursor(index >= 0 ? juce::MouseCursor::PointingHandCursor
                              : juce::MouseCursor::NormalCursor);
    repaint();
}

void StageHeader::mouseExit(const juce::MouseEvent&) {
    if (hovered_ < 0) return;
    hovered_ = -1;
    repaint();
}

void StageHeader::paint(juce::Graphics& g) {
    g.fillAll(kBackground);

    // The bar is separated from the content by a hairline, not by a fill. The whole design
    // holds together on one-pixel rules over bare ground.
    g.setColour(kHairline);
    g.fillRect(0, getHeight() - 1, getWidth(), 1);

    // Baseline rather than a bounding box: the canvas sets the mark on the bar's optical
    // centre, which sits a little below the geometric one because the face is all caps.
    const float baseline = static_cast<float>(getHeight()) * 0.5f + kWordmarkHeight * 0.36f;
    drawWordmark(g, static_cast<float>(kSidePadding), baseline, kWordmarkHeight);

    g.setFont(navFont());
    for (int i = 0; i < static_cast<int>(items_.size()); ++i) {
        const auto& item = items_[static_cast<std::size_t>(i)];

        juce::Colour colour = kHeaderDim;
        if (!item.enabled)              colour = kHeaderDim.withAlpha(0.35f);
        else if (item.screen == active_) colour = kText;
        else if (i == hovered_)          colour = kText;

        g.setColour(colour);
        g.drawText(item.text, item.bounds, juce::Justification::centred);

        if (item.screen == active_) {
            // A 2px underline under the active entry. The canvas signals the active screen
            // with brightness alone; on a real display, at 12px and this much tracking,
            // brightness alone was not enough to find at a glance.
            g.setColour(kText);
            g.fillRect(item.bounds.getX(), getHeight() - 3, item.bounds.getWidth(), 2);
        }
    }

    // Status lamps, laid out from the right edge so they never collide with the nav.
    struct Lamp { const char* text; juce::Colour colour; };
    const Lamp lamps[] = {
        {"AUDIO", lamp_audio_}, {"BAND", lamp_band_}, {"PEDAL", lamp_pedal_},
    };

    const auto lamp_font = juce::Font{juce::FontOptions(kLampFontHeight, juce::Font::bold)}
                               .withExtraKerningFactor(kLampTracking);
    g.setFont(lamp_font);

    int right = getWidth() - kSidePadding;
    for (int i = static_cast<int>(std::size(lamps)) - 1; i >= 0; --i) {
        const auto& lamp = lamps[static_cast<std::size_t>(i)];
        const int text_width = juce::GlyphArrangement::getStringWidthInt(lamp_font, lamp.text);

        g.setColour(kHeaderDim);
        g.drawText(lamp.text,
                   juce::Rectangle<int>(right - text_width, 0, text_width, getHeight()),
                   juce::Justification::centred);

        drawStatusBlock(g, {right - text_width - 8 - 4, getHeight() / 2}, lamp.colour, 7.0f);
        right -= text_width + 8 + 7 + kLampGap;
    }
}

} // namespace ghostband::app
