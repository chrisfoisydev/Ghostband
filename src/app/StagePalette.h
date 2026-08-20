// GhostBand — live AI accompaniment for singer-songwriters.
// Copyright 2026 GhostBand contributors. Licensed under Apache-2.0.

#pragma once

#include <juce_graphics/juce_graphics.h>

namespace ghostband::app {

/// The stage-hardware palette. Dark, high contrast, no gradients, no purple.
///
/// Defined once because it was previously copied into every view, and semantic colours
/// that are supposed to mean the same thing everywhere are exactly the kind of constant
/// that drifts when duplicated — a warning amber that is subtly different on one screen
/// teaches the performer nothing.
///
/// **Two grounds, deliberately.** Setup and the stage screen do not share a background:
/// the stage view is darker and its text brighter, because it is read from several feet
/// away under stage lighting rather than at a desk. The semantic colours are shared.

/// Values taken from the `GhostBand · Standalone` design canvas (2026-08-13). Warmer and
/// darker than the palette they replace: the ground is true black, the panels sit *above*
/// it rather than below, and the text is a warm off-white rather than neutral grey.
/// See `docs/DESIGN_GAP_ANALYSIS.md` §4 for the before/after table.

/// @name Semantic — identical on every screen
/// @{
inline const juce::Colour kOk{0xff46b96b};
inline const juce::Colour kWarn{0xffb98b2e};
inline const juce::Colour kFault{0xffd93a2b};
/// The design uses one red for both fault and PANIC. Kept as a separate name because
/// PANIC is a different kind of statement from an error, and may want to diverge again.
inline const juce::Colour kPanicRed{0xffd93a2b};
/// @}

/// @name Setup / editor screens — read at a desk
/// @{
inline const juce::Colour kBackground{0xff0a0a0a};
inline const juce::Colour kPanel{0xff1e2126};
/// One step lighter than kPanel, for a raised row or a selected item.
inline const juce::Colour kPanelRaised{0xff2c3036};
inline const juce::Colour kText{0xffedeae4};
inline const juce::Colour kDim{0xff8a8f97};
/// Secondary warm text — quieter than kText but not as cold as kDim.
inline const juce::Colour kMuted{0xffc9c5bd};
/// @}

/// @name Structure — the colours that build the canvas's *layout*, not its controls
///
/// These were the missing half of the palette. The first two design passes took the
/// semantic colours above and left every screen built out of JUCE's own boxes, so the app
/// was the right colour and the wrong shape. The canvas draws almost nothing as a filled
/// panel: it draws hairline-separated rows on the bare ground, occasional bordered cards
/// one step off black, and outlined buttons. That needs its own names.
/// @{
/// Row separators and the header's bottom edge. One pixel, everywhere.
inline const juce::Colour kHairline{0xff1e2126};
/// Card ground. Barely above the background on purpose — a card is marked by its border,
/// not by its fill.
inline const juce::Colour kCard{0xff101215};
inline const juce::Colour kCardBorder{0xff23262b};
/// Outlined controls. Buttons in the canvas are a 1px border and transparent fill.
inline const juce::Colour kControlBorder{0xff2c3036};
inline const juce::Colour kControlBorderHover{0xff555b63};
/// The quietest tracked caps: section kickers, column headings, units.
inline const juce::Colour kKicker{0xff6e737b};
/// Header nav and status text — between kKicker and kDim.
inline const juce::Colour kHeaderDim{0xff7c818a};
/// Body values beside a heading.
inline const juce::Colour kValue{0xffa8adb5};
/// Anton row headings. Slightly softer than kText so the display type does not glare.
inline const juce::Colour kHeading{0xffe4e0d8};
/// The brightest text in the design, reserved for the one thing a screen is about.
inline const juce::Colour kBright{0xfff4f1eb};
/// @}

/// @name Performance Mode — read across a stage
/// @{
inline const juce::Colour kStageBg{0xff0a0a0a};
inline const juce::Colour kStageText{0xffedeae4};
inline const juce::Colour kStageDim{0xff6e737b};
inline const juce::Colour kStageOk = kOk;
inline const juce::Colour kStageWarn = kWarn;
inline const juce::Colour kStageFault = kFault;
inline const juce::Colour kStagePanic = kPanicRed;
/// @}

} // namespace ghostband::app
