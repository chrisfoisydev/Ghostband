// GhostBand — live AI accompaniment for singer-songwriters.
// Copyright 2026 GhostBand contributors. Licensed under Apache-2.0.
//
// macOS-only. Compiled on the target Mac; see KNOWN_ISSUES.md §1.

#pragma once

#include "GhostBandAudioEngine.h"
#include "core/SongEditor.h"

#include <juce_gui_extra/juce_gui_extra.h>

#include <functional>
#include <memory>

namespace ghostband::app {

/// Authoring screen: the section list on the left, the selected section's fields on the
/// right, song-level fields above both.
///
/// **Nothing here touches the band until asked.** `core::SongEditor` holds its own copy of
/// the song; APPLY TO BAND pushes it to the engine and SAVE writes it to disk, and they are
/// separate because they answer different questions — "does this sound right" and "keep
/// this". Editing live would mean a half-typed prompt reaching MRT2 on every keystroke.
///
/// Warnings are shown while editing rather than at save time. A song whose prompts overflow
/// MRT2's slots will stall on some section change (`KNOWN_ISSUES.md` §4); the performer has
/// to learn that here, where rewording still fixes it, not on stage.
class SongEditorView : public juce::Component,
                       private juce::ListBoxModel {
public:
    explicit SongEditorView(GhostBandAudioEngine& engine);
    ~SongEditorView() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    /// Load a song into the editor. Called when the screen is opened, so it always edits
    /// what is currently loaded rather than whatever was last left here.
    void beginEditing(const core::Song& song);

    std::function<void()> onCloseRequested;

private:
    // juce::ListBoxModel
    int getNumRows() override;
    void paintListBoxItem(int row, juce::Graphics& g, int width, int height,
                          bool selected) override;
    void selectedRowsChanged(int lastRowSelected) override;

    /// Rewrite every control from the editor's song. Called after any change, so the
    /// screen can never disagree with the model it is showing.
    void refresh();

    /// Flush the NAME field into the model.
    ///
    /// NAME commits on Enter or focus loss rather than per keystroke — deliberately, since
    /// the uniqueness rule would rename a section to "Vers 2" while "Verse" is still being
    /// typed. The cost is that typed text is not in the model until one of those happens,
    /// and clicking a button does not reliably move keyboard focus. So every action that
    /// *reads* the model has to flush first.
    void commitPendingEdits();
    void refreshSectionFields();
    int selectedSection() const;
    void applyToBand();
    void saveToDisk();
    void requestClose();

    GhostBandAudioEngine& engine_;
    core::SongEditor editor_;

    juce::Label heading_;

    juce::Label title_label_;
    juce::TextEditor title_editor_;
    juce::Label default_prompt_label_;
    juce::TextEditor default_prompt_editor_;

    juce::ListBox section_list_;
    juce::TextButton add_button_{"ADD"};
    juce::TextButton duplicate_button_{"DUPLICATE"};
    juce::TextButton remove_button_{"REMOVE"};
    juce::TextButton up_button_{"MOVE UP"};
    juce::TextButton down_button_{"MOVE DOWN"};

    juce::Label section_heading_;
    juce::Label name_label_;
    juce::TextEditor name_editor_;
    juce::Label prompt_label_;
    juce::TextEditor prompt_editor_;
    juce::Label prompt_hint_;
    juce::Label intensity_label_;
    juce::Slider intensity_slider_;
    juce::ToggleButton ai_enabled_toggle_{"BAND PLAYS IN THIS SECTION"};
    juce::Label transition_label_;
    juce::ComboBox transition_combo_;

    juce::Label validation_label_;
    juce::Label slots_label_;

    juce::TextButton undo_button_{"UNDO"};
    juce::TextButton apply_button_{"APPLY TO BAND"};
    juce::TextButton save_button_{"SAVE SONG"};
    juce::TextButton close_button_{"DONE"};
    juce::Label file_status_label_;

    /// DONE with unsaved changes asks once by turning into DISCARD CHANGES?, rather than
    /// opening a modal dialog. Fewer moving parts, and no modal window in an app whose
    /// whole design avoids them.
    bool confirming_discard_ = false;

    /// Guards the editor callbacks while refresh() is writing into the controls, so
    /// setText does not read back as a user edit and push a spurious undo entry.
    bool updating_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SongEditorView)
};

} // namespace ghostband::app
