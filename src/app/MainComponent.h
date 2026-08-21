// GhostBand — live AI accompaniment for singer-songwriters.
// Copyright 2026 GhostBand contributors. Licensed under Apache-2.0.
//
// ⚠️ macOS-only. Builds and runs on macOS/Apple Silicon; cannot be compiled on
// this Linux development machine at all. See KNOWN_ISSUES.md §1 — which means any change
// here is unverified until someone builds it on the Mac.

#pragma once

#include "FootControlPanel.h"
#include "GhostBandAudioEngine.h"
#include "SetlistView.h"
#include "SongEditorView.h"
#include "PerformanceView.h"
#include "StageHeader.h"

#include <juce_gui_extra/juce_gui_extra.h>

#include <array>
#include <functional>
#include <utility>
#include <vector>

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

/// The scrollable body of the setup screen.
///
/// Exists because the design's SETUP screen is a tall single column that scrolls
/// (`overflow-y: auto` in the canvas), and JUCE needs a real component inside a Viewport to
/// scroll. Every setup control is a child of this rather than of MainComponent; the
/// overlays (stage, pedal, editor) are not, because they own the whole window.
///
/// Painting is delegated back to MainComponent through `onPaint` so the drawing code stays
/// next to the state it draws, instead of this class needing a reference to the engine.
class SetupContent : public juce::Component {
public:
    std::function<void(juce::Graphics&)> onPaint;
    void paint(juce::Graphics& g) override {
        if (onPaint) onPaint(g);
    }
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
    /// The id currently chosen in `model_combo_`, e.g. `"mrt2_small"`.
    juce::String selectedModelId() const;
    /// Verdict for the current choice on this machine.
    core::RealtimeVerdict selectedModelVerdict() const;
    /// Rebuild the combo's item text and enabled state from what is on disk.
    void refreshModelChoices();

    /// One line of the YOUR RIG list — the design's `grid-template-columns: 220px 1fr auto`
    /// row: a display-type heading, a quiet value, and a tracked status beside a lamp.
    ///
    /// This replaces the old single status label, which packed model, backend and PANIC
    /// state into one run-on sentence ("NO MODEL - NO BAND (no model loaded)"). Five
    /// labelled rows say the same things separably, which is what you want at soundcheck.
    struct RigRow {
        juce::String label;
        juce::String value;
        juce::String status;
        juce::Colour colour = kDim;
    };

    /// Recomputed on every refresh. Fixed size so a 10 Hz refresh does not churn the heap
    /// on the message thread; the strings themselves still allocate, which is fine here and
    /// would not be in the audio callback.
    std::array<RigRow, 5> rig_rows_{};
    /// Where the rig list was laid out, so paint() and resized() cannot disagree about it.
    juce::Rectangle<int> rig_area_;
    juce::Rectangle<int> title_area_;
    /// Section kickers — the canvas's tiny 0.22em caps above each block. Drawn rather than
    /// made into Labels because they are typography, not controls, and a Label per kicker
    /// would be five more components to keep in sync with the layout.
    std::vector<std::pair<juce::String, juce::Rectangle<int>>> kickers_;

    /// Refresh the rig list from engine state. Returns true if anything changed, so the
    /// 10 Hz timer does not repaint a tall scrolling component that has not moved.
    bool updateRigRows();

    /// When the running binary was linked, read from the executable's own timestamp.
    ///
    /// On screen because "it looks exactly the same" has two causes that need opposite
    /// responses - the change did not land, or the change did not run - and three rounds
    /// were spent not knowing which. `moreThanOneInstanceAllowed()` is false, so `open`
    /// silently focuses a stale instance (KNOWN_ISSUES.md §20), and a failed build leaves
    /// the previous binary in place and runnable. The executable's mtime cannot lie about
    /// either. Stage hardware shows its firmware version for the same reason.
    juce::String build_stamp_;

    /// Whether the warning banner currently has text. The banner's row collapses to zero
    /// height when empty — leaving a 36px hole under the transport row for the ~99% of the
    /// time nothing is wrong put a visible gap in the middle of the screen. Tracked so that
    /// a warning appearing or clearing triggers a re-layout; refreshStatus() rewrites the
    /// text at 10 Hz but resized() only runs when something asks it to.
    bool warning_shown_ = false;

    void paintSetup(juce::Graphics& g);
    /// Give a button the canvas's inverted or destructive treatment. See StageChrome.h.
    static void makePrimary(juce::TextButton& button);
    static void makeDanger(juce::TextButton& button);

    void saveCurrentSong();
    void openSongFile();
    void openSetlistFile();
    /// Report a load/save outcome in the warning line. Empty text clears it.
    void showFileMessage(const juce::String& message, bool isError);

    /// Resolve the default MRT2 install location used by upstream's own examples:
    /// ~/Documents/Magenta/magenta-rt-v2/
    static juce::File defaultResourceDir();
    static juce::File defaultModelPath(const juce::String& modelName);

    GhostBandAudioEngine engine_;

    /// Persistent chrome. A direct child of MainComponent, so it stays put while the setup
    /// body scrolls beneath it.
    StageHeader header_;
    juce::Viewport setup_viewport_;
    SetupContent setup_content_;

    /// Which model to load. Populated from `core::knownModels()`; entries that are not
    /// installed on disk are listed but disabled, because "you do not have this yet" and
    /// "this does not exist" are different facts and hiding the second one makes the first
    /// look like a missing feature.
    juce::ComboBox model_combo_;
    juce::Label model_label_;
    /// The consequence of the current choice on *this* machine, or empty when there is
    /// nothing to warn about.
    juce::Label model_note_;
    /// A model upstream marks as not real-time here needs a second, deliberate press —
    /// same shape as the editor's DISCARD CHANGES? confirmation, and for the same reason:
    /// one modal-free barrier in front of a decision that ruins a gig.
    bool confirming_slow_model_ = false;

    juce::TextButton load_button_{"LOAD MODEL"};
    juce::TextButton load_song_button_{"LOAD DEMO SONG"};
    /// Enters the stage screen. Disabled until a song exists, because an empty
    /// Performance Mode would be a screen that promises a set it cannot run.
    juce::TextButton performance_button_{"PERFORMANCE MODE"};
    juce::TextButton foot_control_button_{"FOOT CONTROL"};
    juce::TextButton save_song_button_{"SAVE SONG"};
    juce::TextButton open_song_button_{"OPEN SONG"};
    juce::TextButton open_setlist_button_{"OPEN SETLIST"};
    juce::TextButton edit_song_button_{"EDIT SONG"};
    /// Owned because a FileChooser must outlive the async callback that uses it.
    std::unique_ptr<juce::FileChooser> file_chooser_;
    juce::TextButton start_button_{"START"};
    juce::TextButton stop_button_{"STOP"};
    juce::TextButton panic_button_{"PANIC"};
    /// Only visible while Health == Degraded. Recovery from a degraded state must be an
    /// explicit operator action (the band must not reappear mid-phrase on its own), so
    /// there has to be a control for it — without one, Degraded is a dead end.
    juce::TextButton recover_button_{"RECOVER BAND"};
    juce::ToggleButton ai_band_toggle_{"BAND"};

    juce::TextEditor prompt_editor_;
    /// Explicit apply. Relying on Enter alone silently swallowed every prompt edit on the
    /// first real run — the model kept its load-time prompt and nothing said so.
    juce::TextButton apply_prompt_button_{"APPLY PROMPT"};
    juce::Label prompt_status_label_;

    /// FOLLOW RESPONSE — the generation buffer in MRT2 frames, named for what the
    /// performer experiences rather than the mechanism. The one lever that meaningfully
    /// moves control latency; exposed so it can be traded against underrun margin by ear.
    juce::ComboBox buffer_combo_;
    juce::Label buffer_label_;
    juce::Slider level_slider_;
    juce::Label level_label_;

    /// BAND INTENSITY — how much the band plays. Kept visually adjacent to, but clearly
    /// distinct from, BAND VOLUME; the brief forbids conflating them.
    juce::Slider intensity_slider_;
    juce::Label intensity_label_;

    /// On-screen keyboard, playable with the mouse or the computer keys (A/W/S/E/D...).
    /// It is a genuine MIDI source, not a simulation: it builds real MIDI messages and
    /// hands them to GhostBandAudioEngine::handleMidiMessage, the same entry point a
    /// hardware controller uses. So it exercises foot-control matching and MIDI Learn as
    /// well as harmony. Present because both features are otherwise untestable without
    /// buying a controller — which means anything new on that path must go through
    /// handleMidiMessage, not around it.
    juce::MidiKeyboardState keyboard_state_;
    std::unique_ptr<juce::MidiKeyboardComponent> keyboard_;
    juce::Label harmony_label_;

    /// No `status_label_` any more. It carried model, backend and PANIC state in one
    /// run-on line; the YOUR RIG rows carry the same facts separably, and PANIC has the
    /// warning banner and the button's own RELEASE PANIC text.
    juce::Label warning_label_;
    /// Outcome of the last save/open. Separate from warning_label_, which is rewritten
    /// from engine state ten times a second and would erase it immediately.
    juce::Label file_status_label_;
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

    /// The authoring screen. Also an overlay, and also built once — opening it must not
    /// allocate while the band is playing.
    std::unique_ptr<SongEditorView> song_editor_view_;
    bool song_editor_mode_ = false;
    void setSongEditorMode(bool on);

    /// Building a set. Same ownership rules as the other overlays.
    std::unique_ptr<SetlistView> setlist_view_;
    bool setlist_mode_ = false;
    void setSetlistMode(bool on);

    /// Show exactly one of setup / Performance Mode / foot control / song editor.
    /// Centralised because two independent show-hide passes had already made the setup
    /// screen reappear underneath the mapping panel.
    void applyScreenVisibility();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};

} // namespace ghostband::app
