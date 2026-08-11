// GhostBand — live AI accompaniment for singer-songwriters.
// Copyright 2026 GhostBand contributors. Licensed under Apache-2.0.
//
// ⚠️ macOS-only, NEVER COMPILED as of this commit. See KNOWN_ISSUES.md §1.

#pragma once

#include "FootControlPanel.h"
#include "GhostBandAudioEngine.h"
#include "PerformanceView.h"

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
/// Monospaced multi-line text that can live inside a Viewport.
///
/// Replaces a juce::TextEditor for the diagnostics panel. The panel is rewritten at 10 Hz,
/// and TextEditor::setText resets the scroll position on every rewrite — so the view
/// snapped back to the top ten times a second and could not be scrolled at all. A
/// Viewport keeps its own scroll offset across content changes, which a TextEditor's
/// internal one does not expose.
class DiagnosticsText : public juce::Component {
public:
    void setContent(const juce::String& text, int viewWidth);
    void paint(juce::Graphics& g) override;

private:
    juce::StringArray lines_;
};

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
    juce::TextButton load_song_button_{"LOAD DEMO SONG"};
    /// Enters the stage screen. Disabled until a song exists, because an empty
    /// Performance Mode would be a screen that promises a set it cannot run.
    juce::TextButton performance_button_{"PERFORMANCE MODE"};
    juce::TextButton foot_control_button_{"FOOT CONTROL"};
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
    juce::Viewport diagnostics_viewport_;
    DiagnosticsText diagnostics_text_;

    std::unique_ptr<juce::AudioDeviceSelectorComponent> device_selector_;

    /// Owned but only visible in performance mode. Kept alive rather than rebuilt so
    /// entering the stage screen mid-set cannot allocate or stall.
    std::unique_ptr<PerformanceView> performance_view_;
    bool performance_mode_ = false;
    void setPerformanceMode(bool on);

    /// Overlays the setup screen. Built once for the same reason as PerformanceView:
    /// opening it must not allocate while the band is playing.
    std::unique_ptr<FootControlPanel> foot_control_panel_;
    bool foot_control_mode_ = false;
    void setFootControlMode(bool on);

    /// Show exactly one of setup / Performance Mode / foot control. Centralised because
    /// two independent show-hide passes had already made the setup screen reappear
    /// underneath the mapping panel.
    void applyScreenVisibility();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};

} // namespace ghostband::app
