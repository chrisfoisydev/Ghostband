// GhostBand — live AI accompaniment for singer-songwriters.
// Copyright 2026 GhostBand contributors. Licensed under Apache-2.0.
//
// macOS-only. Compiled on the target Mac; see KNOWN_ISSUES.md §1.

#include "SongEditorView.h"

#include "core/ChordParser.h"

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

struct HarmonyChoice {
    core::HarmonySource source;
    const char* label;
    const char* hint;
};

/// Offer order, and the wording the performer reads.
///
/// Guitar Follow is listed and **labelled unbuilt** rather than hidden. It is a real part
/// of the product the brief describes, and a performer who has read about it should find
/// out here that it does not exist yet, rather than concluding the app is missing a feature
/// it never had. Selecting it is allowed; the hint says plainly what will happen, which is
/// nothing.
const HarmonyChoice kHarmonySources[] = {
    {core::HarmonySource::Midi, "Keyboard / MIDI",
     "The band follows the notes you hold. The reliable path."},
    {core::HarmonySource::SongMap, "Song Map (chord chart)",
     "The band follows this section's chords. A section with a tempo advances on its own; "
     "without one it waits for a NEXT CHORD footswitch."},
    {core::HarmonySource::GuitarExperimental, "Guitar Follow - NOT BUILT",
     "EXPERIMENTAL and not implemented. Selecting this leaves the band with no harmony "
     "at all. Phase 3."},
};

struct ChordLengthChoice { const char* label; int beats; };

/// Offered lengths. Includes 3 and 6 because a waltz is a real thing a singer-songwriter
/// writes, and a 4/4-only list would quietly make 3/4 charts impossible.
const ChordLengthChoice kChordLengths[] = {
    {"1 beat",              1},
    {"2 beats (half bar)",  2},
    {"3 beats (bar of 3)",  3},
    {"4 beats (one bar)",   4},
    {"6 beats (two bars of 3)", 6},
    {"8 beats (two bars)",  8},
    {"12 beats",            12},
    {"16 beats (four bars)", 16},
};

int chordLengthIndexFor(int beats) {
    for (int i = 0; i < static_cast<int>(std::size(kChordLengths)); ++i) {
        if (kChordLengths[i].beats == beats) return i;
    }
    // A song file can legitimately hold a value not on this list — the loader clamps to
    // 1..16 but does not snap to these. Showing the nearest is better than showing the
    // first, and the model is left alone until the performer actually picks something.
    int best = 0;
    int best_delta = -1;
    for (int i = 0; i < static_cast<int>(std::size(kChordLengths)); ++i) {
        const int delta = std::abs(kChordLengths[i].beats - beats);
        if (best_delta < 0 || delta < best_delta) { best = i; best_delta = delta; }
    }
    return best;
}

int harmonyIndexFor(core::HarmonySource source) {
    for (int i = 0; i < static_cast<int>(std::size(kHarmonySources)); ++i) {
        if (kHarmonySources[i].source == source) return i;
    }
    return 0;
}

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

    addLabel(harmony_label_, "HARMONY", kKicker);
    addAndMakeVisible(harmony_combo_);
    // Ids are 1-based and map onto kHarmonySources below, not onto the enum's own values —
    // ComboBox reserves 0 for "nothing selected".
    for (int i = 0; i < static_cast<int>(std::size(kHarmonySources)); ++i) {
        harmony_combo_.addItem(kHarmonySources[i].label, i + 1);
    }
    harmony_combo_.onChange = [this] {
        if (updating_) return;
        const int index = harmony_combo_.getSelectedId() - 1;
        if (index < 0 || index >= static_cast<int>(std::size(kHarmonySources))) return;
        editor_.setHarmonySource(kHarmonySources[index].source);
        refresh();
    };
    addLabel(harmony_hint_, "", kDim);

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

    addLabel(intensity_label_, "BAND INTENSITY", kDim);
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

    addLabel(chords_label_, "CHORDS", kKicker);
    addAndMakeVisible(chords_editor_);
    chords_editor_.setMultiLine(false);
    chords_editor_.setTextToShowWhenEmpty("G  D  Em  C", kDim);
    chords_editor_.onTextChange = [this] {
        if (updating_) return;
        editor_.setSectionChords(selectedSection(),
                                 splitChordText(chords_editor_.getText()));
        refresh();
    };
    addLabel(chords_hint_, "", kDim);

    addLabel(tempo_label_, "TEMPO", kKicker);
    addAndMakeVisible(tempo_editor_);
    tempo_editor_.setMultiLine(false);
    tempo_editor_.setTextToShowWhenEmpty("none - advance by footswitch", kDim);
    tempo_editor_.onFocusLost = [this] {
        if (updating_) return;
        const auto text = tempo_editor_.getText().trim();
        if (text.isEmpty()) {
            // Clearing is a musical choice, not a missing value: no tempo puts the chart
            // into Manual, where it moves only when the performer says so.
            editor_.setSectionTempoBpm(selectedSection(), std::nullopt);
        } else {
            // Refused rather than clamped by the model. refresh() then rewrites the field
            // from the model, so a rejected value visibly snaps back instead of sitting
            // there looking accepted.
            editor_.setSectionTempoBpm(selectedSection(), text.getDoubleValue());
        }
        refresh();
    };
    // Commit on Enter too: a performer who types a tempo and immediately clicks SAVE never
    // loses focus, and this is the field where that silently discarded the edit before
    // (KNOWN_ISSUES.md §25 is the same shape).
    tempo_editor_.onReturnKey = [this] { tempo_editor_.onFocusLost(); };
    addLabel(tempo_hint_, "", kDim);

    addLabel(chord_length_label_, "CHORD LENGTH", kKicker);
    addAndMakeVisible(chord_length_combo_);
    for (int i = 0; i < static_cast<int>(std::size(kChordLengths)); ++i) {
        chord_length_combo_.addItem(kChordLengths[i].label, i + 1);
    }
    chord_length_combo_.onChange = [this] {
        if (updating_) return;
        const int index = chord_length_combo_.getSelectedId() - 1;
        if (index < 0 || index >= static_cast<int>(std::size(kChordLengths))) return;
        editor_.setSectionBeatsPerChord(selectedSection(), kChordLengths[index].beats);
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

    const int harmony_index = harmonyIndexFor(song.harmonySource);
    harmony_combo_.setSelectedId(harmony_index + 1, juce::dontSendNotification);
    harmony_hint_.setText(kHarmonySources[harmony_index].hint, juce::dontSendNotification);
    // Unbuilt is not the same as merely non-default, so it gets the fault colour rather
    // than the quiet one.
    harmony_hint_.setColour(juce::Label::textColourId,
                            song.harmonySource == core::HarmonySource::GuitarExperimental
                                ? kFault
                                : kDim);
    updateSongMapVisibility();

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

void SongEditorView::updateSongMapVisibility() {
    // Hidden rather than disabled. A greyed-out chord field on a song that does not read
    // charts is clutter the performer has to reason about every time the screen opens, and
    // the layout reclaims the space when it is not needed.
    const bool song_map = editor_.song().harmonySource == core::HarmonySource::SongMap;
    for (juce::Component* c : {static_cast<juce::Component*>(&chords_label_),
                               static_cast<juce::Component*>(&chords_editor_),
                               static_cast<juce::Component*>(&chords_hint_),
                               static_cast<juce::Component*>(&tempo_label_),
                               static_cast<juce::Component*>(&tempo_editor_),
                               static_cast<juce::Component*>(&tempo_hint_),
                               static_cast<juce::Component*>(&chord_length_label_),
                               static_cast<juce::Component*>(&chord_length_combo_)}) {
        c->setVisible(song_map);
    }
    resized();
}

std::vector<std::string> SongEditorView::splitChordText(const juce::String& text) {
    std::vector<std::string> chords;
    // Whitespace-separated, empties dropped. Nothing else is normalised — the performer's
    // own spelling has to survive the round trip through the file, and "correcting" it
    // here would mean the chart they read back is not the chart they typed.
    for (const auto& token : juce::StringArray::fromTokens(text, " \t\r\n", {})) {
        const auto trimmed = token.trim();
        if (trimmed.isNotEmpty()) chords.push_back(trimmed.toStdString());
    }
    return chords;
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
    chords_editor_.setEnabled(enabled);
    tempo_editor_.setEnabled(enabled);
    chord_length_combo_.setEnabled(enabled);

    if (section == nullptr) {
        section_heading_.setText("SECTION", juce::dontSendNotification);
        name_editor_.setText({}, juce::dontSendNotification);
        prompt_editor_.setText({}, juce::dontSendNotification);
        chords_editor_.setText({}, juce::dontSendNotification);
        tempo_editor_.setText({}, juce::dontSendNotification);
        chords_hint_.setText({}, juce::dontSendNotification);
        tempo_hint_.setText({}, juce::dontSendNotification);
        return;
    }

    // --- Song Map fields ---------------------------------------------------------------
    juce::String chord_text;
    for (const auto& chord : section->chordProgression) {
        if (chord_text.isNotEmpty()) chord_text += "  ";
        chord_text += juce::String(chord);
    }
    if (chords_editor_.getText() != chord_text) {
        chords_editor_.setText(chord_text, juce::dontSendNotification);
    }

    // Which symbols are unreadable, by position. "chord 3 is unreadable" is actionable in
    // a way that "invalid progression" is not, which is why ChordParser reports indices.
    const auto bad = core::unparsableChordIndices(section->chordProgression);
    if (bad.empty()) {
        const int count = static_cast<int>(section->chordProgression.size());
        chords_hint_.setColour(juce::Label::textColourId, kDim);
        chords_hint_.setText(count == 0
                                 ? juce::String("No chords yet. Song Map needs at least one.")
                                 : juce::String(count) + " chord(s), all readable.",
                             juce::dontSendNotification);
    } else {
        juce::String positions;
        for (int i : bad) {
            if (positions.isNotEmpty()) positions += ", ";
            positions += juce::String(i + 1);
        }
        chords_hint_.setColour(juce::Label::textColourId, kFault);
        // Says what will happen, not just that something is wrong: an unreadable chord is
        // skipped and the band holds, which is a specific audible outcome.
        chords_hint_.setText("Cannot read chord " + positions
                                 + " - the band will hold through it.",
                             juce::dontSendNotification);
    }

    const juce::String tempo_text = section->tempoBpm.has_value()
                                        ? juce::String(*section->tempoBpm, 0)
                                        : juce::String();
    if (tempo_editor_.getText() != tempo_text) {
        tempo_editor_.setText(tempo_text, juce::dontSendNotification);
    }
    chord_length_combo_.setSelectedId(chordLengthIndexFor(section->beatsPerChord) + 1,
                                      juce::dontSendNotification);

    tempo_hint_.setColour(juce::Label::textColourId, kDim);
    if (section->tempoBpm.has_value()) {
        // The concrete consequence, in seconds, rather than the abstract setting. "Each
        // chord lasts 2.0 s" is checkable against the song in the performer's head in a
        // way that "4 beats at 120 BPM" is not.
        const double seconds = (60.0 / *section->tempoBpm)
                             * static_cast<double>(section->beatsPerChord);
        tempo_hint_.setText("Each chord lasts " + juce::String(seconds, 1) + " s.",
                            juce::dontSendNotification);
    } else {
        tempo_hint_.setText("No tempo: chords wait for a NEXT CHORD footswitch. 20-300 BPM.",
                            juce::dontSendNotification);
    }

    section_heading_.setText("SECTION " + juce::String(index + 1) + " OF "
                                 + juce::String(editor_.sectionCount()),
                             juce::dontSendNotification);

    // Never overwrite a field that is being typed into. refresh() runs on almost every
    // interaction in this view, and clobbering a half-typed name is the other half of the
    // bug above — the model write was missing, and this line then actively erased the
    // evidence that anything had been typed at all.
    if (!name_editor_.hasKeyboardFocus(false)
        && name_editor_.getText() != juce::String(section->name)) {
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

void SongEditorView::commitPendingEdits() {
    // **This is what "the section name changed back" was.** Type a new section name, click
    // SAVE SONG: focus loss did not fire, so the model still held the old name, saveToDisk
    // wrote the old name, and the refresh() at the end of it then overwrote the field with
    // that old name — the rename disappeared in front of you, and the file on disk never
    // had it.
    //
    // Exactly the same shape as the bug the main prompt field hit on the first real run
    // (see MainComponent's "three ways to apply" comment): a deferred commit with no flush
    // before the read. Fixing it in one place rather than adding a third callback, because
    // the rule is about *reads of the model*, not about any one control.
    if (updating_) return;

    const int index = selectedSection();
    const auto* section = editor_.sectionAt(index);
    if (section == nullptr) return;

    // TEMPO commits on Enter or focus loss for the same reason NAME does — a partially
    // typed "12" would otherwise be refused as out of range on the way to "120". So it
    // needs the same flush before any read of the model, or SAVE would write the old
    // tempo. This is the third control to need it; the rule really is about reads.
    const auto typed_tempo = tempo_editor_.getText().trim();
    const juce::String held_tempo = section->tempoBpm.has_value()
                                        ? juce::String(*section->tempoBpm, 0)
                                        : juce::String();
    if (typed_tempo != held_tempo) {
        if (typed_tempo.isEmpty()) {
            editor_.setSectionTempoBpm(index, std::nullopt);
        } else {
            editor_.setSectionTempoBpm(index, typed_tempo.getDoubleValue());
        }
    }

    const auto typed = name_editor_.getText().toStdString();
    if (typed == section->name) return;

    // A refused rename (empty, or colliding with another section) leaves the model alone;
    // refreshSectionFields() then restores the field to what the model actually holds.
    editor_.setSectionName(index, typed);
}

void SongEditorView::applyToBand() {
    commitPendingEdits();
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
    commitPendingEdits();

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
    // Before isDirty() is consulted, not after: a typed-but-uncommitted rename is a change,
    // and without this the DISCARD CHANGES guard would wave it through as "nothing to lose".
    commitPendingEdits();

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

    auto harmony_row = area.removeFromTop(26);
    harmony_label_.setBounds(harmony_row.removeFromLeft(110));
    harmony_combo_.setBounds(harmony_row.removeFromLeft(260));
    harmony_row.removeFromLeft(12);
    harmony_hint_.setBounds(harmony_row);
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

    // Song Map fields, only when the song reads a chart. Laid out unconditionally — the
    // controls are hidden rather than unbounded, because a hidden control with stale
    // bounds is the shape of bug that put the BAND toggle off-screen once already
    // (KNOWN_ISSUES.md §21), and giving them real bounds costs nothing.
    right.removeFromTop(10);
    chords_label_.setBounds(right.removeFromTop(18));
    chords_editor_.setBounds(right.removeFromTop(28));
    chords_hint_.setBounds(right.removeFromTop(20));

    right.removeFromTop(8);
    auto length_row = right.removeFromTop(26);
    chord_length_label_.setBounds(length_row.removeFromLeft(120));
    chord_length_combo_.setBounds(length_row.removeFromLeft(220));

    right.removeFromTop(6);
    auto tempo_row = right.removeFromTop(26);
    tempo_label_.setBounds(tempo_row.removeFromLeft(120));
    tempo_editor_.setBounds(tempo_row.removeFromLeft(100));
    tempo_row.removeFromLeft(12);
    tempo_hint_.setBounds(tempo_row);
}

} // namespace ghostband::app
