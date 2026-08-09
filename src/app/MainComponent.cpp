// GhostBand — live AI accompaniment for singer-songwriters.
// Copyright 2026 GhostBand contributors. Licensed under Apache-2.0.
//
// ⚠️ macOS-only, NEVER COMPILED as of this commit. See KNOWN_ISSUES.md §1.

#include "MainComponent.h"

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

MainComponent::MainComponent() {
    setSize(880, 720);

    const juce::String audio_error = engine_.initialise();

    addAndMakeVisible(load_button_);
    load_button_.onClick = [this] { loadModel(); };

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
    prompt_editor_.onReturnKey = [this] { engine_.setTextPrompt(prompt_editor_.getText()); };

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

    addAndMakeVisible(status_label_);
    status_label_.setColour(juce::Label::textColourId, kText);
    status_label_.setFont(juce::FontOptions(20.0f, juce::Font::bold));

    addAndMakeVisible(warning_label_);
    warning_label_.setColour(juce::Label::textColourId, kWarn);

    addAndMakeVisible(diagnostics_view_);
    diagnostics_view_.setMultiLine(true);
    diagnostics_view_.setReadOnly(true);
    diagnostics_view_.setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(),
                                                13.0f, juce::Font::plain));
    diagnostics_view_.setColour(juce::TextEditor::backgroundColourId, kPanel);
    diagnostics_view_.setColour(juce::TextEditor::textColourId, kDim);

    device_selector_ = std::make_unique<juce::AudioDeviceSelectorComponent>(
        engine_.deviceManager(),
        /*minInput*/ 0, /*maxInput*/ 0,   // Phase 0 has no input: GhostBand is additive only
        /*minOutput*/ 2, /*maxOutput*/ 2,
        /*showMidi*/ false,               // MIDI arrives in Phase 1, so it is absent, not fake
        /*showMidiOutput*/ false,
        /*showChannelsAsStereoPairs*/ true,
        /*hideAdvanced*/ false);
    addAndMakeVisible(*device_selector_);

    if (audio_error.isNotEmpty()) {
        warning_label_.setText("Audio device error: " + audio_error, juce::dontSendNotification);
    }

    setWantsKeyboardFocus(true);
    startTimerHz(10);
    refreshStatus();
}

MainComponent::~MainComponent() { stopTimer(); }

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

void MainComponent::timerCallback() { refreshStatus(); }

void MainComponent::refreshStatus() {
    const auto snap = engine_.diagnostics();
    const auto state = engine_.engineState();

    juce::String status = juce::String(toDisplayString(state));
    if (!engine_.hasRealBackend() && state != core::EngineState::Loading) {
        // CLAUDE.md rule 2: never present a non-generating backend as a working band.
        status += "   -   NO AI BAND (no model loaded)";
    }
    if (engine_.isPanicked()) status += "   -   PANIC";
    status_label_.setText(status, juce::dontSendNotification);

    panic_button_.setButtonText(engine_.isPanicked() ? "RELEASE PANIC" : "PANIC");

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
      << "Generation frame      " << juce::String(snap.generationTotalMs, 2) << " ms  (budget 40.00 ms)\n"
      << "  transformer         " << juce::String(snap.generationTransformerMs, 2) << " ms\n"
      << "  headroom            " << juce::String(snap.generationHeadroomRatio() * 100.0f, 1) << " % of budget\n"
      << "\n"
      << "Audio underruns       " << juce::String((juce::int64)snap.audioUnderruns) << "\n"
      << "Dropped frames (MRT2) " << juce::String((juce::int64)snap.droppedFrames) << "\n"
      << "Blocks processed      " << juce::String((juce::int64)snap.blocksProcessed) << "\n"
      << "Health                " << toString(engine_.health()) << "\n"
      << "\n"
      << "Output peak           " << juce::String(engine_.outputStage().peakDb(), 1) << " dBFS\n"
      << "Output RMS            " << juce::String(engine_.outputStage().rmsDb(), 1) << " dBFS\n"
      << "Gain reduction        " << juce::String(snap.gainReductionDb, 1) << " dB\n"
      << "CPU (audio)           " << juce::String(snap.audioCpuLoad * 100.0f, 1) << " %\n"
      << "\n"
      << "MIDI                  not implemented (Phase 1)\n"
      << "Memory                not measured\n";

    // Preserve the caret/scroll so the panel does not fight the user at 10 Hz.
    if (diagnostics_view_.getText() != d) {
        diagnostics_view_.setText(d, juce::dontSendNotification);
    }
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
    auto area = getLocalBounds().reduced(24);
    area.removeFromTop(60); // title block

    auto status_row = area.removeFromTop(34);
    status_row.removeFromLeft(20); // clear the health dot
    status_label_.setBounds(status_row);

    warning_label_.setBounds(area.removeFromTop(24));
    area.removeFromTop(8);

    auto buttons = area.removeFromTop(40);
    load_button_.setBounds(buttons.removeFromLeft(140).reduced(2));
    start_button_.setBounds(buttons.removeFromLeft(100).reduced(2));
    stop_button_.setBounds(buttons.removeFromLeft(100).reduced(2));
    ai_band_toggle_.setBounds(buttons.removeFromLeft(120).reduced(2));
    panic_button_.setBounds(buttons.removeFromRight(180).reduced(2));
    recover_button_.setBounds(buttons.removeFromRight(130).reduced(2));

    area.removeFromTop(12);
    prompt_editor_.setBounds(area.removeFromTop(60));

    area.removeFromTop(8);
    auto level_row = area.removeFromTop(28);
    level_label_.setBounds(level_row.removeFromLeft(150));
    level_slider_.setBounds(level_row);

    area.removeFromTop(12);
    auto lower = area;
    diagnostics_view_.setBounds(lower.removeFromLeft(lower.getWidth() / 2).reduced(0, 0));
    lower.removeFromLeft(12);
    if (device_selector_) device_selector_->setBounds(lower);
}

} // namespace ghostband::app
