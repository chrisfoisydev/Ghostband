// GhostBand — live AI accompaniment for singer-songwriters.
// Copyright 2026 GhostBand contributors. Licensed under Apache-2.0.
//
// ⚠️ macOS-only, NEVER COMPILED as of this commit. See KNOWN_ISSUES.md §1.

#pragma once

#include "GhostBandAudioEngine.h"

#include <juce_gui_extra/juce_gui_extra.h>

namespace ghostband::app {

/// Phase 0 UI. Deliberately minimal.
///
/// The brief is explicit that the first milestone is "MRT2 generates stable real-time
/// audio inside our standalone application", NOT "the app UI exists" — so this screen is
/// a technical instrument panel, not Performance Mode. Songs, sections, setlists and the
/// large-type stage view arrive in Phase 2, once there is a verified engine to drive them.
///
/// What is here: model status, START/STOP, PANIC, a prompt field, an audio-device
/// selector, and the diagnostics the spike exists to produce.
class MainComponent : public juce::Component,
                      private juce::Timer,
                      private juce::MidiKeyboardState::Listener {
public:
    MainComponent();
    ~MainComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;

private:
    void timerCallback() override;

    // juce::MidiKeyboardState::Listener — on-screen/computer-key notes.
    void handleNoteOn(juce::MidiKeyboardState*, int channel, int note, float velocity) override;
    void handleNoteOff(juce::MidiKeyboardState*, int channel, int note, float velocity) override;
    void refreshStatus();
    void applyPrompt();
    void loadModel();

    /// Draw the GHOSTBAND wordmark: "GHOST" outlined, "BAND" solid, set as one word.
    /// `baseline` is the text baseline, not the top of the glyphs.
    void drawWordmark(juce::Graphics& g, float x, float baseline, float height);

    /// Resolve the default MRT2 install location used by upstream's own examples:
    /// ~/Documents/Magenta/magenta-rt-v2/
    static juce::File defaultResourceDir();
    static juce::File defaultModelPath(const juce::String& modelName);

    GhostBandAudioEngine engine_;

    juce::TextButton load_button_{"LOAD MODEL"};
    juce::TextButton start_button_{"START"};
    juce::TextButton stop_button_{"STOP"};
    juce::TextButton panic_button_{"PANIC"};
    /// Only visible while Health == Degraded. Recovery from a degraded state must be an
    /// explicit operator action (the band must not reappear mid-phrase on its own), so
    /// there has to be a control for it — without one, Degraded is a dead end.
    juce::TextButton recover_button_{"RECOVER AI"};
    juce::ToggleButton ai_band_toggle_{"AI BAND"};

    juce::TextEditor prompt_editor_;
    /// Explicit apply. Relying on Enter alone silently swallowed every prompt edit on the
    /// first real run — the model kept its load-time prompt and nothing said so.
    juce::TextButton apply_prompt_button_{"APPLY PROMPT"};
    juce::Label prompt_status_label_;

    /// Generation buffer in MRT2 frames. The one lever that meaningfully moves control
    /// latency; exposed so it can be traded against underrun margin by ear.
    juce::ComboBox buffer_combo_;
    juce::Label buffer_label_;
    juce::Slider level_slider_;
    juce::Label level_label_;

    /// AI INTENSITY — how much the band plays. Kept visually adjacent to, but clearly
    /// distinct from, AI OUTPUT LEVEL; the brief forbids conflating them.
    juce::Slider intensity_slider_;
    juce::Label intensity_label_;

    /// On-screen keyboard, playable with the mouse or the computer keys (A/W/S/E/D...).
    /// It is a genuine MIDI source, not a simulation: notes go through the same
    /// MidiHarmonyState a hardware controller uses, so testing here exercises the real
    /// path. Present because the harmony feature is otherwise untestable without buying a
    /// controller.
    juce::MidiKeyboardState keyboard_state_;
    std::unique_ptr<juce::MidiKeyboardComponent> keyboard_;
    juce::Label harmony_label_;

    juce::Label status_label_;
    juce::Label warning_label_;
    juce::TextEditor diagnostics_view_;

    std::unique_ptr<juce::AudioDeviceSelectorComponent> device_selector_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};

} // namespace ghostband::app
