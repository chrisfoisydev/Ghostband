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

/// @name Semantic — identical on every screen
/// @{
inline const juce::Colour kOk{0xff37c871};
inline const juce::Colour kWarn{0xffe0a020};
inline const juce::Colour kFault{0xffe0453e};
inline const juce::Colour kPanicRed{0xffb3231c};
/// @}

/// @name Setup / editor screens — read at a desk
/// @{
inline const juce::Colour kBackground{0xff0e0f11};
inline const juce::Colour kPanel{0xff17191c};
inline const juce::Colour kText{0xffe8e8e8};
inline const juce::Colour kDim{0xff8a8f96};
/// @}

/// @name Performance Mode — read across a stage
/// @{
inline const juce::Colour kStageBg{0xff08090a};
inline const juce::Colour kStageText{0xfff2f2f2};
inline const juce::Colour kStageDim{0xff70767d};
inline const juce::Colour kStageOk = kOk;
inline const juce::Colour kStageWarn = kWarn;
inline const juce::Colour kStageFault = kFault;
inline const juce::Colour kStagePanic = kPanicRed;
/// @}

} // namespace ghostband::app
