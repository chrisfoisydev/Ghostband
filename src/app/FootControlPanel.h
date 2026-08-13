// GhostBand — live AI accompaniment for singer-songwriters.
// Copyright 2026 GhostBand contributors. Licensed under Apache-2.0.
//
// macOS-only. Compiled on the target Mac; see KNOWN_ISSUES.md §1.

#pragma once

#include "GhostBandAudioEngine.h"

#include <juce_gui_extra/juce_gui_extra.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace ghostband::app {

/// Foot-controller mapping screen.
///
/// One row per action: what it does, what it is bound to, LEARN, CLEAR. The flow the
/// brief asks for is "pick an action, press the pedal, mapping saved", so LEARN arms and
/// the next press on any open MIDI input binds it.
///
/// The row also lights when its action fires. That is not decoration — "is my pedal
/// actually reaching the app" is the question a performer has at soundcheck, and without
/// feedback the only way to answer it is to trigger the action for real and watch the
/// band change, which is not something to do with an audience in the room.
class FootControlPanel : public juce::Component, private juce::Timer {
public:
    /// @param keyboardState  shared with the setup screen's keyboard, so a key pressed
    ///        here travels the identical path a hardware pedal does. The panel needs its
    ///        own copy because it covers the whole window: without one, the only MIDI
    ///        source on a machine with no pedal is hidden behind the screen that asks you
    ///        to press it.
    FootControlPanel(GhostBandAudioEngine& engine, juce::MidiKeyboardState& keyboardState);
    ~FootControlPanel() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    /// Invoked when the performer closes the panel.
    std::function<void()> onCloseRequested;

private:
    void timerCallback() override;
    void rebuildLabels();

    struct Row {
        core::PerformanceAction action = core::PerformanceAction::None;
        std::unique_ptr<juce::Label> name;
        std::unique_ptr<juce::Label> binding;
        std::unique_ptr<juce::TextButton> learn;
        std::unique_ptr<juce::TextButton> clear;
        /// Millisecond counter at which this row last fired, for the activity flash.
        double lit_at_ms = 0.0;
    };

    GhostBandAudioEngine& engine_;
    juce::MidiKeyboardState& keyboard_state_;
    /// Present so MIDI Learn is usable with no hardware attached. Bound to the same
    /// MidiKeyboardState as the setup screen's keyboard.
    std::unique_ptr<juce::MidiKeyboardComponent> keyboard_;
    juce::Label keyboard_hint_;
    std::vector<Row> rows_;

    juce::TextButton defaults_button_{"RESTORE DEFAULTS"};
    juce::TextButton clear_all_button_{"CLEAR ALL"};
    juce::TextButton close_button_{"DONE"};
    juce::Label heading_;
    juce::Label hint_;
    /// Standing warning about a binding that steals a message harmony needs. Rewritten by
    /// every rebuildLabels().
    juce::Label collision_label_;
    /// Transient "took the switch from X" notice. Kept separate from collision_label_
    /// because rebuildLabels() runs immediately after a theft and would otherwise
    /// overwrite the notice before it could be read.
    juce::Label displaced_label_;

    /// The fire counter this panel has already reacted to. The engine's last-fired value
    /// is sticky, so a sequence number is what distinguishes a new press from the same
    /// old one.
    std::uint32_t last_fired_sequence_ = 0;
    bool was_learning_ = false;
    /// How long a row stays lit after its action fires.
    static constexpr double kFlashMs = 600.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FootControlPanel)
};

} // namespace ghostband::app
