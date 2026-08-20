// GhostBand — live AI accompaniment for singer-songwriters.
// Copyright 2026 GhostBand contributors. Licensed under Apache-2.0.
//
// ⚠️ macOS-only, NEVER COMPILED as of this commit. See KNOWN_ISSUES.md §1.

#include "PerformanceView.h"

#include "StagePalette.h"
#include "StageType.h"

#include "core/ChordNamer.h"

namespace ghostband::app {

namespace {

// Stage palette: near-black, high contrast, no gradients. Readable from several feet in
// a dark room and under coloured stage lighting.
// Palette lives in StagePalette.h. The stage ground is darker than setup's on purpose.

// UI strings stay ASCII — see the note in MainComponent.cpp. On this screen it matters
// more than anywhere else: it is read at a glance, mid-song, from a distance.
// Kept as a thin shim so the many call sites below read unchanged, but every one of them
// now goes through the tracked type system in StageType.h. The previous version applied no
// letter-spacing, which is the single reason the first design pass looked identical to
// what it replaced.
juce::Font stageFont(float height, bool bold = true) {
    return bold ? displayFont(height) : valueFont(height);
}

} // namespace

PerformanceView::PerformanceView(GhostBandAudioEngine& engine) : engine_(engine) {
    auto styleButton = [this](juce::TextButton& b, juce::Colour bg, juce::Colour fg) {
        addAndMakeVisible(b);
        b.setColour(juce::TextButton::buttonColourId, bg);
        b.setColour(juce::TextButton::textColourOffId, fg);
    };

    styleButton(previous_button_, kPanel, kStageText);
    styleButton(next_button_, kPanel, kStageText);
    styleButton(less_button_, kPanel, kStageText);
    styleButton(more_button_, kPanel, kStageText);
    styleButton(panic_button_, kStagePanic, juce::Colours::white);
    styleButton(exit_button_, juce::Colour{0xff14171a}, kStageDim);

    previous_button_.onClick = [this] { engine_.previousSection(); repaint(); };
    next_button_.onClick = [this] { engine_.nextSection(); repaint(); };

    // Same step the pedal uses, so the button and the footswitch move the band by the
    // same amount. A control that behaves differently depending on how it was reached is
    // exactly the drift the single-entry-point rule exists to prevent.
    less_button_.onClick = [this] {
        engine_.setAiIntensity(engine_.aiIntensity() - 0.1f);
        repaint();
    };
    more_button_.onClick = [this] {
        engine_.setAiIntensity(engine_.aiIntensity() + 0.1f);
        repaint();
    };
    panic_button_.onClick = [this] {
        if (engine_.isPanicked()) engine_.clearPanic(); else engine_.panic();
        repaint();
    };
    exit_button_.onClick = [this] { if (onExitRequested) onExitRequested(); };

    setWantsKeyboardFocus(true);
    // 10 Hz. Nothing on this screen needs to be smoother, and a busy repaint is exactly
    // the "distracting animation" the brief rules out.
    startTimerHz(10);
}

PerformanceView::~PerformanceView() { stopTimer(); }

void PerformanceView::timerCallback() {
    // RESUME rather than PANIC once it has fired, from the design. A latched PANIC with a
    // button still reading "PANIC" gives the performer no visible way back.
    const auto* wanted = engine_.isPanicked() ? "RESUME" : "PANIC";
    if (panic_button_.getButtonText() != wanted) panic_button_.setButtonText(wanted);

    repaint();
}

void PerformanceView::visibilityChanged() {
    if (isVisible()) rebuildPedalLegend();
}

void PerformanceView::rebuildPedalLegend() {
    // Only the actions worth a glance mid-song, in the order a pedal is usually laid out.
    static const core::PerformanceAction kShown[] = {
        core::PerformanceAction::PreviousSection,
        core::PerformanceAction::NextSection,
        core::PerformanceAction::IntensityDown,
        core::PerformanceAction::IntensityUp,
        core::PerformanceAction::Panic,
    };

    const auto mappings = engine_.midiMappings();
    juce::String legend;

    for (auto action : kShown) {
        const auto binding = mappings.bindingFor(action);
        if (!binding.isValid()) continue;   // unmapped actions are omitted, not faked

        if (legend.isNotEmpty()) legend += "   ";
        legend += (binding.type == core::MidiBinding::Type::Note ? "NOTE " : "CC ");
        legend += juce::String(binding.number);
        legend += " ";
        legend += core::toDisplayString(action);
    }

    pedal_legend_ = legend.isEmpty() ? juce::String("NO PEDAL MAPPED") : legend;
}

bool PerformanceView::keyPressed(const juce::KeyPress& key) {
    // Every action reachable without the trackpad. Foot control maps onto these same
    // actions, so the pedal path and the key path cannot drift apart.
    // Song changes take a modifier, and are tested BEFORE the bare arrows. Sections change
    // many times a song and songs a handful of times a set, so the unmodified arrows
    // belong to sections — and a mis-hit that jumps to the next song costs far more than
    // one that jumps a section. Checked first because KeyPress's comparison against a bare
    // keycode is not obviously modifier-sensitive, and an ordering that depends on that
    // would fail silently.
    if (key.getKeyCode() == juce::KeyPress::rightKey
        && key.getModifiers().isCommandDown()) {
        engine_.nextSong();
        repaint();
        return true;
    }
    if (key.getKeyCode() == juce::KeyPress::leftKey
        && key.getModifiers().isCommandDown()) {
        engine_.previousSong();
        repaint();
        return true;
    }

    if (key == juce::KeyPress::leftKey)  { engine_.previousSection(); repaint(); return true; }
    if (key == juce::KeyPress::rightKey) { engine_.nextSection();     repaint(); return true; }
    if (key == juce::KeyPress::escapeKey) {
        engine_.panic();
        repaint();
        return true;
    }
    if (key.getTextCharacter() == 'r' || key.getTextCharacter() == 'R') {
        engine_.repeatSection();   // repeating a chorus is a first-class action
        repaint();
        return true;
    }
    return false;
}

void PerformanceView::drawSectionBlock(juce::Graphics& g, juce::Rectangle<int> area) {
    const auto& perf = engine_.performance();
    const auto* section = perf.sections().current();

    // Song title: present but subordinate. The performer knows what song they are in.
    auto title_row = area.removeFromTop(36);
    g.setColour(kStageDim);
    g.setFont(labelFont(14.0f));
    g.drawText(perf.hasSong() ? juce::String(perf.song()->title) : juce::String("NO SONG"),
               title_row, juce::Justification::centredLeft);

    // Position in the set, right-aligned so it never crowds the title. Only when a set is
    // loaded — "song 1 of 1" for a single song would be noise.
    if (engine_.hasSetlist()) {
        const auto& set = engine_.setlist();
        g.drawText(juce::String(set.currentIndex() + 1) + " / " + juce::String(set.size()),
                   title_row, juce::Justification::centredRight);
    }

    area.removeFromTop(4);

    // "NOW" from the design. One word, but it turns a bare name into an answer to the
    // question the performer is actually asking when they glance down.
    g.setColour(kStageDim);
    g.setFont(labelFont(12.0f));
    g.drawText("NOW", area.removeFromTop(22), juce::Justification::centredLeft);

    // The one thing that must be readable across a stage.
    //
    // A setlist entry whose file is missing has no sections to show. Saying so in the
    // large type is the honest thing: an empty stage screen looks like a crash, and the
    // performer needs to know immediately that this slot has nothing behind it.
    const bool missing_song = engine_.hasSetlist() && engine_.setlist().currentSong() == nullptr;

    g.setColour(missing_song ? kStageFault : kStageText);
    g.setFont(stageFont(missing_song ? 56.0f : 96.0f));
    g.drawText(missing_song  ? juce::String("SONG FILE MISSING")
             : section != nullptr ? juce::String(section->name).toUpperCase()
                                  : juce::String("-"),
               area.removeFromTop(110), juce::Justification::centredLeft);

    area.removeFromTop(10);

    // Next section preview. Absent at the end of the song rather than wrapping — a
    // surprise return to the top mid-set is worse than showing nothing.
    const auto* next = perf.sections().peekNext();
    g.setColour(kStageDim);
    g.setFont(labelFont(12.0f));
    g.drawText("NEXT", area.removeFromTop(24), juce::Justification::centredLeft);

    g.setColour(next != nullptr ? kStageText : kStageDim);
    g.setFont(stageFont(38.0f));
    g.drawText(next != nullptr ? juce::String(next->name).toUpperCase()
                               : juce::String("END OF SONG"),
               area.removeFromTop(46), juce::Justification::centredLeft);

    // At the end of a song inside a set, what comes next is a song, not a section. The
    // one moment a performer most needs to know what is coming is exactly here.
    if (next == nullptr && engine_.hasSetlist()) {
        const auto* next_song = engine_.setlist().peekNext();
        g.setColour(kStageDim);
        g.setFont(labelFont(12.0f));
        g.drawText(next_song != nullptr
                       ? "THEN: " + juce::String(next_song->cachedTitle).toUpperCase()
                       : juce::String("END OF SET"),
                   area.removeFromTop(30), juce::Justification::centredLeft);
    }
}

void PerformanceView::drawFollowingRow(juce::Graphics& g, juce::Rectangle<int> area) {
    // What the band is following, on the stage screen rather than only in setup. This is
    // the product thesis made visible: if the performer cannot see what GhostBand thinks
    // they are playing, they cannot tell a wrong chord from a slow one.
    //
    // The design also shows "STRONG SIGNAL" beside this. That is guitar-follow confidence,
    // which does not exist (Phase 3), so it is deliberately absent rather than faked.
    const auto sounding = engine_.harmony().soundingNotes();
    const juce::String chord(core::nameChord(sounding));

    g.setColour(kStageDim);
    g.setFont(labelFont(12.0f));
    g.drawText("FOLLOWING", area.removeFromTop(24), juce::Justification::centredLeft);

    g.setColour(sounding.empty() ? kStageDim : kStageText);
    g.setFont(stageFont(38.0f));
    g.drawText(sounding.empty() ? juce::String("-") : chord, area,
               juce::Justification::centredLeft);
}

void PerformanceView::drawBandRow(juce::Graphics& g, juce::Rectangle<int> area) {
    const auto& perf = engine_.performance();
    const bool audible = engine_.isAiBandOn()
                      && (!perf.hasSong() || perf.currentSectionAiEnabled());

    auto label_row = area.removeFromTop(24);
    g.setColour(kStageDim);
    g.setFont(labelFont(12.0f));
    g.drawText("BAND", label_row, juce::Justification::centredLeft);
    g.drawText("BAND VOLUME  " + juce::String(engine_.outputLevelDb(), 1) + " dB",
               label_row, juce::Justification::centredRight);

    auto state_row = area.removeFromTop(44);
    g.setColour(audible ? kStageOk : kStageDim);
    g.fillEllipse(static_cast<float>(state_row.getX()),
                  static_cast<float>(state_row.getCentreY() - 7), 14.0f, 14.0f);

    g.setColour(kStageText);
    g.setFont(stageFont(34.0f));
    g.drawText(audible ? "ON" : "OFF", state_row.withTrimmedLeft(26),
               juce::Justification::centredLeft);

    // SPARSE <-> FULL, drawn as a bar rather than a percentage. "68 %" is a number to
    // decode; a bar is a position to glance at, which is all there is time for mid-song.
    auto bar = state_row.removeFromRight(state_row.getWidth() * 2 / 3).reduced(0, 14);
    g.setColour(kStageDim);
    g.setFont(labelFont(10.0f));
    g.drawText("SPARSE", bar.removeFromLeft(58), juce::Justification::centredLeft);
    g.drawText("FULL", bar.removeFromRight(40), juce::Justification::centredRight);

    const auto track = bar.reduced(8, 5).toFloat();
    g.setColour(kPanelRaised);
    g.fillRoundedRectangle(track, 3.0f);

    const float fill = juce::jlimit(0.0f, 1.0f, perf.intensity().intensity());
    if (fill > 0.0f) {
        g.setColour(audible ? kStageOk : kStageDim);
        g.fillRoundedRectangle(track.withWidth(track.getWidth() * fill), 3.0f);
    }
}

void PerformanceView::drawStatusRow(juce::Graphics& g, juce::Rectangle<int> area) {
    struct Light { juce::String text; juce::Colour colour; };

    const auto state = engine_.engineState();
    const auto health = engine_.health();

    std::vector<Light> lights;

    lights.push_back({juce::String("MRT2 ") + toDisplayString(state),
                      state == core::EngineState::Running ? kStageOk
                      : state == core::EngineState::Error ? kStageFault : kStageWarn});

    lights.push_back({engine_.anyMidiDeviceConnected() ? "MIDI CONNECTED" : "NO MIDI DEVICE",
                      engine_.anyMidiDeviceConnected() ? kStageOk : kStageDim});

    lights.push_back({juce::String(toDisplayString(health)),
                      health == core::Health::Healthy ? kStageOk
                      : health == core::Health::Warning ? kStageWarn : kStageFault});

    // Only shown when true: a song whose changes may hesitate is worth knowing about on
    // stage, and staying silent about it would be the dishonest option.
    if (engine_.hasSong() && !engine_.performance().allocator().isFullyResident()) {
        lights.push_back({"SOME SECTION CHANGES NOT INSTANT", kStageWarn});
    }

    g.setFont(labelFont(11.0f));
    int x = area.getX();
    for (const auto& light : lights) {
        g.setColour(light.colour);
        g.fillEllipse(static_cast<float>(x), static_cast<float>(area.getCentreY() - 5), 10.0f, 10.0f);

        const int width = juce::GlyphArrangement::getStringWidthInt(labelFont(11.0f),
                                                                    light.text) + 34;
        g.setColour(kStageDim);
        g.drawText(light.text, x + 16, area.getY(), width, area.getHeight(),
                   juce::Justification::centredLeft);
        x += width;
    }
}

void PerformanceView::paint(juce::Graphics& g) {
    g.fillAll(kStageBg);

    auto area = getLocalBounds().reduced(40, 28);
    area.removeFromTop(34);          // header row, occupied by EXIT

    drawSectionBlock(g, area.removeFromTop(300));

    // Status and the pedal legend live at the bottom, out of the glance path of the
    // section name — which is the only thing on this screen meant to be read at distance.
    drawStatusRow(g, area.removeFromBottom(28));

    g.setColour(kStageDim);
    g.setFont(labelFont(10.0f));
    g.drawText(pedal_legend_, area.removeFromBottom(22), juce::Justification::centredLeft);

    area.removeFromBottom(96);       // the button row, laid out in resized()

    drawBandRow(g, area.removeFromBottom(72));
    area.removeFromBottom(10);
    drawFollowingRow(g, area.removeFromBottom(66));

    if (engine_.isPanicked()) {
        // Unmissable. If the band is silent because of PANIC, that must never be a
        // mystery the performer has to solve mid-song.
        //
        // Two lines, from the design canvas, because the first one alone is not the
        // reassurance that matters. "AI SILENT" says what stopped; what the performer
        // actually needs to know in that second is that their own signal did not — that
        // they can keep playing while they work out what went wrong. That is the brief's
        // failure philosophy stated on screen rather than only in the architecture.
        auto banner = getLocalBounds().reduced(40, 28).removeFromBottom(160);

        g.setColour(kStagePanic);
        g.setFont(stageFont(44.0f));
        g.drawText("BAND STOPPED", banner.removeFromTop(56), juce::Justification::centred);

        g.setColour(kStageText);
        g.setFont(labelFont(16.0f));
        g.drawText("YOUR GUITAR AND VOCAL ARE CLEAR", banner.removeFromTop(34),
                   juce::Justification::centred);
    }
}

void PerformanceView::resized() {
    auto area = getLocalBounds().reduced(40, 28);
    exit_button_.setBounds(area.removeFromTop(34).removeFromRight(120));

    // Must mirror paint()'s bottom-up order exactly, or the buttons land on the readouts.
    area.removeFromBottom(28);       // status row
    area.removeFromBottom(22);       // pedal legend
    auto controls = area.removeFromBottom(96);

    // Big targets. On stage these are hit in a hurry, or by someone not looking. PANIC
    // keeps the right edge to itself so a mis-aimed grab for NEXT cannot reach it.
    panic_button_.setBounds(controls.removeFromRight(200).reduced(6));
    controls.removeFromRight(24);    // a deliberate gap, not spacing

    const int quarter = controls.getWidth() / 4;
    previous_button_.setBounds(controls.removeFromLeft(quarter).reduced(6));
    next_button_.setBounds(controls.removeFromLeft(quarter).reduced(6));
    less_button_.setBounds(controls.removeFromLeft(quarter).reduced(6));
    more_button_.setBounds(controls.reduced(6));
}

} // namespace ghostband::app
