// GhostBand — live AI accompaniment for singer-songwriters.
// Copyright 2026 GhostBand contributors. Licensed under Apache-2.0.
//
// ⚠️ macOS-only. Builds and runs on macOS/Apple Silicon; cannot be compiled on
// this Linux development machine at all. See KNOWN_ISSUES.md §1 — which means any change
// here is unverified until someone builds it on the Mac.

#pragma once

#include "GhostBandAudioEngine.h"
#include "StageChrome.h"

#include "core/SetlistEditor.h"

#include <juce_gui_extra/juce_gui_extra.h>

#include <functional>
#include <vector>

namespace ghostband::app {

/// Building a set: the design canvas's SETLISTS screen.
///
/// Layout is the canvas's `grid-template-columns: 1fr 380px` — the running order on the
/// left, details of the selected song on the right, one hairline between them.
///
/// ## Where this departs from the canvas, and why
///
/// - **Reorder is buttons, not drag.** The canvas says "DRAG A SONG TO REORDER". Drag is
///   implementable in JUCE, but a set is reordered at soundcheck on a laptop trackpad, and
///   a mis-drag that silently drops a song two places away is worse than two clicks. The
///   hint line says what is true rather than what the canvas says.
/// - **No song length column.** The canvas shows `{{ t.len }}` per song and a set length in
///   the header. GhostBand does not know how long a song is — sections have no duration,
///   because the performer decides when a section ends. A plausible-looking "4:12" would be
///   invented, which `CLAUDE.md` rule 2 forbids.
/// - **No venue or start time.** Same reason; that is HOME's data and it does not exist.
/// - **MISSING is shown in fault red**, which the canvas does not depict. A set referencing
///   a song that is no longer on disk is the single most important thing this screen can
///   tell you, and it must be visible before the gig rather than between songs.
///
/// All four are recorded in `docs/DESIGN_GAP_ANALYSIS.md`.
///
/// Editing goes through `core::SetlistEditor`, which owns undo and the refuse-don't-clamp
/// rules and is tested on Linux. This class is presentation and file I/O only.
class SetlistView : public juce::Component,
                    private juce::ListBoxModel {
public:
    explicit SetlistView(GhostBandAudioEngine& engine);
    ~SetlistView() override;

    std::function<void()> onCloseRequested;
    /// Raised when the performer asks to run the set. The host enters Performance Mode;
    /// this view does not know about screens.
    std::function<void()> onPerformRequested;

    /// Start editing a copy of `list`. Called when the screen opens, so the editor never
    /// holds a stale set from last time.
    void beginEditing(core::Setlist list);
    /// The set currently loaded in the engine, or an empty one if there is none.
    void beginEditingCurrent();

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    // juce::ListBoxModel
    int getNumRows() override;
    void paintListBoxItem(int row, juce::Graphics&, int width, int height,
                          bool rowIsSelected) override;
    void selectedRowsChanged(int lastRowSelected) override;

    void refresh();
    void refreshDetails();

    /// Flush the name field into the model before anything reads it. Same rule as the Song
    /// Editor's `commitPendingEdits` — see KNOWN_ISSUES.md §25, where a deferred commit
    /// with no flush before the read silently discarded a rename.
    void commitPendingEdits();

    void addSong();
    void removeSelected();
    void moveSelected(int delta);
    void saveToDisk();
    void loadIntoBand();
    void requestClose();

    /// True when the entry's file is not on disk. Recomputed on refresh rather than cached,
    /// because a song can be deleted while this screen is open.
    bool isMissing(int index) const;

    void showMessage(const juce::String& text, juce::Colour colour);

    GhostBandAudioEngine& engine_;
    core::SetlistEditor editor_;

    /// Song files present on disk, refreshed whenever the set changes. Used both for the
    /// ADD SONG menu and to mark missing entries.
    std::vector<GhostBandAudioEngine::SongOnDisk> available_;

    juce::TextEditor name_editor_;
    juce::ListBox song_list_;

    juce::TextButton perform_button_{"PERFORM SET"};
    juce::TextButton add_button_{"ADD SONG"};
    juce::TextButton remove_button_{"REMOVE"};
    juce::TextButton up_button_{"MOVE UP"};
    juce::TextButton down_button_{"MOVE DOWN"};
    juce::TextButton undo_button_{"UNDO"};
    juce::TextButton save_button_{"SAVE SET"};
    juce::TextButton load_button_{"LOAD INTO BAND"};
    juce::TextButton close_button_{"DONE"};

    juce::Label status_label_;

    /// Right-hand column. Plain text rather than controls: this is the canvas's STAGE NOTES
    /// panel, which describes the selected song and does not edit it.
    struct DetailRow {
        juce::String label;
        juce::String value;
    };
    std::vector<DetailRow> details_;
    juce::String detail_title_;

    /// Areas filled in by resized() and drawn by paint(), so the two cannot disagree.
    juce::Rectangle<int> title_area_;
    juce::Rectangle<int> hint_area_;
    juce::Rectangle<int> details_area_;

    /// Two-click confirm on DONE with unsaved changes, matching the Song Editor. A modal
    /// dialog is not worth adding to an app that otherwise has none.
    bool confirming_discard_ = false;

    /// Guards the callbacks while refresh() writes into the controls.
    bool updating_ = false;


    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SetlistView)
};

} // namespace ghostband::app
