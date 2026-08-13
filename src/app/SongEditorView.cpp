// GhostBand — live AI accompaniment for singer-songwriters.
// Copyright 2026 GhostBand contributors. Licensed under Apache-2.0.
//
// macOS-only. Compiled on the target Mac; see KNOWN_ISSUES.md §1.

#include "SongEditorView.h"

#include "StagePalette.h"

#include <cstdlib>   // std::abs
#include <iterator>  // std::size

namespace ghostband::app {

namespace {

// UI literals stay strictly ASCII — non-ASCII reached the screen as mojibake on a real run.

constexpr int kRowHeight = 30;

struct TransitionChoice { const char* label; int ms; };

/// The brief's named transition times (§15). Offered as a list rather than a free number
/// because "how fast does the band change" is a musical decision with a handful of useful
/// answers, not a millisecond to be dialled in.
const TransitionChoice kTransitions[] = {
    {"Instant",   core::kTransitionInstantMs},
    {"Fast",      core::kTransitionFastMs},
    {"Medium",    core::kTransitionMediumMs},
    {"Slow",      core::kTransitionSlowMs},
    {"Very slow", core::kTransitionVerySlowMs},
};

int transitionIndexFor(int ms) {
    int best = 0;
    int best_delta = -1;
    for (int i = 0; i < static_cast<int>(std::size(kTransitions)); ++i) {
        const int delta = std::abs(kTransitions[i].ms - ms);
        if (best_delta < 0 || delta < best_delta) { best = i; best_delta = delta; }
    }
    return best;
}

} // namespace

SongEditorView::SongEditorView(GhostBandAudioEngine& engine) : engine_(engine) {
    auto addLabel = [this](juce::Label& l, const char* text, juce::Colour colour) {
        addAndMakeVisible(l);
        l.setText(text, juce::dontSendNotification);
        l.setColour(juce::Label::textColourId, colour);
    };

    addLabel(heading_, "SONG EDITOR", kText);
    heading_.setFont(juce::FontOptions(22.0f, juce::Font::bold));

    addLabel(title_label_, "TITLE", kDim);
    addAndMakeVisible(title_editor_);
    title_editor_.onTextChange = [this] {
        if (updating_) return;
        editor_.setTitle(title_editor_.getText().toStdString());
        refresh();
    };

    addLabel(default_prompt_label_, "SONG PROMPT", kDim);
    addAndMakeVisible(default_prompt_editor_);
    default_prompt_editor_.setMultiLine(true);
    default_prompt_editor_.setReturnKeyStartsNewLine(false);
    default_prompt_editor_.onTextChange = [this] {
        if (updating_) return;
        editor_.setDefaultPrompt(default_prompt_editor_.getText().toStdString());
        refresh();
    };

    addAndMakeVisible(section_list_);
    section_list_.setModel(this);
    section_list_.setRowHeight(kRowHeight);
    section_list_.setColour(juce::ListBox::backgroundColourId, kPanel);

    addAndMakeVisible(add_button_);
    add_button_.onClick = [this] {
        const int index = editor_.addSection();
        refresh();
        section_list_.selectRow(index);
    };

    addAndMakeVisible(duplicate_button_);
    duplicate_button_.onClick = [this] {
        const int index = editor_.duplicateSection(selectedSection());
        refresh();
        if (index >= 0) section_list_.selectRow(index);
    };

    addAndMakeVisible(remove_button_);
    remove_button_.onClick = [this] {
        const int index = selectedSection();
        if (!editor_.removeSection(index)) return;
        refresh();
        section_list_.selectRow(juce::jmin(index, editor_.sectionCount() - 1));
    };

    addAndMakeVisible(up_button_);
    up_button_.onClick = [this] {
        const int index = selectedSection();
        if (!editor_.moveSection(index, index - 1)) return;
        refresh();
        section_list_.selectRow(index - 1);
    };

    addAndMakeVisible(down_button_);
    down_button_.onClick = [this] {
        const int index = selectedSection();
        if (!editor_.moveSection(index, index + 1)) return;
        refresh();
        section_list_.selectRow(index + 1);
    };

    addLabel(section_heading_, "SECTION", kText);
    section_heading_.setFont(juce::FontOptions(18.0f, juce::Font::bold));

    addLabel(name_label_, "NAME", kDim);
    addAndMakeVisible(name_editor_);
    // Applied on focus loss and Enter rather than per keystroke: the uniqueness rule would
    // otherwise rename a section to "Vers 2" while the performer is still typing "Verse".
    name_editor_.onReturnKey = [this] {
        if (updating_) return;
        editor_.setSectionName(selectedSection(), name_editor_.getText().toStdString());
        refresh();
    };
    name_editor_.onFocusLost = [this] {
        if (updating_) return;
        editor_.setSectionName(selectedSection(), name_editor_.getText().toStdString());
        refresh();
    };

    addLabel(prompt_label_, "SECTION PROMPT", kDim);
    addAndMakeVisible(prompt_editor_);
    prompt_editor_.setMultiLine(true);
    prompt_editor_.setReturnKeyStartsNewLine(false);
    prompt_editor_.onTextChange = [this] {
        if (updating_) return;
        editor_.setSectionPrompt(selectedSection(), prompt_editor_.getText().toStdString());
        refresh();
    };

    addLabel(prompt_hint_, "Leave empty to use the song prompt.", kDim);

    addLabel(intensity_label_, "AI INTENSITY", kDim);
    addAndMakeVisible(intensity_slider_);
    intensity_slider_.setRange(0.0, 100.0, 1.0);
    intensity_slider_.setTextValueSuffix(" %");
    intensity_slider_.onValueChange = [this] {
        if (updating_) return;
        editor_.setSectionIntensity(selectedSection(),
                                    static_cast<float>(intensity_slider_.getValue() / 100.0));
        refresh();
    };

    addAndMakeVisible(ai_enabled_toggle_);
    ai_enabled_toggle_.setColour(juce::ToggleButton::textColourId, kText);
    ai_enabled_toggle_.onClick = [this] {
        if (updating_) return;
        editor_.setSectionAiEnabled(selectedSection(), ai_enabled_toggle_.getToggleState());
        refresh();
    };

    addLabel(transition_label_, "TRANSITION", kDim);
    addAndMakeVisible(transition_combo_);
    for (int i = 0; i < static_cast<int>(std::size(kTransitions)); ++i) {
        transition_combo_.addItem(juce::String(kTransitions[i].label) + "  ("
                                      + juce::String(kTransitions[i].ms) + " ms)",
                                  i + 1);
    }
    transition_combo_.onChange = [this] {
        if (updating_) return;
        const int index = transition_combo_.getSelectedId() - 1;
        if (index < 0 || index >= static_cast<int>(std::size(kTransitions))) return;
        editor_.setSectionTransitionMs(selectedSection(), kTransitions[index].ms);
        refresh();
    };

    addLabel(validation_label_, "", kFault);
    addLabel(slots_label_, "", kWarn);
    addLabel(file_status_label_, "", kDim);

    addAndMakeVisible(undo_button_);
    undo_button_.onClick = [this] {
        if (!editor_.undo()) return;
        refresh();
    };

    addAndMakeVisible(apply_button_);
    apply_button_.onClick = [this] { applyToBand(); };

    addAndMakeVisible(save_button_);
    save_button_.onClick = [this] { saveToDisk(); };

    addAndMakeVisible(close_button_);
    close_button_.onClick = [this] { requestClose(); };

    refresh();
}

SongEditorView::~SongEditorView() { section_list_.setModel(nullptr); }

void SongEditorView::beginEditing(const core::Song& song) {
    editor_.reset(song);
    confirming_discard_ = false;
    file_status_label_.setText({}, juce::dontSendNotification);
    refresh();
    section_list_.selectRow(0);
}

int SongEditorView::selectedSection() const {
    return section_list_.getSelectedRow();
}

// --- ListBoxModel -------------------------------------------------------------------

int SongEditorView::getNumRows() { return editor_.sectionCount(); }

void SongEditorView::paintListBoxItem(int row, juce::Graphics& g, int width, int height,
                                      bool selected) {
    const auto* section = editor_.sectionAt(row);
    if (section == nullptr) return;

    g.fillAll(selected ? juce::Colour{0xff23272c} : kPanel);

    // Order number: the performer's mental model of a song is "verse, chorus, verse" in
    // sequence, and the number is what makes MOVE UP/DOWN legible.
    g.setColour(kDim);
    g.setFont(juce::FontOptions(13.0f));
    g.drawText(juce::String(row + 1), 6, 0, 24, height, juce::Justification::centredLeft);

    g.setColour(section->aiEnabled ? kText : kDim);
    g.setFont(juce::FontOptions(15.0f, juce::Font::bold));
    g.drawText(section->name, 32, 0, width - 130, height, juce::Justification::centredLeft);

    // A section with the band off is a musical choice, not a disabled row — so it is
    // labelled rather than greyed into looking broken.
    g.setColour(section->aiEnabled ? kDim : kWarn);
    g.setFont(juce::FontOptions(12.0f));
    g.drawText(section->aiEnabled
                   ? juce::String(static_cast<int>(section->aiIntensity * 100.0f + 0.5f)) + " %"
                   : juce::String("NO BAND"),
               width - 92, 0, 84, height, juce::Justification::centredRight);
}

void SongEditorView::selectedRowsChanged(int) { refreshSectionFields(); }

// --- Refresh -------------------------------------------------------------------------

void SongEditorView::refresh() {
    // Guarded because setText fires onTextChange, which would push an undo entry for a
    // change the performer never made.
    const juce::ScopedValueSetter<bool> guard(updating_, true);

    const auto& song = editor_.song();
    if (title_editor_.getText() != juce::String(song.title)) {
        title_editor_.setText(song.title, juce::dontSendNotification);
    }
    if (default_prompt_editor_.getText() != juce::String(song.defaultStylePrompt)) {
        default_prompt_editor_.setText(song.defaultStylePrompt, juce::dontSendNotification);
    }

    section_list_.updateContent();
    section_list_.repaint();

    const int index = selectedSection();
    const bool has_selection = index >= 0 && index < editor_.sectionCount();
    duplicate_button_.setEnabled(has_selection);
    remove_button_.setEnabled(has_selection && editor_.sectionCount() > 1);
    up_button_.setEnabled(has_selection && index > 0);
    down_button_.setEnabled(has_selection && index < editor_.sectionCount() - 1);
    undo_button_.setEnabled(editor_.canUndo());

    // Both refuse rather than write something unusable. Saving an invalid song would move
    // the discovery to load time, and applying one would stop the band mid-set.
    save_button_.setEnabled(editor_.isPerformable());
    apply_button_.setEnabled(editor_.isPerformable());
    save_button_.setButtonText(editor_.isDirty() ? "SAVE SONG *" : "SAVE SONG");

    validation_label_.setText(editor_.isPerformable()
                                  ? juce::String()
                                  : "Cannot save: " + juce::String(editor_.validationError()),
                              juce::dontSendNotification);

    // The slot ceiling, stated in the terms that matter: which section changes will hesitate.
    if (editor_.fitsPromptSlots()) {
        slots_label_.setText(juce::String(editor_.distinctPromptCount()) + " of "
                                 + juce::String(core::SongEditor::promptCapacity())
                                 + " prompt slots used - every section change is instant",
                             juce::dontSendNotification);
        slots_label_.setColour(juce::Label::textColourId, kDim);
    } else {
        slots_label_.setText(juce::String(editor_.distinctPromptCount())
                                 + " distinct prompts exceeds the "
                                 + juce::String(core::SongEditor::promptCapacity())
                                 + " MRT2 holds - some section changes will hesitate. "
                                   "Reuse a prompt between sections to fix it.",
                             juce::dontSendNotification);
        slots_label_.setColour(juce::Label::textColourId, kWarn);
    }

    if (!editor_.isDirty()) confirming_discard_ = false;
    close_button_.setButtonText(confirming_discard_ ? "DISCARD CHANGES?" : "DONE");

    refreshSectionFields();
}

void SongEditorView::refreshSectionFields() {
    const juce::ScopedValueSetter<bool> guard(updating_, true);

    const int index = selectedSection();
    const auto* section = editor_.sectionAt(index);

    const bool enabled = section != nullptr;
    name_editor_.setEnabled(enabled);
    prompt_editor_.setEnabled(enabled);
    intensity_slider_.setEnabled(enabled);
    ai_enabled_toggle_.setEnabled(enabled);
    transition_combo_.setEnabled(enabled);

    if (section == nullptr) {
        section_heading_.setText("SECTION", juce::dontSendNotification);
        name_editor_.setText({}, juce::dontSendNotification);
        prompt_editor_.setText({}, juce::dontSendNotification);
        return;
    }

    section_heading_.setText("SECTION " + juce::String(index + 1) + " OF "
                                 + juce::String(editor_.sectionCount()),
                             juce::dontSendNotification);

    if (name_editor_.getText() != juce::String(section->name)) {
        name_editor_.setText(section->name, juce::dontSendNotification);
    }
    if (prompt_editor_.getText() != juce::String(section->stylePrompt)) {
        prompt_editor_.setText(section->stylePrompt, juce::dontSendNotification);
    }
    intensity_slider_.setValue(section->aiIntensity * 100.0, juce::dontSendNotification);
    ai_enabled_toggle_.setToggleState(section->aiEnabled, juce::dontSendNotification);
    transition_combo_.setSelectedId(transitionIndexFor(section->transitionMs) + 1,
                                    juce::dontSendNotification);

    // What this section will actually sound like, resolving inheritance. Without it an
    // empty prompt field looks like a mistake rather than "use the song prompt".
    prompt_hint_.setText(section->stylePrompt.empty()
                             ? "Empty - uses the song prompt: \""
                                   + juce::String(editor_.song().defaultStylePrompt) + "\""
                             : juce::String("Overrides the song prompt for this section."),
                         juce::dontSendNotification);
}

// --- Actions ---------------------------------------------------------------------------

void SongEditorView::applyToBand() {
    if (!engine_.loadSong(editor_.song())) {
        file_status_label_.setColour(juce::Label::textColourId, kFault);
        file_status_label_.setText("Could not load this song into the band.",
                                   juce::dontSendNotification);
        return;
    }
    file_status_label_.setColour(juce::Label::textColourId, kOk);
    file_status_label_.setText("Loaded into the band. Not saved to disk yet.",
                               juce::dontSendNotification);
}

void SongEditorView::saveToDisk() {
    juce::File written;
    const auto error = engine_.saveSongAs(editor_.song(), written);
    if (error.isNotEmpty()) {
        file_status_label_.setColour(juce::Label::textColourId, kFault);
        file_status_label_.setText(error, juce::dontSendNotification);
        return;
    }

    editor_.markSaved();
    file_status_label_.setColour(juce::Label::textColourId, kOk);
    file_status_label_.setText("Saved " + written.getFileName(), juce::dontSendNotification);
    refresh();
}

void SongEditorView::requestClose() {
    // One confirmation, in the button itself. Losing an evening's arrangement to a stray
    // click is not worth saving a modal dialog, and a modal dialog is not worth adding to
    // an app that otherwise has none.
    if (editor_.isDirty() && !confirming_discard_) {
        confirming_discard_ = true;
        close_button_.setButtonText("DISCARD CHANGES?");
        file_status_label_.setColour(juce::Label::textColourId, kWarn);
        file_status_label_.setText("Unsaved changes. Press again to discard, or SAVE SONG.",
                                   juce::dontSendNotification);
        return;
    }
    confirming_discard_ = false;
    if (onCloseRequested) onCloseRequested();
}

// --- Layout ----------------------------------------------------------------------------

void SongEditorView::paint(juce::Graphics& g) { g.fillAll(kBackground); }

void SongEditorView::resized() {
    auto area = getLocalBounds().reduced(20);

    heading_.setBounds(area.removeFromTop(28));
    area.removeFromTop(6);

    auto title_row = area.removeFromTop(26);
    title_label_.setBounds(title_row.removeFromLeft(110));
    title_editor_.setBounds(title_row);
    area.removeFromTop(4);

    auto prompt_row = area.removeFromTop(46);
    default_prompt_label_.setBounds(prompt_row.removeFromLeft(110));
    default_prompt_editor_.setBounds(prompt_row);
    area.removeFromTop(6);

    slots_label_.setBounds(area.removeFromTop(20));
    validation_label_.setBounds(area.removeFromTop(20));
    area.removeFromTop(8);

    auto footer = area.removeFromBottom(38);
    file_status_label_.setBounds(area.removeFromBottom(22));
    undo_button_.setBounds(footer.removeFromLeft(100).reduced(2));
    close_button_.setBounds(footer.removeFromRight(180).reduced(2));
    save_button_.setBounds(footer.removeFromRight(140).reduced(2));
    apply_button_.setBounds(footer.removeFromRight(160).reduced(2));
    area.removeFromBottom(8);

    // Left: the running order. Right: the selected section.
    auto left = area.removeFromLeft(300);
    auto list_buttons = left.removeFromBottom(34);
    add_button_.setBounds(list_buttons.removeFromLeft(60).reduced(1));
    duplicate_button_.setBounds(list_buttons.removeFromLeft(90).reduced(1));
    remove_button_.setBounds(list_buttons.removeFromLeft(80).reduced(1));
    left.removeFromBottom(4);

    auto move_buttons = left.removeFromBottom(30);
    up_button_.setBounds(move_buttons.removeFromLeft(110).reduced(1));
    down_button_.setBounds(move_buttons.removeFromLeft(120).reduced(1));
    left.removeFromBottom(6);
    section_list_.setBounds(left);

    area.removeFromLeft(16);
    auto right = area;

    section_heading_.setBounds(right.removeFromTop(24));
    right.removeFromTop(6);

    auto name_row = right.removeFromTop(26);
    name_label_.setBounds(name_row.removeFromLeft(120));
    name_editor_.setBounds(name_row);
    right.removeFromTop(8);

    prompt_label_.setBounds(right.removeFromTop(20));
    prompt_editor_.setBounds(right.removeFromTop(70));
    prompt_hint_.setBounds(right.removeFromTop(20));
    right.removeFromTop(8);

    auto intensity_row = right.removeFromTop(28);
    intensity_label_.setBounds(intensity_row.removeFromLeft(120));
    intensity_slider_.setBounds(intensity_row);
    right.removeFromTop(6);

    ai_enabled_toggle_.setBounds(right.removeFromTop(26));
    right.removeFromTop(6);

    auto transition_row = right.removeFromTop(26);
    transition_label_.setBounds(transition_row.removeFromLeft(120));
    transition_combo_.setBounds(transition_row.removeFromLeft(220));
}

} // namespace ghostband::app
