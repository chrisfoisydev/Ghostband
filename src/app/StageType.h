// GhostBand — live AI accompaniment for singer-songwriters.
// Copyright 2026 GhostBand contributors. Licensed under Apache-2.0.

#pragma once

#include <juce_graphics/juce_graphics.h>

namespace ghostband::app {

/// The type system from the `GhostBand · Standalone` design canvas.
///
/// **The tracking is the design.** The canvas sets letter-spacing between 0.14em and
/// 0.28em on nearly every label, against a scale that is much smaller than it looks —
/// 10, 11, 12 and 13px dominate, with display type jumping straight to 22–40px. Wide,
/// tiny, upper-case labels beneath big condensed display type is the entire visual
/// identity, and none of it implies any capability the app does not have.
///
/// This was missed in the first design pass, which changed colours and words and left the
/// type alone — so the result looked unchanged. Recording that here because "the palette
/// is the design" was a wrong assumption, cheaply made and expensive to notice.
///
/// JUCE expresses letter-spacing as `withExtraKerningFactor`, a proportion of the font
/// height, which maps directly onto CSS `em` tracking.

/// Display face. Anton in the design — a heavy condensed sans, which is also what
/// `CLAUDE.md` specifies for the GHOSTBAND wordmark.
///
/// Anton is not installed on macOS, and bundling it means a `BinaryData` blob plus an
/// entry in `THIRD_PARTY_NOTICES.md` (it is OFL-1.1, so redistribution is fine). Until
/// that is done, this asks for the closest condensed faces present on macOS and lets JUCE
/// fall back. **The fallback has not been seen rendered** — if the display type does not
/// look condensed on screen, the font is missing and bundling is the fix, not a different
/// name.
inline juce::Font displayFont(float height, float tracking = 0.02f) {
    juce::Font f{juce::FontOptions("Helvetica Neue Condensed Black", height,
                                   juce::Font::bold)};
    return f.withExtraKerningFactor(tracking);
}

/// Section and screen headings. 22px in the canvas, tracked 0.12em.
inline juce::Font headingFont(float height = 22.0f) {
    return juce::Font{juce::FontOptions(height, juce::Font::bold)}
        .withExtraKerningFactor(0.12f);
}

/// The workhorse: a small upper-case label above or beside a value. This is the single
/// most common piece of type in the design, and the widest-tracked.
inline juce::Font labelFont(float height = 11.0f) {
    return juce::Font{juce::FontOptions(height, juce::Font::bold)}
        .withExtraKerningFactor(0.18f);
}

/// The value a label describes. Larger than its label, barely tracked — the contrast
/// between a wide quiet label and a tight loud value is what makes the pairing read.
inline juce::Font valueFont(float height = 15.0f) {
    return juce::Font{juce::FontOptions(height, juce::Font::plain)}
        .withExtraKerningFactor(0.02f);
}

/// Buttons: small, bold, widely tracked, upper case.
inline juce::Font buttonFont(float height = 12.0f) {
    return juce::Font{juce::FontOptions(height, juce::Font::bold)}
        .withExtraKerningFactor(0.16f);
}

/// Diagnostics only. Monospaced and untracked on purpose: it is a column of numbers to be
/// scanned, not a label to be read, and tracking would break the alignment that makes it
/// scannable.
inline juce::Font monoFont(float height = 13.0f) {
    return juce::Font{juce::FontOptions(juce::Font::getDefaultMonospacedFontName(),
                                        height, juce::Font::plain)};
}

/// Chamfer depth for cards and the primary action, in pixels. The canvas cuts 14px on
/// buttons and 18px on the large cards; nothing on it is rounded, which is why there is no
/// corner *radius* here. See `chamferedRect` in StageChrome.h.
inline constexpr float kCardChamfer = 18.0f;
inline constexpr float kControlChamfer = 12.0f;

} // namespace ghostband::app
