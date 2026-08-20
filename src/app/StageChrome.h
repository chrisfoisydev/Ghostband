// GhostBand — live AI accompaniment for singer-songwriters.
// Copyright 2026 GhostBand contributors. Licensed under Apache-2.0.

#pragma once

#include "StagePalette.h"
#include "StageType.h"

#include <juce_graphics/juce_graphics.h>

namespace ghostband::app {

/// The shape vocabulary of the `GhostBand · Standalone` design canvas.
///
/// **Why this file exists.** Two design passes changed colours, words and type and left the
/// app looking unchanged, because what actually distinguishes the canvas is none of those
/// things — it is the *shapes*: chamfered rectangles, hairline-separated rows on bare
/// ground, outlined rather than filled buttons, an 8px square status block rather than a
/// dot. Those are four drawing primitives, and once they exist every screen can be built
/// out of them instead of out of JUCE's default boxes.
///
/// Everything here is presentation. Nothing draws a control that does not exist, and
/// nothing here can imply a capability the app lacks.

/// A rectangle with the top-right and bottom-left corners cut off.
///
/// This is the canvas's signature: `clip-path: polygon(0 0, calc(100% - 18px) 0, 100% 18px,
/// 100% 100%, 18px 100%, 0 calc(100% - 18px))`. Two opposite corners, not four — it reads
/// as a machined bevel rather than a rounded UI card, which is the whole point of the
/// stage-hardware brief. `CLAUDE.md` calls for chamfered corners on the wordmark for the
/// same reason.
///
/// The previous pass used a 6px *rounded* radius throughout, which was simply the wrong
/// shape family.
inline juce::Path chamferedRect(juce::Rectangle<float> r, float cut) {
    juce::Path p;
    // Degenerate rectangles happen during layout (a row that has not been sized yet), and
    // a cut larger than half the shorter side turns the polygon inside out.
    cut = juce::jlimit(0.0f, juce::jmin(r.getWidth(), r.getHeight()) * 0.5f, cut);

    p.startNewSubPath(r.getX(), r.getY());
    p.lineTo(r.getRight() - cut, r.getY());
    p.lineTo(r.getRight(), r.getY() + cut);
    p.lineTo(r.getRight(), r.getBottom());
    p.lineTo(r.getX() + cut, r.getBottom());
    p.lineTo(r.getX(), r.getBottom() - cut);
    p.closeSubPath();
    return p;
}

/// A card: near-black fill, one-pixel border, chamfered. The fill is only two steps off the
/// background — in the canvas a card is defined by its border, not by contrast.
inline void drawCard(juce::Graphics& g, juce::Rectangle<int> bounds, float cut = 14.0f,
                     juce::Colour fill = kCard, juce::Colour border = kCardBorder) {
    const auto path = chamferedRect(bounds.toFloat().reduced(0.5f), cut);
    g.setColour(fill);
    g.fillPath(path);
    g.setColour(border);
    g.strokePath(path, juce::PathStrokeType(1.0f));
}

/// The one-pixel rule that separates rows. Drawn at the bottom edge of the row it closes.
inline void drawHairline(juce::Graphics& g, juce::Rectangle<int> row,
                         juce::Colour colour = kHairline) {
    g.setColour(colour);
    g.fillRect(row.getX(), row.getBottom() - 1, row.getWidth(), 1);
}

/// Status indicator: a small filled **square**, not an ellipse.
///
/// The canvas uses `width: 8px; height: 8px; display: block` everywhere a status is shown —
/// header, rig rows, device list. A circle in the same place reads as a generic UI dot; the
/// square reads as a panel lamp, which is the intent.
inline void drawStatusBlock(juce::Graphics& g, juce::Point<int> centre, juce::Colour colour,
                            float size = 8.0f) {
    g.setColour(colour);
    g.fillRect(juce::Rectangle<float>(size, size).withCentre(centre.toFloat()));
}

/// Small, bold, upper-case, widely tracked — the canvas's most common piece of type.
/// Returns the advance width so callers can flow a status block and its label together
/// without measuring twice.
inline int drawTrackedCaps(juce::Graphics& g, const juce::String& text,
                           juce::Rectangle<int> area, juce::Colour colour,
                           float height = 11.0f, float tracking = 0.18f,
                           juce::Justification justification = juce::Justification::centredLeft) {
    const auto font = juce::Font{juce::FontOptions(height, juce::Font::bold)}
                          .withExtraKerningFactor(tracking);
    g.setColour(colour);
    g.setFont(font);
    g.drawText(text.toUpperCase(), area, justification);
    return juce::GlyphArrangement::getStringWidthInt(font, text.toUpperCase());
}

/// The GHOSTBAND wordmark: one word, all caps, "GHOST" outlined and "BAND" solid.
///
/// Drawn typographically rather than shipped as a bitmap so it stays sharp at any scale and
/// inherits the palette — `CLAUDE.md` is explicit about both the construction and the
/// setting. `baseline` is the text baseline, not the top of the glyphs. Returns the total
/// advance width so a caller can lay out a nav bar to the right of it.
inline float drawWordmark(juce::Graphics& g, float x, float baseline, float height,
                          juce::Colour colour = kText) {
    const juce::Font mark = displayFont(height, 0.01f);

    juce::GlyphArrangement ghost;
    ghost.addLineOfText(mark, "GHOST", x, baseline);
    juce::Path ghost_path;
    ghost.createPath(ghost_path);

    g.setColour(colour);
    // JUCE has no "stroke this text" call, so the outline goes through GlyphArrangement.
    g.strokePath(ghost_path, juce::PathStrokeType(height * 0.055f));

    // Butt the two halves together — the mark is a single word, not two.
    const float ghost_width = ghost.getBoundingBox(0, -1, true).getWidth();

    juce::GlyphArrangement band;
    band.addLineOfText(mark, "BAND", x + ghost_width, baseline);
    juce::Path band_path;
    band.createPath(band_path);
    g.fillPath(band_path);

    return ghost_width + band.getBoundingBox(0, -1, true).getWidth();
}

/// Component property names read by GhostBandLookAndFeel to pick a button treatment.
///
/// Properties rather than subclasses because the buttons already exist all over the app and
/// a subclass would mean touching every declaration; a property is one line at the call
/// site and the LookAndFeel stays the only place that knows how each treatment is drawn.
namespace prop {
/// Inverted: light fill, dark text. The canvas allows exactly one per screen — the thing
/// you came to the screen to do.
inline constexpr const char* kPrimary = "ghostband-primary";
/// Destructive: filled red. PANIC only.
inline constexpr const char* kDanger = "ghostband-danger";
} // namespace prop

} // namespace ghostband::app
