// GhostBand — live AI accompaniment for singer-songwriters.
// Copyright 2026 GhostBand contributors. Licensed under Apache-2.0.
//
// macOS-only. Compiled on the target Mac; see KNOWN_ISSUES.md §1.

#include "FootControlPanel.h"

namespace ghostband::app {

namespace {

// Same stage palette as MainComponent. UI literals are strictly ASCII — non-ASCII reached
// the screen as mojibake on a real run.
const juce::Colour kBackground{0xff0e0f11};
const juce::Colour kPanel{0xff17191c};
const juce::Colour kText{0xffe8e8e8};
const juce::Colour kDim{0xff8a8f96};
const juce::Colour kOk{0xff37c871};
const juce::Colour kWarn{0xffe0a020};

constexpr int kRowHeight = 34;

juce::String bindingText(const core::MidiBinding& b) {
    if (!b.isValid()) return "unmapped";

    juce::String s(b.type == core::MidiBinding::Type::Note ? "Note " : "CC ");
    s << b.number;
    s << (b.channel == 0 ? juce::String("  (any ch)")
                         : "  (ch " + juce::String(b.channel) + ")");
    return s;
}

/// A binding that also swallows something harmony needs.
///
/// Notes: the message is consumed for the action, so that key stops reaching the chord.
/// CC 64: the MIDI spec's sustain pedal, which GhostBand uses to hold a chord.
/// Neither is forbidden — a performer with only a note-sending pedal has to bind notes —
/// but it is a real trade and not something to discover mid-song.
juce::String collisionWarning(const core::MidiBinding& b) {
    if (!b.isValid()) return {};
    if (b.type == core::MidiBinding::Type::Note) {
        return "A pedal is bound to note " + juce::String(b.number)
             + " - that key no longer reaches the harmony keyboard.";
    }
    if (b.number == 64) {
        return "A pedal is bound to CC 64 - the sustain pedal no longer holds chords.";
    }
    return {};
}

} // namespace

FootControlPanel::FootControlPanel(GhostBandAudioEngine& engine) : engine_(engine) {
    addAndMakeVisible(heading_);
    heading_.setText("FOOT CONTROL", juce::dontSendNotification);
    heading_.setColour(juce::Label::textColourId, kText);
    heading_.setFont(juce::FontOptions(22.0f, juce::Font::bold));

    addAndMakeVisible(hint_);
    hint_.setText("Press LEARN, then press the pedal. A row lights when its pedal fires.",
                  juce::dontSendNotification);
    hint_.setColour(juce::Label::textColourId, kDim);

    addAndMakeVisible(collision_label_);
    collision_label_.setColour(juce::Label::textColourId, kWarn);

    addAndMakeVisible(displaced_label_);
    displaced_label_.setColour(juce::Label::textColourId, kWarn);

    for (auto action : core::allPerformanceActions()) {
        Row row;
        row.action = action;

        row.name = std::make_unique<juce::Label>();
        row.name->setText(core::toDisplayString(action), juce::dontSendNotification);
        row.name->setColour(juce::Label::textColourId, kText);
        addAndMakeVisible(*row.name);

        row.binding = std::make_unique<juce::Label>();
        row.binding->setColour(juce::Label::textColourId, kDim);
        addAndMakeVisible(*row.binding);

        row.learn = std::make_unique<juce::TextButton>("LEARN");
        row.learn->onClick = [this, action] {
            // A second press of the same LEARN cancels, so an accidental arm does not
            // leave the next pedal press silently rebinding something.
            if (engine_.isMidiLearning() && engine_.midiLearningAction() == action) {
                engine_.cancelMidiLearn();
            } else {
                engine_.beginMidiLearn(action);
            }
            rebuildLabels();
        };
        addAndMakeVisible(*row.learn);

        row.clear = std::make_unique<juce::TextButton>("CLEAR");
        row.clear->onClick = [this, action] {
            auto set = engine_.midiMappings();
            set.clear(action);
            engine_.setMidiMappings(set);
            rebuildLabels();
        };
        addAndMakeVisible(*row.clear);

        rows_.push_back(std::move(row));
    }

    addAndMakeVisible(defaults_button_);
    defaults_button_.onClick = [this] {
        engine_.setMidiMappings(core::MidiMappingSet::makeDefault());
        rebuildLabels();
    };

    addAndMakeVisible(clear_all_button_);
    clear_all_button_.onClick = [this] {
        auto set = engine_.midiMappings();
        set.clearAll();
        engine_.setMidiMappings(set);
        rebuildLabels();
    };

    addAndMakeVisible(close_button_);
    close_button_.onClick = [this] {
        // Leaving with learn armed would make the next pedal press rebind something from
        // a screen the performer is no longer looking at.
        engine_.cancelMidiLearn();
        if (onCloseRequested) onCloseRequested();
    };

    rebuildLabels();
    startTimerHz(20);   // fast enough that the activity flash reads as a response
}

FootControlPanel::~FootControlPanel() { stopTimer(); }

void FootControlPanel::rebuildLabels() {
    const auto mappings = engine_.midiMappings();
    const bool learning = engine_.isMidiLearning();
    const auto learning_action = engine_.midiLearningAction();

    juce::String collision;

    for (auto& row : rows_) {
        const auto binding = mappings.bindingFor(row.action);
        const bool is_learning_this = learning && learning_action == row.action;

        row.binding->setText(is_learning_this ? "press a pedal..." : bindingText(binding),
                             juce::dontSendNotification);
        row.binding->setColour(juce::Label::textColourId,
                               is_learning_this ? kWarn
                                                : (binding.isValid() ? kText : kDim));
        row.learn->setButtonText(is_learning_this ? "CANCEL" : "LEARN");
        row.clear->setEnabled(binding.isValid());

        if (collision.isEmpty()) collision = collisionWarning(binding);
    }

    collision_label_.setText(collision, juce::dontSendNotification);
    repaint();
}

void FootControlPanel::timerCallback() {
    const auto fired = engine_.lastFiredAction();

    // A displaced binding is reported once and then cleared, so the message cannot linger
    // over a mapping the performer has since changed.
    if (const auto displaced = engine_.takeDisplacedAction();
        displaced != core::PerformanceAction::None) {
        displaced_label_.setText(juce::String("Took the switch from ")
                                     + core::toDisplayString(displaced)
                                     + " - it is now unmapped.",
                                 juce::dontSendNotification);
        rebuildLabels();
    }

    if (fired.action != core::PerformanceAction::None
        && fired.sequence != last_fired_sequence_) {
        last_fired_sequence_ = fired.sequence;
        const double now = juce::Time::getMillisecondCounterHiRes();
        for (auto& row : rows_) {
            if (row.action == fired.action) row.lit_at_ms = now;
        }
        repaint();
    }

    // Learn resolves on the MIDI thread, so the panel finds out by polling.
    const bool learning_now = engine_.isMidiLearning();
    if (learning_now != was_learning_) {
        was_learning_ = learning_now;
        rebuildLabels();
    }

    // Repaint while anything is still lit, so the flash decays rather than sticking.
    const double now = juce::Time::getMillisecondCounterHiRes();
    for (const auto& row : rows_) {
        if (now - row.lit_at_ms < kFlashMs) { repaint(); break; }
    }
}

void FootControlPanel::paint(juce::Graphics& g) {
    g.fillAll(kBackground);

    const double now = juce::Time::getMillisecondCounterHiRes();
    for (const auto& row : rows_) {
        if (row.name == nullptr) continue;
        auto bounds = row.name->getBounds().withRight(getWidth() - 24).expanded(4, 2);

        const double age = now - row.lit_at_ms;
        if (age < kFlashMs) {
            // Linear decay: the point is "did it arrive", so a visible tail beats a
            // subtle one.
            const auto alpha = static_cast<float>(1.0 - age / kFlashMs);
            g.setColour(kOk.withAlpha(alpha * 0.35f));
            g.fillRect(bounds);
        } else {
            g.setColour(kPanel);
            g.fillRect(bounds);
        }
    }
}

void FootControlPanel::resized() {
    auto area = getLocalBounds().reduced(24);

    heading_.setBounds(area.removeFromTop(30));
    hint_.setBounds(area.removeFromTop(22));
    collision_label_.setBounds(area.removeFromTop(22));
    displaced_label_.setBounds(area.removeFromTop(22));
    area.removeFromTop(10);

    for (auto& row : rows_) {
        auto r = area.removeFromTop(kRowHeight);
        row.name->setBounds(r.removeFromLeft(200));
        row.clear->setBounds(r.removeFromRight(90).reduced(2));
        row.learn->setBounds(r.removeFromRight(100).reduced(2));
        row.binding->setBounds(r);
        area.removeFromTop(4);
    }

    area.removeFromTop(16);
    auto buttons = area.removeFromTop(36);
    defaults_button_.setBounds(buttons.removeFromLeft(190).reduced(2));
    clear_all_button_.setBounds(buttons.removeFromLeft(130).reduced(2));
    close_button_.setBounds(buttons.removeFromRight(120).reduced(2));
}

} // namespace ghostband::app
