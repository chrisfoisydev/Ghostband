// GhostBand — live AI accompaniment for singer-songwriters.
// Copyright 2026 GhostBand contributors. Licensed under Apache-2.0.
//
// ⚠️ macOS-only, NEVER COMPILED as of this commit. See KNOWN_ISSUES.md §1.

#include "MainComponent.h"

#include "core/ChordNamer.h"
#include "core/GhostBandConstants.h"
#include "core/Logging.h"

namespace ghostband::app {

namespace {

// UI string literals are kept strictly ASCII. Non-ASCII characters (em dashes, middots)
// arrive at the screen as mojibake — "PANIC — RELEASE" rendered as "PANIC â€ RELEASE" on
// a real run, because JUCE reinterpreted the UTF-8 bytes. Anything a performer might read
// mid-set has to be legible with certainty, so typography is not worth the risk here.
// If a non-ASCII glyph is genuinely needed later, wrap it: juce::CharPointer_UTF8("...").

// Dark, high-contrast, stage-hardware palette. No gradients, no purple, no sparkles.
const juce::Colour kBackground{0xff0e0f11};
const juce::Colour kPanel{0xff17191c};
const juce::Colour kText{0xffe8e8e8};
const juce::Colour kDim{0xff8a8f96};
const juce::Colour kOk{0xff37c871};
const juce::Colour kWarn{0xffe0a020};
const juce::Colour kFault{0xffe0453e};
const juce::Colour kPanicRed{0xffb3231c};

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

MainComponent::MainComponent() {
    const juce::String audio_error = engine_.initialise();

    addAndMakeVisible(load_button_);
    load_button_.onClick = [this] { loadModel(); };

    addAndMakeVisible(load_song_button_);
    load_song_button_.onClick = [this] {
        engine_.loadDemoSong();
        refreshStatus();
    };

    addAndMakeVisible(performance_button_);
    performance_button_.onClick = [this] { setPerformanceMode(true); };

    addAndMakeVisible(foot_control_button_);
    foot_control_button_.onClick = [this] { setFootControlMode(true); };

    addAndMakeVisible(save_song_button_);
    save_song_button_.onClick = [this] { saveCurrentSong(); };

    addAndMakeVisible(open_song_button_);
    open_song_button_.onClick = [this] { openSongFile(); };

    addAndMakeVisible(open_setlist_button_);
    open_setlist_button_.onClick = [this] { openSetlistFile(); };

    addAndMakeVisible(file_status_label_);
    file_status_label_.setColour(juce::Label::textColourId, kDim);

    addAndMakeVisible(start_button_);
    start_button_.onClick = [this] { engine_.startGeneration(); refreshStatus(); };

    addAndMakeVisible(stop_button_);
    stop_button_.onClick = [this] { engine_.stopGeneration(); refreshStatus(); };

    addAndMakeVisible(panic_button_);
    panic_button_.setColour(juce::TextButton::buttonColourId, kPanicRed);
    panic_button_.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    panic_button_.onClick = [this] {
        // Toggle: a second press releases, so PANIC is recoverable without a menu.
        if (engine_.isPanicked()) engine_.clearPanic();
        else engine_.panic();
        refreshStatus();
    };

    addChildComponent(recover_button_);  // shown only while Degraded
    recover_button_.setColour(juce::TextButton::buttonColourId, kWarn);
    recover_button_.setColour(juce::TextButton::textColourOffId, juce::Colours::black);
    recover_button_.onClick = [this] { engine_.recoverFromDegraded(); refreshStatus(); };

    addAndMakeVisible(ai_band_toggle_);
    ai_band_toggle_.setColour(juce::ToggleButton::textColourId, kText);
    ai_band_toggle_.onClick = [this] { engine_.setAiBandOn(ai_band_toggle_.getToggleState()); };

    addAndMakeVisible(prompt_editor_);
    prompt_editor_.setMultiLine(true);
    prompt_editor_.setReturnKeyStartsNewLine(false);
    prompt_editor_.setText("warm organic indie folk ensemble, piano, bass and restrained "
                           "percussion, instrumental");
    // Three ways to apply, because one was not enough: Enter, focus loss, and the
    // button. The first real run showed every prompt edit being silently discarded.
    prompt_editor_.onReturnKey = [this] { applyPrompt(); };
    prompt_editor_.onFocusLost = [this] { applyPrompt(); };

    addAndMakeVisible(apply_prompt_button_);
    apply_prompt_button_.onClick = [this] { applyPrompt(); };

    addAndMakeVisible(prompt_status_label_);
    prompt_status_label_.setColour(juce::Label::textColourId, kDim);

    addAndMakeVisible(buffer_label_);
    buffer_label_.setText("GEN BUFFER", juce::dontSendNotification);
    buffer_label_.setColour(juce::Label::textColourId, kDim);

    addAndMakeVisible(buffer_combo_);
    buffer_combo_.addItem("1 frame (40 ms) - lowest latency", 1);
    buffer_combo_.addItem("2 frames (80 ms) - default", 2);
    buffer_combo_.addItem("3 frames (120 ms) - most margin", 3);
    buffer_combo_.setSelectedId(2, juce::dontSendNotification);
    buffer_combo_.onChange = [this] {
        engine_.setGenerationBufferFrames(buffer_combo_.getSelectedId());
    };

    addAndMakeVisible(level_label_);
    level_label_.setText("AI OUTPUT LEVEL", juce::dontSendNotification);
    level_label_.setColour(juce::Label::textColourId, kDim);

    addAndMakeVisible(level_slider_);
    level_slider_.setRange(-60.0, 6.0, 0.1);
    level_slider_.setValue(0.0, juce::dontSendNotification);
    level_slider_.setTextValueSuffix(" dB");
    level_slider_.onValueChange = [this] {
        engine_.setOutputLevelDb(static_cast<float>(level_slider_.getValue()));
    };

    addAndMakeVisible(intensity_label_);
    intensity_label_.setText("AI INTENSITY", juce::dontSendNotification);
    intensity_label_.setColour(juce::Label::textColourId, kDim);

    addAndMakeVisible(intensity_slider_);
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
    addAndMakeVisible(*keyboard_);
    keyboard_state_.addListener(this);

    addAndMakeVisible(harmony_label_);
    harmony_label_.setColour(juce::Label::textColourId, kText);
    harmony_label_.setFont(juce::FontOptions(18.0f, juce::Font::bold));

    addAndMakeVisible(status_label_);
    status_label_.setColour(juce::Label::textColourId, kText);
    status_label_.setFont(juce::FontOptions(20.0f, juce::Font::bold));

    addAndMakeVisible(warning_label_);
    warning_label_.setColour(juce::Label::textColourId, kWarn);

    addAndMakeVisible(diagnostics_viewport_);
    diagnostics_viewport_.setViewedComponent(&diagnostics_text_, false);
    diagnostics_viewport_.setScrollBarsShown(true, false);

    performance_view_ = std::make_unique<PerformanceView>(engine_);
    performance_view_->onExitRequested = [this] { setPerformanceMode(false); };
    addChildComponent(*performance_view_);   // built now, shown only on demand

    foot_control_panel_ = std::make_unique<FootControlPanel>(engine_);
    foot_control_panel_->onCloseRequested = [this] { setFootControlMode(false); };
    addChildComponent(*foot_control_panel_);

    device_selector_ = std::make_unique<juce::AudioDeviceSelectorComponent>(
        engine_.deviceManager(),
        /*minInput*/ 0, /*maxInput*/ 0,   // Phase 0 has no input: GhostBand is additive only
        /*minOutput*/ 2, /*maxOutput*/ 2,
        /*showMidi*/ true,                // Phase 1: MIDI inputs are real, so show them
        /*showMidiOutput*/ false,         // GhostBand never sends MIDI out
        /*showChannelsAsStereoPairs*/ true,
        /*hideAdvanced*/ false);
    addAndMakeVisible(*device_selector_);

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
    setSize(980, 920);
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
    status_label_.setText("LOADING...", juce::dontSendNotification);

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
    file_status_is_error_ = isError;
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
    if (on) foot_control_mode_ = false;
    applyScreenVisibility();
    if (on && performance_view_ != nullptr) performance_view_->grabKeyboardFocus();
}

void MainComponent::setFootControlMode(bool on) {
    foot_control_mode_ = on;
    if (!on) engine_.cancelMidiLearn();
    applyScreenVisibility();
}

void MainComponent::applyScreenVisibility() {
    // Exactly one screen is visible at a time. Everything is hidden rather than destroyed,
    // so switching is instant and nothing is reallocated mid-performance.
    const bool setup_visible = !performance_mode_ && !foot_control_mode_;

    for (int i = 0; i < getNumChildComponents(); ++i) {
        auto* child = getChildComponent(i);
        if (child == performance_view_.get() || child == foot_control_panel_.get()) continue;
        child->setVisible(setup_visible);
    }
    if (performance_view_ != nullptr) performance_view_->setVisible(performance_mode_);
    if (foot_control_panel_ != nullptr) foot_control_panel_->setVisible(foot_control_mode_);

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

    juce::String status = juce::String(toDisplayString(state));
    if (!engine_.hasRealBackend() && state != core::EngineState::Loading) {
        // CLAUDE.md rule 2: never present a non-generating backend as a working band.
        status += "   -   NO AI BAND (no model loaded)";
    }
    if (engine_.isPanicked()) status += "   -   PANIC";
    status_label_.setText(status, juce::dontSendNotification);

    panic_button_.setButtonText(engine_.isPanicked() ? "RELEASE PANIC" : "PANIC");

    performance_button_.setEnabled(engine_.hasSong());
    start_button_.setEnabled(state == core::EngineState::Ready);
    stop_button_.setEnabled(state == core::EngineState::Running);
    ai_band_toggle_.setToggleState(engine_.isAiBandOn(), juce::dontSendNotification);

    juce::String warning = engine_.sampleRateWarning();
    if (state == core::EngineState::Error) warning = engine_.engineError();
    const bool degraded = engine_.health() == core::Health::Degraded;
    if (degraded) {
        warning = "AI muted: sustained audio underruns. Press RECOVER AI when stable.";
    }
    recover_button_.setVisible(degraded);
    warning_label_.setText(warning, juce::dontSendNotification);

    // The numbers this spike exists to produce. Anything unmeasured says so explicitly
    // rather than showing a plausible-looking zero.
    juce::String d;
    d << "Model                 " << snap.modelName << "\n"
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
      << "AI intensity          " << juce::String(engine_.aiIntensityPercent()) << " %\n"
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

void MainComponent::drawWordmark(juce::Graphics& g, float x, float baseline, float height) {
    // The GHOSTBAND mark: one word, all caps, "GHOST" as an outline and "BAND" solid.
    // Reproducing that split here rather than shipping a bitmap keeps the header sharp on
    // any display and lets it inherit the stage palette. Outlines are built through
    // GlyphArrangement -> Path because JUCE has no "stroke this text" call.
    const juce::Font mark(juce::FontOptions(height, juce::Font::bold));

    juce::GlyphArrangement ghost;
    ghost.addLineOfText(mark, "GHOST", x, baseline);
    juce::Path ghost_path;
    ghost.createPath(ghost_path);

    g.setColour(kText);
    g.strokePath(ghost_path, juce::PathStrokeType(1.4f));

    // Butt the two halves together — the mark is a single word, not two.
    const float ghost_width = ghost.getBoundingBox(0, -1, true).getWidth();

    juce::GlyphArrangement band;
    band.addLineOfText(mark, "BAND", x + ghost_width, baseline);
    juce::Path band_path;
    band.createPath(band_path);
    g.fillPath(band_path);
}

void MainComponent::paint(juce::Graphics& g) {
    g.fillAll(kBackground);

    drawWordmark(g, 24.0f, 44.0f, 30.0f);

    g.setColour(kDim);
    g.setFont(juce::FontOptions(13.0f));
    g.drawText("Your band follows you.   Phase 0 technical spike",
               24, 50, 600, 20, juce::Justification::left);

    const auto health = engine_.health();
    g.setColour(health == core::Health::Healthy ? kOk
              : health == core::Health::Warning ? kWarn : kFault);
    g.fillEllipse(24.0f, 88.0f, 10.0f, 10.0f);
}

void MainComponent::resized() {
    if (performance_view_ != nullptr) performance_view_->setBounds(getLocalBounds());
    if (foot_control_panel_ != nullptr) foot_control_panel_->setBounds(getLocalBounds());
    if (performance_mode_ || foot_control_mode_) return;   // an overlay owns the window

    auto area = getLocalBounds().reduced(24);
    area.removeFromTop(60); // title block

    auto status_row = area.removeFromTop(34);
    status_row.removeFromLeft(20); // clear the health dot
    status_label_.setBounds(status_row);

    warning_label_.setBounds(area.removeFromTop(24));
    area.removeFromTop(8);

    auto buttons = area.removeFromTop(40);
    panic_button_.setBounds(buttons.removeFromRight(180).reduced(2));
    recover_button_.setBounds(buttons.removeFromRight(130).reduced(2));
    load_button_.setBounds(buttons.removeFromLeft(130).reduced(2));
    load_song_button_.setBounds(buttons.removeFromLeft(150).reduced(2));
    performance_button_.setBounds(buttons.removeFromLeft(170).reduced(2));
    start_button_.setBounds(buttons.removeFromLeft(90).reduced(2));
    stop_button_.setBounds(buttons.removeFromLeft(90).reduced(2));
    ai_band_toggle_.setBounds(buttons.removeFromLeft(110).reduced(2));

    // Second row: setup-time controls that never need reaching for mid-song.
    area.removeFromTop(4);
    auto buttons2 = area.removeFromTop(34);
    foot_control_button_.setBounds(buttons2.removeFromLeft(160).reduced(2));
    open_song_button_.setBounds(buttons2.removeFromLeft(130).reduced(2));
    open_setlist_button_.setBounds(buttons2.removeFromLeft(140).reduced(2));
    save_song_button_.setBounds(buttons2.removeFromLeft(130).reduced(2));

    area.removeFromTop(2);
    file_status_label_.setBounds(area.removeFromTop(22));

    area.removeFromTop(12);
    auto prompt_row = area.removeFromTop(60);
    auto prompt_side = prompt_row.removeFromRight(200);
    prompt_editor_.setBounds(prompt_row);
    apply_prompt_button_.setBounds(prompt_side.removeFromTop(30).reduced(4, 2));
    prompt_status_label_.setBounds(prompt_side.reduced(4, 2));

    area.removeFromTop(6);
    auto buffer_row = area.removeFromTop(26);
    buffer_label_.setBounds(buffer_row.removeFromLeft(150));
    buffer_combo_.setBounds(buffer_row.removeFromLeft(280));

    area.removeFromTop(8);
    auto intensity_row = area.removeFromTop(28);
    intensity_label_.setBounds(intensity_row.removeFromLeft(150));
    intensity_slider_.setBounds(intensity_row);

    area.removeFromTop(4);
    auto level_row = area.removeFromTop(28);
    level_label_.setBounds(level_row.removeFromLeft(150));
    level_slider_.setBounds(level_row);

    area.removeFromTop(8);
    harmony_label_.setBounds(area.removeFromTop(26));

    if (keyboard_) keyboard_->setBounds(area.removeFromBottom(90));
    area.removeFromBottom(8);

    area.removeFromTop(12);
    auto lower = area;
    diagnostics_viewport_.setBounds(lower.removeFromLeft(lower.getWidth() / 2));
    lower.removeFromLeft(12);
    if (device_selector_) device_selector_->setBounds(lower);
}

} // namespace ghostband::app
