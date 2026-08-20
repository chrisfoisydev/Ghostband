// GhostBand — live AI accompaniment for singer-songwriters.
// Copyright 2026 GhostBand contributors. Licensed under Apache-2.0.
//
// ⚠️ macOS-only. Builds and runs on macOS/Apple Silicon; cannot be compiled on
// this Linux development machine at all. See KNOWN_ISSUES.md §1 — which means any change
// here is unverified until someone builds it on the Mac.

#include "MainComponent.h"

#include "StageChrome.h"
#include "StagePalette.h"
#include "StageType.h"

#include "core/ChordNamer.h"
#include "core/GhostBandConstants.h"
#include "core/Logging.h"

#include <utility>   // std::pair
#include <vector>

namespace ghostband::app {

namespace {

// UI string literals are kept strictly ASCII. Non-ASCII characters (em dashes, middots)
// arrive at the screen as mojibake — "PANIC — RELEASE" rendered as "PANIC â€ RELEASE" on
// a real run, because JUCE reinterpreted the UTF-8 bytes. Anything a performer might read
// mid-set has to be legible with certainty, so typography is not worth the risk here.
// If a non-ASCII glyph is genuinely needed later, wrap it: juce::CharPointer_UTF8("...").

// Palette lives in StagePalette.h so the semantic colours cannot drift between views.

juce::String promptStatusText(core::PromptStatus s) {
    switch (s) {
        case core::PromptStatus::Idle:     return "idle";
        case core::PromptStatus::Encoding: return "encoding...";
        case core::PromptStatus::Ready:    return "ready";
        case core::PromptStatus::Error:    return "ERROR";
    }
    return "?";
}

} // namespace

namespace {
/// "3 of 7 mapped" rather than a bare yes/no: at soundcheck the useful question is how
/// much of the pedal is live, not whether any of it is.
juce::String mappingSummary(int mapped, int total) {
    if (mapped == 0) return "nothing mapped";
    return juce::String(mapped) + " of " + juce::String(total) + " actions mapped";
}

/// Position in the set, and any gaps in it. A missing song is stated here rather than only
/// at load time, because the diagnostics panel is what gets read at soundcheck.
juce::String setlistSummary(const GhostBandAudioEngine& engine) {
    if (!engine.hasSetlist()) return "none";

    const auto& s = engine.setlist();
    juce::String text = juce::String(s.setlist().name) + "   song "
                      + juce::String(s.currentIndex() + 1) + " of "
                      + juce::String(s.size());
    if (s.hasMissingSongs()) {
        text += "   " + juce::String(s.missingCount()) + " MISSING";
    }
    return text;
}
} // namespace

namespace {
constexpr int kDiagLineHeight = 17;
constexpr float kDiagFontHeight = 13.0f;

/// Lay out controls left to right, wrapping to a new row when the next will not fit.
///
/// Hand-tuned pixel rows silently swallowed the AI BAND toggle: a later feature added
/// buttons to a row that was already full, and `removeFromLeft` on an exhausted rectangle
/// returns a zero-width rectangle rather than complaining. The control stayed enabled and
/// "visible" and was simply never drawn — the performer lost the band on/off switch with
/// nothing on screen to suggest anything was wrong.
///
/// Wrapping makes that impossible: a control that does not fit moves down instead of
/// disappearing. `area` is advanced past every row used.
/// `rightItems` are placed against the right edge in the order given, before the left-hand
/// flow starts, so a control that must never be crowded (PANIC) keeps its position however
/// many buttons appear beside it.
///
/// Takes `area` rather than a pre-extracted row: wrapping has to be able to claim the next
/// row from somewhere, and a row rectangle has no height left to give.
void layoutRow(juce::Rectangle<int>& area, int rowHeight,
               const std::vector<std::pair<juce::Component*, int>>& leftItems,
               const std::vector<std::pair<juce::Component*, int>>& rightItems = {}) {
    auto row = area.removeFromTop(rowHeight);

    for (const auto& [component, width] : rightItems) {
        if (component != nullptr) component->setBounds(row.removeFromRight(width).reduced(2));
    }
    for (const auto& [component, width] : leftItems) {
        if (component == nullptr) continue;
        if (row.getWidth() < width) {
            area.removeFromTop(4);
            row = area.removeFromTop(rowHeight);
        }
        component->setBounds(row.removeFromLeft(width).reduced(2));
    }
}
} // namespace

void DiagnosticsText::setContent(const juce::String& text, int viewWidth) {
    juce::StringArray next;
    next.addLines(text);
    if (next == lines_ && getWidth() == viewWidth) return; // no repaint if nothing moved

    lines_ = std::move(next);
    setSize(juce::jmax(viewWidth, 10), juce::jmax(lines_.size() * kDiagLineHeight, 10));
    repaint();
}

void DiagnosticsText::paint(juce::Graphics& g) {
    g.fillAll(kPanel);
    g.setColour(kDim);
    g.setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(),
                                kDiagFontHeight, juce::Font::plain));

    for (int i = 0; i < lines_.size(); ++i) {
        g.drawSingleLineText(lines_[i], 8, (i + 1) * kDiagLineHeight - 4);
    }
}

void MainComponent::makePrimary(juce::TextButton& button) {
    // Inverted: light fill, dark text. The canvas allows one per screen — the thing you
    // came to the screen to do. GhostBandLookAndFeel reads the property when it draws.
    button.getProperties().set(prop::kPrimary, true);
    button.setColour(juce::TextButton::textColourOffId, kBackground);
    button.setColour(juce::TextButton::textColourOnId, kBackground);
}

void MainComponent::makeDanger(juce::TextButton& button) {
    button.getProperties().set(prop::kDanger, true);
    button.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    button.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
}

MainComponent::MainComponent() {
    const juce::String audio_error = engine_.initialise();

    // Numeric month, not "%d %b": strftime's abbreviated month is locale-dependent and can
    // be non-ASCII, which this UI renders as mojibake (see the note at the top of the file).
    build_stamp_ = "BUILD "
                 + juce::File::getSpecialLocation(juce::File::currentExecutableFile)
                       .getLastModificationTime()
                       .formatted("%Y-%m-%d %H:%M");

    // Persistent chrome first. The nav is the design's shell; everything else hangs inside
    // it. HOME and SETLISTS are deliberately absent — see StageHeader.h for why.
    addAndMakeVisible(header_);
    header_.onNavigate = [this](StageHeader::Screen screen) {
        switch (screen) {
            case StageHeader::Screen::Setup:
                setPerformanceMode(false);
                setFootControlMode(false);
                setSongEditorMode(false);
                setSetlistMode(false);
                break;
            case StageHeader::Screen::Songs:    setSongEditorMode(true); break;
            case StageHeader::Screen::Setlists: setSetlistMode(true); break;
            case StageHeader::Screen::Pedal:    setFootControlMode(true); break;
            case StageHeader::Screen::Perform:  setPerformanceMode(true); break;
        }
    };

    addAndMakeVisible(setup_viewport_);
    setup_viewport_.setViewedComponent(&setup_content_, false);
    setup_viewport_.setScrollBarsShown(true, false);
    // No background colour is set on the Viewport: juce::Viewport declares no ColourIds and
    // paints nothing of its own, so MainComponent's fillAll shows through any gap between
    // the content's height and the viewport's.
    setup_content_.onPaint = [this](juce::Graphics& g) { paintSetup(g); };

    setup_content_.addAndMakeVisible(load_button_);
    load_button_.onClick = [this] { loadModel(); };

    setup_content_.addAndMakeVisible(load_song_button_);
    load_song_button_.onClick = [this] {
        engine_.loadDemoSong();
        refreshStatus();
    };

    setup_content_.addAndMakeVisible(performance_button_);
    makePrimary(performance_button_);
    performance_button_.onClick = [this] { setPerformanceMode(true); };

    setup_content_.addAndMakeVisible(foot_control_button_);
    foot_control_button_.onClick = [this] { setFootControlMode(true); };

    setup_content_.addAndMakeVisible(edit_song_button_);
    edit_song_button_.onClick = [this] { setSongEditorMode(true); };

    setup_content_.addAndMakeVisible(save_song_button_);
    save_song_button_.onClick = [this] { saveCurrentSong(); };

    setup_content_.addAndMakeVisible(open_song_button_);
    open_song_button_.onClick = [this] { openSongFile(); };

    setup_content_.addAndMakeVisible(open_setlist_button_);
    open_setlist_button_.onClick = [this] { openSetlistFile(); };

    setup_content_.addAndMakeVisible(file_status_label_);
    file_status_label_.setColour(juce::Label::textColourId, kDim);
    file_status_label_.setFont(labelFont(11.0f));

    setup_content_.addAndMakeVisible(start_button_);
    start_button_.onClick = [this] { engine_.startGeneration(); refreshStatus(); };

    setup_content_.addAndMakeVisible(stop_button_);
    stop_button_.onClick = [this] { engine_.stopGeneration(); refreshStatus(); };

    setup_content_.addAndMakeVisible(panic_button_);
    makeDanger(panic_button_);
    panic_button_.onClick = [this] {
        // Toggle: a second press releases, so PANIC is recoverable without a menu.
        if (engine_.isPanicked()) engine_.clearPanic();
        else engine_.panic();
        refreshStatus();
    };

    setup_content_.addChildComponent(recover_button_);  // shown only while Degraded
    recover_button_.setColour(juce::TextButton::buttonColourId, kWarn);
    recover_button_.setColour(juce::TextButton::textColourOffId, juce::Colours::black);
    recover_button_.onClick = [this] {
        // One button, two conditions, in priority order. A device that has come back is
        // the more specific situation and is what the button says when both are true.
        if (!engine_.acknowledgeDeviceRestored()) engine_.recoverFromDegraded();
        refreshStatus();
    };

    setup_content_.addAndMakeVisible(ai_band_toggle_);
    ai_band_toggle_.setColour(juce::ToggleButton::textColourId, kText);
    ai_band_toggle_.onClick = [this] { engine_.setAiBandOn(ai_band_toggle_.getToggleState()); };

    setup_content_.addAndMakeVisible(prompt_editor_);
    prompt_editor_.setMultiLine(true);
    prompt_editor_.setReturnKeyStartsNewLine(false);
    prompt_editor_.setText("warm organic indie folk ensemble, piano, bass and restrained "
                           "percussion, instrumental");
    // Three ways to apply, because one was not enough: Enter, focus loss, and the
    // button. The first real run showed every prompt edit being silently discarded.
    prompt_editor_.onReturnKey = [this] { applyPrompt(); };
    prompt_editor_.onFocusLost = [this] { applyPrompt(); };

    setup_content_.addAndMakeVisible(apply_prompt_button_);
    apply_prompt_button_.onClick = [this] { applyPrompt(); };

    setup_content_.addAndMakeVisible(prompt_status_label_);
    prompt_status_label_.setColour(juce::Label::textColourId, kDim);
    prompt_status_label_.setFont(labelFont(10.0f));

    setup_content_.addAndMakeVisible(buffer_label_);
    buffer_label_.setText("FOLLOW RESPONSE", juce::dontSendNotification);
    buffer_label_.setColour(juce::Label::textColourId, kKicker);
    buffer_label_.setFont(labelFont(11.0f));

    setup_content_.addAndMakeVisible(buffer_combo_);
    // The design names this FOLLOW RESPONSE, which is what the performer actually
    // experiences; "generation buffer" is the mechanism. Fast responds sooner and
    // leaves less margin against a late inference frame.
    buffer_combo_.addItem("Fast - 40 ms, least margin", 1);
    buffer_combo_.addItem("Balanced - 80 ms (default)", 2);
    buffer_combo_.addItem("Stable - 120 ms, most margin", 3);
    buffer_combo_.setSelectedId(2, juce::dontSendNotification);
    buffer_combo_.onChange = [this] {
        engine_.setGenerationBufferFrames(buffer_combo_.getSelectedId());
    };

    setup_content_.addAndMakeVisible(level_label_);
    level_label_.setText("BAND VOLUME", juce::dontSendNotification);
    level_label_.setColour(juce::Label::textColourId, kKicker);
    level_label_.setFont(labelFont(11.0f));

    setup_content_.addAndMakeVisible(level_slider_);
    level_slider_.setRange(-60.0, 6.0, 0.1);
    level_slider_.setValue(0.0, juce::dontSendNotification);
    level_slider_.setTextValueSuffix(" dB");
    level_slider_.onValueChange = [this] {
        engine_.setOutputLevelDb(static_cast<float>(level_slider_.getValue()));
    };

    setup_content_.addAndMakeVisible(intensity_label_);
    intensity_label_.setText("BAND INTENSITY", juce::dontSendNotification);
    intensity_label_.setColour(juce::Label::textColourId, kKicker);
    intensity_label_.setFont(labelFont(11.0f));

    setup_content_.addAndMakeVisible(intensity_slider_);
    intensity_slider_.setRange(0.0, 100.0, 1.0);
    intensity_slider_.setValue(50.0, juce::dontSendNotification);
    intensity_slider_.setTextValueSuffix(" %");
    intensity_slider_.onValueChange = [this] {
        engine_.setAiIntensity(static_cast<float>(intensity_slider_.getValue() / 100.0));
    };

    keyboard_ = std::make_unique<juce::MidiKeyboardComponent>(
        keyboard_state_, juce::MidiKeyboardComponent::horizontalKeyboard);
    keyboard_->setAvailableRange(36, 84);          // C2..C6, enough for chord work
    keyboard_->setKeyPressBaseOctave(4);           // computer keys start at C4
    keyboard_->setLowestVisibleKey(48);
    setup_content_.addAndMakeVisible(*keyboard_);
    keyboard_state_.addListener(this);

    setup_content_.addAndMakeVisible(harmony_label_);
    harmony_label_.setColour(juce::Label::textColourId, kText);
    harmony_label_.setFont(valueFont(16.0f));

    setup_content_.addAndMakeVisible(warning_label_);
    warning_label_.setColour(juce::Label::textColourId, kWarn);

    setup_content_.addAndMakeVisible(diagnostics_viewport_);
    diagnostics_viewport_.setViewedComponent(&diagnostics_text_, false);
    diagnostics_viewport_.setScrollBarsShown(true, false);

    performance_view_ = std::make_unique<PerformanceView>(engine_);
    performance_view_->onExitRequested = [this] { setPerformanceMode(false); };
    addChildComponent(*performance_view_);   // built now, shown only on demand

    foot_control_panel_ = std::make_unique<FootControlPanel>(engine_, keyboard_state_);
    foot_control_panel_->onCloseRequested = [this] { setFootControlMode(false); };
    addChildComponent(*foot_control_panel_);

    song_editor_view_ = std::make_unique<SongEditorView>(engine_);
    song_editor_view_->onCloseRequested = [this] { setSongEditorMode(false); };
    addChildComponent(*song_editor_view_);

    setlist_view_ = std::make_unique<SetlistView>(engine_);
    setlist_view_->onCloseRequested = [this] { setSetlistMode(false); };
    setlist_view_->onPerformRequested = [this] { setPerformanceMode(true); };
    addChildComponent(*setlist_view_);

    device_selector_ = std::make_unique<juce::AudioDeviceSelectorComponent>(
        engine_.deviceManager(),
        /*minInput*/ 0, /*maxInput*/ 0,   // Phase 0 has no input: GhostBand is additive only
        /*minOutput*/ 2, /*maxOutput*/ 2,
        /*showMidi*/ true,                // Phase 1: MIDI inputs are real, so show them
        /*showMidiOutput*/ false,         // GhostBand never sends MIDI out
        /*showChannelsAsStereoPairs*/ true,
        /*hideAdvanced*/ false);
    setup_content_.addAndMakeVisible(*device_selector_);

    if (audio_error.isNotEmpty()) {
        warning_label_.setText("Audio device error: " + audio_error, juce::dontSendNotification);
    }

    setWantsKeyboardFocus(true);
    startTimerHz(10);
    refreshStatus();

    // setSize() LAST, not first. It triggers resized(), and any child still unbuilt at
    // that moment is skipped by its null guard — permanently, because the size never
    // changes again afterwards. Sizing first left the on-screen keyboard and the audio
    // device selector laid out at zero size and therefore invisible.
    setSize(1060, 940);
}

MainComponent::~MainComponent() {
    keyboard_state_.removeListener(this);
    stopTimer();
}

// The on-screen keyboard is a MIDI source, not a shortcut into harmony. Building a real
// message and handing it to the engine means it takes the identical path a hardware
// controller does, foot-control matching included — which is the only way MIDI Learn can
// be exercised on a machine with no pedal attached.
void MainComponent::handleNoteOn(juce::MidiKeyboardState*, int channel, int note,
                                 float velocity) {
    engine_.handleMidiMessage(juce::MidiMessage::noteOn(
        juce::jlimit(1, 16, channel), note,
        static_cast<juce::uint8>(juce::jlimit(1, 127, juce::roundToInt(velocity * 127.0f)))));
}

void MainComponent::handleNoteOff(juce::MidiKeyboardState*, int channel, int note, float) {
    engine_.handleMidiMessage(
        juce::MidiMessage::noteOff(juce::jlimit(1, 16, channel), note));
}

juce::File MainComponent::defaultResourceDir() {
    return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
        .getChildFile("Magenta/magenta-rt-v2/resources");
}

juce::File MainComponent::defaultModelPath(const juce::String& modelName) {
    return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
        .getChildFile("Magenta/magenta-rt-v2/models")
        .getChildFile(modelName)
        .getChildFile(modelName + ".mlxfn");
}

void MainComponent::loadModel() {
    // Phase 0 loads mrt2_small — the model the brief designates as the live default
    // ("Performance"). Model selection UI arrives in Phase 1.
    const juce::File resources = defaultResourceDir();
    const juce::File model = defaultModelPath("mrt2_small");

    load_button_.setEnabled(false);
    // The MODEL rig row reports progress now; refreshStatus() reads engine state, so a
    // separate "LOADING..." write here would only race it.
    refreshStatus();

    engine_.loadModelAsync(resources, model, [this](bool ok, juce::String error) {
        load_button_.setEnabled(true);
        if (!ok) {
            warning_label_.setText(error, juce::dontSendNotification);
        } else {
            warning_label_.setText({}, juce::dontSendNotification);
            engine_.setTextPrompt(prompt_editor_.getText());
        }
        refreshStatus();
    });
}

bool MainComponent::keyPressed(const juce::KeyPress& key) {
    // PANIC must be reachable without the trackpad. Escape is the least ambiguous key on
    // a stage, and it is the one a panicking human reaches for.
    if (key == juce::KeyPress::escapeKey) {
        engine_.panic();
        refreshStatus();
        return true;
    }
    if (key == juce::KeyPress::spaceKey) {
        if (engine_.engineState() == core::EngineState::Running) engine_.stopGeneration();
        else engine_.startGeneration();
        refreshStatus();
        return true;
    }
    return false;
}

void MainComponent::applyPrompt() {
    engine_.setTextPrompt(prompt_editor_.getText());
    refreshStatus();
}

void MainComponent::showFileMessage(const juce::String& message, bool isError) {
    file_status_label_.setColour(juce::Label::textColourId, isError ? kFault : kOk);
    file_status_label_.setText(message, juce::dontSendNotification);
}

void MainComponent::saveCurrentSong() {
    if (!engine_.hasSong()) {
        showFileMessage("No song loaded - nothing to save.", true);
        return;
    }

    juce::File written;
    const auto error = engine_.saveSongAs(*engine_.performance().song(), written);
    if (error.isNotEmpty()) {
        showFileMessage(error, true);
        return;
    }
    // Naming the file, not just saying "Saved": the performer needs to know where it went
    // the first time, and afterwards it confirms which of several songs was written.
    showFileMessage("Saved " + written.getFileName() + " to "
                        + written.getParentDirectory().getFullPathName(),
                    false);
}

void MainComponent::openSongFile() {
    const auto dir = GhostBandAudioEngine::songsDirectory();
    dir.createDirectory();   // so the chooser opens somewhere real on a fresh install

    file_chooser_ = std::make_unique<juce::FileChooser>(
        "Open a GhostBand song", dir, juce::String("*") + core::kSongFileExtension);

    file_chooser_->launchAsync(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& chooser) {
            const auto file = chooser.getResult();
            if (file == juce::File()) return;      // cancelled

            const auto error = engine_.loadSongFile(file);
            showFileMessage(error.isNotEmpty() ? error
                                               : "Opened " + file.getFileName(),
                            error.isNotEmpty());
            refreshStatus();
        });
}

void MainComponent::openSetlistFile() {
    const auto dir = GhostBandAudioEngine::setlistsDirectory();
    dir.createDirectory();

    file_chooser_ = std::make_unique<juce::FileChooser>(
        "Open a GhostBand setlist", dir, juce::String("*") + core::kSetlistFileExtension);

    file_chooser_->launchAsync(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& chooser) {
            const auto file = chooser.getResult();
            if (file == juce::File()) return;

            const auto error = engine_.loadSetlistFile(file);
            if (error.isNotEmpty()) {
                showFileMessage(error, true);
                refreshStatus();
                return;
            }

            // A partly-loaded set is usable, but the performer has to be told at load
            // time. Finding out between songs is too late.
            const auto missing = engine_.missingSongs();
            if (!missing.empty()) {
                juce::String names;
                for (const auto& title : missing) {
                    if (names.isNotEmpty()) names += ", ";
                    names += juce::String(title);
                }
                showFileMessage(juce::String(static_cast<int>(missing.size()))
                                    + " song(s) MISSING: " + names,
                                true);
            } else {
                showFileMessage("Opened " + file.getFileName() + " - "
                                    + juce::String(engine_.setlist().size()) + " songs",
                                false);
            }
            refreshStatus();
        });
}

void MainComponent::setPerformanceMode(bool on) {
    performance_mode_ = on;
    // Leaving mapping armed behind a screen change would let the next pedal press rebind
    // something nobody is looking at.
    if (on) { foot_control_mode_ = false; song_editor_mode_ = false; setlist_mode_ = false; }
    applyScreenVisibility();
    if (on && performance_view_ != nullptr) performance_view_->grabKeyboardFocus();
}

void MainComponent::setFootControlMode(bool on) {
    foot_control_mode_ = on;
    if (!on) engine_.cancelMidiLearn();
    if (on) { song_editor_mode_ = false; setlist_mode_ = false; }
    applyScreenVisibility();
}

void MainComponent::setSetlistMode(bool on) {
    if (on && setlist_view_ != nullptr) {
        foot_control_mode_ = false;
        song_editor_mode_ = false;
        // Always start from the set that is loaded, never from whatever was left here last
        // time — same reasoning as the Song Editor.
        setlist_view_->beginEditingCurrent();
    }
    setlist_mode_ = on;
    applyScreenVisibility();
}

void MainComponent::setSongEditorMode(bool on) {
    if (on && song_editor_view_ != nullptr) {
        foot_control_mode_ = false;
        setlist_mode_ = false;
        // Always start from what is loaded, rather than whatever was left here last time.
        // Editing a stale copy and saving it would silently overwrite the current song.
        song_editor_view_->beginEditing(engine_.hasSong() ? *engine_.performance().song()
                                                         : core::makeDemoSong());
    }
    song_editor_mode_ = on;
    applyScreenVisibility();
}

void MainComponent::applyScreenVisibility() {
    // Exactly one screen is visible at a time. Everything is hidden rather than destroyed,
    // so switching is instant and nothing is reallocated mid-performance.
    const bool setup_visible = !performance_mode_ && !foot_control_mode_
                            && !song_editor_mode_ && !setlist_mode_;

    // Now that every setup control lives inside setup_content_, this component has exactly
    // five direct children, so visibility is stated rather than swept. The old loop hid
    // "everything that is not an overlay", which would now also hide the header and defeat
    // the persistent shell the design is built around.
    setup_viewport_.setVisible(setup_visible);
    header_.setVisible(!performance_mode_);

    if (performance_view_ != nullptr) performance_view_->setVisible(performance_mode_);
    if (foot_control_panel_ != nullptr) foot_control_panel_->setVisible(foot_control_mode_);
    if (song_editor_view_ != nullptr) song_editor_view_->setVisible(song_editor_mode_);
    if (setlist_view_ != nullptr) setlist_view_->setVisible(setlist_mode_);

    header_.setActiveScreen(performance_mode_    ? StageHeader::Screen::Perform
                            : foot_control_mode_ ? StageHeader::Screen::Pedal
                            : song_editor_mode_  ? StageHeader::Screen::Songs
                            : setlist_mode_      ? StageHeader::Screen::Setlists
                                                 : StageHeader::Screen::Setup);

    resized();
    repaint();
}

void MainComponent::timerCallback() { refreshStatus(); }

void MainComponent::refreshStatus() {
    const auto snap = engine_.diagnostics();
    const auto state = engine_.engineState();
    const auto cl = engine_.controlLatency();

    const auto ip = engine_.intensityParams();

    // Is the typed prompt the one MRT2 is actually using? Showing this removes the
    // "did that take effect?" doubt that hid the silent-discard bug for a whole session.
    const bool prompt_dirty = prompt_editor_.getText() != engine_.appliedPrompt();
    apply_prompt_button_.setEnabled(prompt_dirty);
    prompt_status_label_.setText(
        prompt_dirty ? "NOT APPLIED - press APPLY PROMPT"
                     : "applied  (" + juce::String(promptStatusText(engine_.promptStatus())) + ")",
        juce::dontSendNotification);
    prompt_status_label_.setColour(juce::Label::textColourId, prompt_dirty ? kWarn : kDim);

    // Detected harmony. Display only — MRT2 is steered by the raw notes, never by this
    // label, so a naming miss can never become a wrong chord.
    const auto sounding = engine_.harmony().soundingNotes();
    const juce::String chord_notes(core::noteNames(sounding));
    const juce::String chord_name(core::nameChord(sounding));
    // The hint is part of the readout rather than a separate label: the on-screen
    // keyboard only receives computer keys once it has focus, and that is not guessable.
    harmony_label_.setText(sounding.empty()
                               ? juce::String("Harmony: nothing held   "
                                              "(click the keyboard below, then play A S D F G H J)")
                               : "Harmony: " + chord_notes + "   " + chord_name,
                           juce::dontSendNotification);

    if (updateRigRows()) setup_content_.repaint();

    panic_button_.setButtonText(engine_.isPanicked() ? "RELEASE PANIC" : "PANIC");

    performance_button_.setEnabled(engine_.hasSong());
    start_button_.setEnabled(state == core::EngineState::Ready);
    stop_button_.setEnabled(state == core::EngineState::Running);
    ai_band_toggle_.setToggleState(engine_.isAiBandOn(), juce::dontSendNotification);

    juce::String warning = engine_.sampleRateWarning();
    if (state == core::EngineState::Error) warning = engine_.engineError();
    const bool degraded = engine_.health() == core::Health::Degraded;
    if (degraded) {
        // Same reasoning as the PANIC banner: say what stopped, then say what did not.
        warning = "BAND STOPPED - sustained audio underruns. Your guitar and vocal are "
                  "unaffected. Press RECOVER BAND when stable.";
    }

    // The device outranks everything above it. A 48 kHz notice is not worth reading while
    // there is no output at all, and the engine error that a dropout produces describes a
    // symptom rather than the cause.
    const auto device_health = engine_.deviceHealth();
    if (device_health != core::DeviceHealth::Running) {
        warning = engine_.deviceMessage();
        if (device_health == core::DeviceHealth::Lost && engine_.deviceRetryCount() > 0) {
            // So a reconnect that is taking a while reads as work in progress rather than
            // as a frozen app.
            warning += "  (attempt " + juce::String(engine_.deviceRetryCount()) + ")";
        }
    }

    const bool restored = device_health == core::DeviceHealth::Restored;
    recover_button_.setVisible(degraded || restored);
    recover_button_.setButtonText(restored ? "RESUME BAND" : "RECOVER BAND");
    warning_label_.setText(warning, juce::dontSendNotification);

    // The banner's row has no height when there is nothing to say, so its appearance and
    // disappearance both change the layout below it.
    if (warning.isNotEmpty() != warning_shown_) {
        warning_shown_ = warning.isNotEmpty();
        resized();
    }

    // The numbers this spike exists to produce. Anything unmeasured says so explicitly
    // rather than showing a plausible-looking zero.
    juce::String d;
    d << "Build                 " << build_stamp_.fromFirstOccurrenceOf(" ", false, false)
                                  << "\n"
      << "Model                 " << snap.modelName << "\n"
      << "State                 " << toString(state) << "\n"
      << "Streaming             " << (state == core::EngineState::Running ? "Active" : "Stopped") << "\n"
      << "Prompt                " << promptStatusText(engine_.promptStatus()) << "\n"
      << "\n"
      << "Sample rate           " << juce::String(snap.sampleRate, 0) << " Hz\n"
      << "Audio buffer          " << juce::String((int)snap.blockSize) << " samples\n"
      << "Generation buffer     " << juce::String((int)snap.generationBufferAvailable) << " / "
                                  << juce::String((int)snap.generationBufferCapacity) << " samples\n"
      << "PANIC latency         " << juce::String(engine_.outputStage().panicLatencyMs(), 1) << " ms\n"
      << "\n"
      << "Control latency       " << juce::String(cl.typicalMs, 1) << " ms typical, "
                                  << juce::String(cl.worstCaseMs, 1) << " ms worst\n"
      << "  frame quantisation  0-" << juce::String(cl.frameQuantisationMaxMs, 1) << " ms\n"
      << "  generation buffer   " << juce::String(cl.generationBufferMs, 1) << " ms\n"
      << "  device buffer       " << juce::String(cl.deviceBufferMs, 1) << " ms\n"
      << "  limiter lookahead   " << juce::String(cl.limiterLookaheadMs, 1) << " ms\n"
      << "  + MRT2 response     ~200 ms (upstream, not measured here)\n"
      << "\n"
      << "Generation frame      " << juce::String(snap.generationTotalMs, 2) << " ms  (budget 40.00 ms)\n"
      << "  transformer         " << juce::String(snap.generationTransformerMs, 2) << " ms\n"
      << "  headroom            " << juce::String(snap.generationHeadroomRatio() * 100.0f, 1) << " % of budget\n"
      << "\n"
      << "Audio underruns       " << juce::String((juce::int64)snap.audioUnderruns)
                                  << "   (while generating)\n"
      << "  absorbed by priming " << juce::String((juce::int64)engine_.outputStage()
                                     .safetyMonitor().primingUnderruns()) << "\n"
      << "Dropped frames (MRT2) " << juce::String((juce::int64)snap.droppedFrames) << "\n"
      << "Blocks processed      " << juce::String((juce::int64)snap.blocksProcessed) << "\n"
      << "Health                " << toString(engine_.health()) << "\n"
      << "\n"
      << "Output peak           " << juce::String(engine_.outputStage().peakDb(), 1) << " dBFS\n"
      << "Output RMS            " << juce::String(engine_.outputStage().rmsDb(), 1) << " dBFS\n"
      << "Gain reduction        " << juce::String(snap.gainReductionDb, 1) << " dB\n"
      << "CPU (audio)           " << juce::String(snap.audioCpuLoad * 100.0f, 1) << " %\n"
      << "\n"
      << "MIDI                  " << (engine_.anyMidiDeviceConnected()
                                        ? engine_.midiInputNames().joinIntoString(", ")
                                        : juce::String("no device (use the on-screen keyboard)")) << "\n"
      << "Setlist               " << setlistSummary(engine_) << "\n"
      << "Song                  " << (engine_.hasSong()
                                        ? juce::String(engine_.performance().song()->title)
                                        : juce::String("none")) << "\n"
      << "Section               " << (engine_.performance().sections().current() != nullptr
                                        ? juce::String(engine_.performance().sections().current()->name)
                                        : juce::String("-"))
                                  << (engine_.performance().sections().isTransitioning()
                                        ? "  (transitioning)" : "") << "\n"
      << "  changes needing encode " << juce::String((juce::int64)engine_.performance()
                                       .encodedChangeCount()) << "\n"
      << "\n"
      << "Band intensity        " << juce::String(engine_.aiIntensityPercent()) << " %\n"
      << "  drums               " << (ip.drumless ? "removed (drumless)"
                                        : juce::String("cfg ") + juce::String(ip.cfgDrums, 2)) << "\n"
      << "  style guidance      " << juce::String(ip.cfgMusicCoca, 2) << "\n"
      << "  temperature         " << juce::String(ip.temperature, 2) << "\n"
      << "  prompt blend        sparse " << juce::String(ip.promptWeights[0], 2)
                                  << " / base " << juce::String(ip.promptWeights[1], 2)
                                  << " / full " << juce::String(ip.promptWeights[2], 2) << "\n"
      << "\n"
      << "Sounding notes        " << juce::String(engine_.harmony().soundingCount())
                                  << "   " << juce::String(chord_notes) << "\n"
      << "\n"
      << "Foot control          "
                                  << mappingSummary(engine_.mappedActionCount(),
                                                    static_cast<int>(
                                                        core::allPerformanceActions().size()))
                                  << "\n"
      << "  last action fired   " << juce::String(core::toDisplayString(
                                       engine_.lastFiredAction().action)) << "\n"
      << "Memory                " << juce::String(snap.memoryUsageGb, 2) << " GB\n";

    // The Viewport owns the scroll offset, so rewriting the content at 10 Hz no longer
    // fights the user. setContent() also skips the repaint entirely when nothing changed.
    diagnostics_text_.setContent(d, diagnostics_viewport_.getMaximumVisibleWidth());
}

namespace {
// Canvas measurements for the setup column. Named rather than inlined so the layout can be
// checked against the design instead of eyeballed: the canvas pads its screens
// `clamp(24px, 5vh, 56px) clamp(24px, 3.4vw, 48px)` and caps the reading column at 880px.
constexpr int kContentPadX = 48;
constexpr int kContentPadTop = 40;
constexpr int kContentPadBottom = 56;
constexpr int kColumnMax = 880;
constexpr int kRigRowHeight = 56;
constexpr int kRigLabelWidth = 200;
constexpr int kRigStatusWidth = 150;
constexpr int kKickerHeight = 16;
/// Row label column for the slider/combo rows, matching the rig list's 200px so the two
/// blocks share a left edge for their values.
constexpr int kRowLabelWidth = 200;
} // namespace

void MainComponent::paint(juce::Graphics& g) {
    // The header and the setup viewport paint themselves. This is only the ground behind
    // them, visible for a frame during a resize and behind the scrollbar gutter.
    g.fillAll(kBackground);
}

void MainComponent::paintSetup(juce::Graphics& g) {
    g.fillAll(kBackground);

    // Screen title. The canvas sets this in Anton at `clamp(36px, 6.4vh, 64px)` — display
    // type this large above a hairline list is most of what makes the screen read as the
    // design rather than as a control panel.
    g.setColour(kBright);
    g.setFont(displayFont(static_cast<float>(title_area_.getHeight())));
    g.drawText("YOUR RIG", title_area_, juce::Justification::centredLeft);

    // Right-aligned on the title's baseline row, in the quietest colour on the screen.
    drawTrackedCaps(g, build_stamp_, title_area_, kKicker, 10.0f, 0.18f,
                    juce::Justification::centredRight);

    for (const auto& [text, area] : kickers_) {
        drawTrackedCaps(g, text, area, kKicker, 11.0f, 0.22f);
    }

    // The rig list: a rule above the first row, then one hairline closing each row.
    g.setColour(kHairline);
    g.fillRect(rig_area_.getX(), rig_area_.getY(), rig_area_.getWidth(), 1);

    auto row = rig_area_.withHeight(kRigRowHeight);
    for (const auto& r : rig_rows_) {
        drawHairline(g, row);

        auto cells = row;
        const auto label_cell = cells.removeFromLeft(kRigLabelWidth);
        const auto status_cell = cells.removeFromRight(kRigStatusWidth);

        g.setColour(kHeading);
        g.setFont(displayFont(21.0f, 0.06f));
        g.drawText(r.label, label_cell, juce::Justification::centredLeft);

        g.setColour(kValue);
        g.setFont(valueFont(14.0f));
        g.drawText(r.value, cells, juce::Justification::centredLeft);

        // Lamp then word, both in the row's colour: the block carries the state at a
        // glance from across the room, the word confirms it up close.
        drawStatusBlock(g, {status_cell.getX() + 4, row.getCentreY()}, r.colour);
        drawTrackedCaps(g, r.status, status_cell.withTrimmedLeft(18), r.colour, 11.0f, 0.16f);

        row.translate(0, kRigRowHeight);
    }
}

bool MainComponent::updateRigRows() {
    const auto snap = engine_.diagnostics();
    const auto state = engine_.engineState();
    const bool panicked = engine_.isPanicked();

    std::array<RigRow, 5> next{};

    // MODEL. `hasRealBackend()` is the honest question: a model name with no generating
    // backend behind it would be exactly the kind of "looks live, does nothing" readout
    // CLAUDE.md rule 2 exists to prevent.
    if (state == core::EngineState::Loading) {
        next[0] = {"MODEL", "loading...", "LOADING", kWarn};
    } else if (engine_.hasRealBackend()) {
        next[0] = {"MODEL", snap.modelName, "LOADED", kOk};
    } else {
        next[0] = {"MODEL", "no model loaded", "NONE", kKicker};
    }

    // BAND — what the accompaniment is doing right now, PANIC included.
    if (panicked) {
        next[1] = {"BAND", "silenced by PANIC", "PANIC", kPanicRed};
    } else if (engine_.health() == core::Health::Degraded) {
        next[1] = {"BAND", "stopped after sustained underruns", "DEGRADED", kFault};
    } else if (!engine_.isAiBandOn()) {
        next[1] = {"BAND", "switched off", "OFF", kKicker};
    } else {
        const bool running = state == core::EngineState::Running;
        next[1] = {"BAND", juce::String(toDisplayString(state)),
                   running ? "PLAYING" : "READY", running ? kOk : kDim};
    }

    // AUDIO — the device first, then the two numbers that decide whether the engine can
    // keep up. A row reading "48000 Hz  512 samples / OK" while the interface is unplugged
    // would be true about the last device and useless about this one.
    switch (engine_.deviceHealth()) {
        case core::DeviceHealth::Lost:
            next[2] = {"AUDIO", "device disconnected - reconnecting",
                       juce::String(core::toDisplayString(core::DeviceHealth::Lost)), kFault};
            break;
        case core::DeviceHealth::Restored:
            next[2] = {"AUDIO", "device back - band waiting to resume",
                       juce::String(core::toDisplayString(core::DeviceHealth::Restored)), kWarn};
            break;
        case core::DeviceHealth::Running:
            next[2] = {"AUDIO",
                       juce::String(snap.sampleRate, 0) + " Hz   "
                           + juce::String(static_cast<int>(snap.blockSize)) + " samples",
                       engine_.sampleRateWarning().isEmpty() ? "OK" : "CHECK",
                       engine_.sampleRateWarning().isEmpty() ? kOk : kWarn};
            break;
    }

    // MIDI — the on-screen keyboard is a genuine MIDI source, so "none" would be wrong.
    if (engine_.anyMidiDeviceConnected()) {
        next[3] = {"MIDI", engine_.midiInputNames().joinIntoString(", "), "CONNECTED", kOk};
    } else {
        next[3] = {"MIDI", "on-screen keyboard only", "NO DEVICE", kKicker};
    }

    // PEDAL — how much of the foot controller is live, not merely whether any of it is.
    const int mapped = engine_.mappedActionCount();
    const int total = static_cast<int>(core::allPerformanceActions().size());
    next[4] = {"PEDAL", mappingSummary(mapped, total),
               mapped == 0 ? "UNMAPPED" : juce::String(mapped) + " OF " + juce::String(total),
               mapped == 0 ? kKicker : kOk};

    bool changed = false;
    for (std::size_t i = 0; i < next.size(); ++i) {
        if (next[i].label == rig_rows_[i].label && next[i].value == rig_rows_[i].value
            && next[i].status == rig_rows_[i].status && next[i].colour == rig_rows_[i].colour) {
            continue;
        }
        changed = true;
        break;
    }
    if (changed) rig_rows_ = next;

    // The header lamps mirror the rows they summarise, so the bar and the list can never
    // disagree about the same fact.
    header_.setLampColours(next[2].colour, next[1].colour, next[4].colour);
    header_.setScreenEnabled(StageHeader::Screen::Perform, engine_.hasSong());

    return changed;
}

void MainComponent::resized() {
    auto area = getLocalBounds();

    // Performance Mode takes the whole window: it is read from across a stage, and the nav
    // bar is not something anyone touches mid-song. The pedal and editor screens keep the
    // bar, so the shell is persistent everywhere it can honestly be.
    header_.setVisible(!performance_mode_);
    if (!performance_mode_) area.removeFromTop(StageHeader::kHeight);
    header_.setBounds(getLocalBounds().removeFromTop(StageHeader::kHeight));

    if (performance_view_ != nullptr)   performance_view_->setBounds(getLocalBounds());
    if (foot_control_panel_ != nullptr) foot_control_panel_->setBounds(area);
    if (song_editor_view_ != nullptr)   song_editor_view_->setBounds(area);
    if (setlist_view_ != nullptr)       setlist_view_->setBounds(area);

    setup_viewport_.setBounds(area);
    if (performance_mode_ || foot_control_mode_ || song_editor_mode_ || setlist_mode_) return;

    // --- setup column ---------------------------------------------------------------
    // Coordinates below are relative to setup_content_, which is what the Viewport scrolls.
    const int view_width = juce::jmax(320, setup_viewport_.getMaximumVisibleWidth());
    const int column_width = juce::jmax(240, juce::jmin(kColumnMax, view_width - kContentPadX * 2));
    const int x = kContentPadX;
    int y = kContentPadTop;

    kickers_.clear();

    // `flow` exists so every button row can go through layoutRow(), which wraps rather than
    // silently dropping a control that does not fit. That behaviour is not optional: a
    // hand-tuned row is exactly how the BAND toggle disappeared once already
    // (KNOWN_ISSUES.md §21). The tall height is a scratch area, not a real extent.
    auto flowRow = [&](int rowHeight,
                       const std::vector<std::pair<juce::Component*, int>>& leftItems,
                       const std::vector<std::pair<juce::Component*, int>>& rightItems = {}) {
        juce::Rectangle<int> flow(x, y, column_width, 10000);
        const int before = flow.getY();
        layoutRow(flow, rowHeight, leftItems, rightItems);
        y += flow.getY() - before;
    };
    auto block = [&](int height) {
        const juce::Rectangle<int> r(x, y, column_width, height);
        y += height;
        return r;
    };
    auto kicker = [&](const char* text) {
        y += 26;
        kickers_.emplace_back(text, block(kKickerHeight));
        y += 12;
    };

    title_area_ = block(44);
    y += 30;

    rig_area_ = block(kRigRowHeight * static_cast<int>(rig_rows_.size()));
    y += 32;

    // Transport. PANIC and RECOVER are pulled to the right edge, away from everything else,
    // so a mis-aimed click cannot hit them and a deliberate one always lands.
    flowRow(46, {{&performance_button_, 210},
                 {&start_button_, 96},
                 {&stop_button_, 96},
                 {&ai_band_toggle_, 104}},
                {{&panic_button_, 170}, {&recover_button_, 150}});

    if (warning_shown_) {
        y += 10;
        warning_label_.setBounds(block(36));
    } else {
        warning_label_.setBounds(block(0));
    }

    kicker("SONG");
    flowRow(38, {{&load_song_button_, 150},
                 {&edit_song_button_, 116},
                 {&open_song_button_, 116},
                 {&save_song_button_, 116},
                 {&open_setlist_button_, 138}});
    y += 6;
    file_status_label_.setBounds(block(20));

    kicker("RIG");
    flowRow(38, {{&load_button_, 126}, {&foot_control_button_, 146}});

    kicker("BAND");
    {
        auto row = block(64);
        auto side = row.removeFromRight(210);
        prompt_editor_.setBounds(row.withTrimmedRight(14));
        apply_prompt_button_.setBounds(side.removeFromTop(34));
        prompt_status_label_.setBounds(side.withTrimmedTop(4));
    }

    y += 18;
    {
        auto row = block(30);
        buffer_label_.setBounds(row.removeFromLeft(kRowLabelWidth));
        buffer_combo_.setBounds(row.removeFromLeft(300));
    }
    y += 10;
    {
        auto row = block(30);
        intensity_label_.setBounds(row.removeFromLeft(kRowLabelWidth));
        intensity_slider_.setBounds(row);
    }
    y += 8;
    {
        auto row = block(30);
        level_label_.setBounds(row.removeFromLeft(kRowLabelWidth));
        level_slider_.setBounds(row);
    }

    kicker("HARMONY");
    harmony_label_.setBounds(block(26));
    y += 10;
    if (keyboard_ != nullptr) keyboard_->setBounds(block(92));

    kicker("AUDIO DEVICE");
    if (device_selector_ != nullptr) device_selector_->setBounds(block(300));

    kicker("DIAGNOSTICS");
    diagnostics_viewport_.setBounds(block(300));

    // The Viewport scrolls whatever height the content declares, so the column can be as
    // tall as it needs to be instead of fighting for pixels in a fixed window. That is the
    // canvas's own behaviour: its SETUP screen is `overflow-y: auto`.
    setup_content_.setSize(view_width, y + kContentPadBottom);
}

} // namespace ghostband::app
