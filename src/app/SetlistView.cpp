// GhostBand — live AI accompaniment for singer-songwriters.
// Copyright 2026 GhostBand contributors. Licensed under Apache-2.0.

#include "SetlistView.h"

#include "StagePalette.h"
#include "StageType.h"

#include "core/Persistence.h"

namespace ghostband::app {

namespace {
// Canvas measurements. The SETLISTS screen is `grid-template-columns: 1fr 380px` with a
// hairline between, padded `clamp(22px, 4vh, 44px) clamp(22px, 3vw, 44px)`.
constexpr int kDetailsWidth = 380;
constexpr int kPad = 36;
constexpr int kRowHeight = 58;
/// The canvas's `62px` number column.
constexpr int kNumberWidth = 62;
constexpr int kStatusWidth = 120;

// ASCII only, as everywhere else in this UI — see the note at the top of MainComponent.cpp.
constexpr const char* kReorderHint = "SELECT A SONG, THEN MOVE UP OR MOVE DOWN";
} // namespace

SetlistView::SetlistView(GhostBandAudioEngine& engine) : engine_(engine) {
    addAndMakeVisible(name_editor_);
    name_editor_.setFont(displayFont(28.0f, 0.02f));
    // Deferred commit, like the Song Editor's NAME field, and flushed the same way. Typing
    // per keystroke is fine here (no uniqueness rule), but the undo stack would then hold
    // one entry per character, which makes UNDO useless for anything else.
    name_editor_.onReturnKey = [this] { commitPendingEdits(); refresh(); };
    name_editor_.onFocusLost = [this] { commitPendingEdits(); refresh(); };

    addAndMakeVisible(song_list_);
    song_list_.setModel(this);
    song_list_.setRowHeight(kRowHeight);
    song_list_.setColour(juce::ListBox::backgroundColourId, kBackground);
    song_list_.setColour(juce::ListBox::outlineColourId, juce::Colour{0x00000000});

    addAndMakeVisible(perform_button_);
    perform_button_.getProperties().set(prop::kPrimary, true);
    perform_button_.onClick = [this] {
        commitPendingEdits();
        loadIntoBand();
        // Only enter the stage screen if the set actually reached the band; otherwise the
        // performer lands on a screen with nothing behind it.
        if (engine_.hasSetlist() && onPerformRequested) onPerformRequested();
    };

    addAndMakeVisible(add_button_);
    add_button_.onClick = [this] { addSong(); };

    addAndMakeVisible(remove_button_);
    remove_button_.onClick = [this] { removeSelected(); };

    addAndMakeVisible(up_button_);
    up_button_.onClick = [this] { moveSelected(-1); };

    addAndMakeVisible(down_button_);
    down_button_.onClick = [this] { moveSelected(1); };

    addAndMakeVisible(undo_button_);
    undo_button_.onClick = [this] {
        if (editor_.undo()) refresh();
    };

    addAndMakeVisible(save_button_);
    save_button_.onClick = [this] { saveToDisk(); };

    addAndMakeVisible(load_button_);
    load_button_.onClick = [this] { commitPendingEdits(); loadIntoBand(); };

    addAndMakeVisible(close_button_);
    close_button_.onClick = [this] { requestClose(); };

    addAndMakeVisible(status_label_);
    status_label_.setFont(labelFont(11.0f));
    status_label_.setColour(juce::Label::textColourId, kKicker);

    beginEditing(core::Setlist{});
}

SetlistView::~SetlistView() { song_list_.setModel(nullptr); }

// --- Opening -----------------------------------------------------------------------------

void SetlistView::beginEditing(core::Setlist list) {
    editor_.reset(std::move(list));
    confirming_discard_ = false;
    close_button_.setButtonText("DONE");
    status_label_.setText({}, juce::dontSendNotification);
    refresh();
}

void SetlistView::beginEditingCurrent() {
    // Always start from what is loaded, rather than whatever was left here last time —
    // the same reasoning as the Song Editor: editing a stale copy and saving it would
    // silently overwrite the current set.
    beginEditing(engine_.hasSetlist() ? engine_.setlist().setlist() : core::Setlist{});
}

// --- Model reads -------------------------------------------------------------------------

void SetlistView::commitPendingEdits() {
    if (updating_) return;
    const auto typed = name_editor_.getText().toStdString();
    if (typed == editor_.setlist().name) return;
    editor_.setName(typed);
}

bool SetlistView::isMissing(int index) const {
    const auto* entry = editor_.entryAt(index);
    if (entry == nullptr) return false;

    for (const auto& song : available_) {
        if (song.file == juce::String(entry->songFile)) return false;
    }
    return true;
}

// --- Refresh -----------------------------------------------------------------------------

void SetlistView::refresh() {
    // **Refuse re-entry.** `updating_` was being set here and checked only by
    // commitPendingEdits, which left the important path open: refresh() calls
    // ListBox::updateContent(), updateContent() can move or clear the selection when the
    // row count changes, moving the selection fires selectedRowsChanged, and
    // selectedRowsChanged called straight back into refresh(). Adding the first song to an
    // empty set is exactly the 0 -> 1 row-count change that moves the selection, so it
    // recursed until the stack ran out. That was the crash.
    //
    // A ScopedValueSetter alone could not have prevented it — the guard has to be *read*
    // on the way in, not merely raised on the way through.
    if (updating_) return;
    const juce::ScopedValueSetter<bool> guard(updating_, true);

    // Re-read the directory: a song can be deleted while this screen is open, and a set
    // that quietly still shows it as present is the failure this screen exists to prevent.
    available_ = engine_.availableSongs();

    if (!name_editor_.hasKeyboardFocus(false)
        && name_editor_.getText() != juce::String(editor_.setlist().name)) {
        name_editor_.setText(editor_.setlist().name, juce::dontSendNotification);
    }

    song_list_.updateContent();
    song_list_.repaint();

    refreshSelectionUi();
    undo_button_.setEnabled(editor_.canUndo());

    // PERFORM SET and SAVE SET both need a set that can actually be performed. An empty
    // set is a normal step in building one, so this disables rather than complains.
    save_button_.setEnabled(editor_.isPerformable());
    perform_button_.setEnabled(editor_.isPerformable());
    load_button_.setEnabled(editor_.isPerformable());
    save_button_.setButtonText(editor_.isDirty() ? "SAVE SET *" : "SAVE SET");

    close_button_.setButtonText(confirming_discard_ ? "DISCARD CHANGES?" : "DONE");
    repaint();
}

void SetlistView::refreshDetails() {
    details_.clear();
    detail_title_ = {};

    const int selected = song_list_.getSelectedRow();
    const auto* entry = editor_.entryAt(selected);
    if (entry == nullptr) {
        detail_title_ = editor_.isEmpty() ? "NO SONGS YET" : "NOTHING SELECTED";
        return;
    }

    detail_title_ = entry->cachedTitle;
    details_.push_back({"POSITION", juce::String(selected + 1) + " of "
                                        + juce::String(editor_.size())});
    details_.push_back({"FILE", juce::String(entry->songFile)});

    if (isMissing(selected)) {
        // Everything below this point would be read from a file that is not there, so say
        // so and stop rather than showing blanks that look like real answers.
        details_.push_back({"STATUS", "MISSING from the songs folder"});
        return;
    }

    // Read the song for the details the canvas's STAGE NOTES panel shows. Done here rather
    // than cached because the song may have been edited since the set was built — which is
    // exactly why SetlistEntry references a file instead of embedding a copy.
    const auto file = GhostBandAudioEngine::songsDirectory()
                          .getChildFile(juce::String(entry->songFile));
    core::Song song;
    const auto result = core::deserialiseSong(file.loadFileAsString().toStdString(), song);
    if (!result.ok) {
        details_.push_back({"STATUS", "Will not open: " + juce::String(result.message)});
        return;
    }

    details_.push_back({"STATUS", "Ready"});
    details_.push_back({"SECTIONS", juce::String(static_cast<int>(song.sections.size()))});

    juce::String names;
    for (const auto& section : song.sections) {
        if (names.isNotEmpty()) names += "  ";
        names += juce::String(section.name);
    }
    details_.push_back({"ORDER", names});
    details_.push_back({"PROMPT", juce::String(song.defaultStylePrompt)});

    // The prompt-slot ceiling, surfaced where it can still be fixed. Same reasoning as the
    // Song Editor's warning: the performer should learn a song may stall before the gig.
    if (!song.fitsPromptSlots()) {
        details_.push_back({"WARNING", "More distinct prompts than MRT2 has slots - a "
                                       "section change in this song may stall."});
    }
}

// --- ListBoxModel ------------------------------------------------------------------------

int SetlistView::getNumRows() { return editor_.size(); }

void SetlistView::paintListBoxItem(int row, juce::Graphics& g, int width, int height,
                                   bool rowIsSelected) {
    const auto* entry = editor_.entryAt(row);
    if (entry == nullptr) return;

    auto bounds = juce::Rectangle<int>(0, 0, width, height);

    // A selected row is a bordered card; an unselected one is bare ground with a hairline.
    // The canvas draws every row with a border, but at eight rows on black that becomes a
    // grid, and the selection then has nothing left to say.
    if (rowIsSelected) {
        drawCard(g, bounds.reduced(0, 1), 10.0f, kCard, kControlBorderHover);
    } else {
        drawHairline(g, bounds);
    }

    auto cells = bounds.reduced(16, 0);
    const auto number_cell = cells.removeFromLeft(kNumberWidth);
    const auto status_cell = cells.removeFromRight(kStatusWidth);

    const bool missing = isMissing(row);

    g.setColour(kKicker);
    g.setFont(displayFont(20.0f, 0.02f));
    g.drawText(juce::String(row + 1), number_cell, juce::Justification::centredLeft);

    g.setColour(missing ? kFault : (rowIsSelected ? kBright : kHeading));
    g.setFont(displayFont(24.0f, 0.04f));
    g.drawText(entry->cachedTitle, cells, juce::Justification::centredLeft);

    if (missing) {
        drawStatusBlock(g, {status_cell.getX() + 4, bounds.getCentreY()}, kFault);
        drawTrackedCaps(g, "MISSING", status_cell.withTrimmedLeft(18), kFault, 11.0f, 0.16f);
    }
}

void SetlistView::selectedRowsChanged(int) {
    // Deliberately not refresh(). Moving the selection changes which buttons apply and
    // what the details panel shows; it does not change what is on disk, and a full
    // refresh() re-read the songs directory on every arrow key. Scanning the filesystem
    // once per keypress is the kind of thing that is invisible at a desk with three songs
    // and audible at a gig with forty.
    refreshSelectionUi();
    repaint();
}

void SetlistView::refreshSelectionUi() {
    const int selected = song_list_.getSelectedRow();
    const bool has_selection = selected >= 0 && selected < editor_.size();

    remove_button_.setEnabled(has_selection);
    up_button_.setEnabled(has_selection && selected > 0);
    down_button_.setEnabled(has_selection && selected < editor_.size() - 1);

    refreshDetails();
}

// --- Actions -----------------------------------------------------------------------------

void SetlistView::addSong() {
    commitPendingEdits();
    available_ = engine_.availableSongs();

    if (available_.empty()) {
        showMessage("No songs in the songs folder yet. Save a song first.", kWarn);
        return;
    }

    juce::PopupMenu menu;
    for (int i = 0; i < static_cast<int>(available_.size()); ++i) {
        const auto& song = available_[static_cast<std::size_t>(i)];
        // Damaged songs are listed but not selectable: putting one in a set would build a
        // running order with a hole in it that only shows up on the night.
        menu.addItem(i + 1, song.title + (song.readable ? "" : "   (will not open)"),
                     song.readable, false);
    }

    // SafePointer, not a bare `this`: the callback outlives the click, and a menu left open
    // while the app shuts down would otherwise call into a destroyed view. Cheap insurance
    // against the one crash class that would happen on stage rather than at a desk.
    juce::Component::SafePointer<SetlistView> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(add_button_),
                       [this, safe](int choice) {
                           if (safe == nullptr) return;
                           if (choice <= 0) return;
                           const auto& song = available_[static_cast<std::size_t>(choice - 1)];
                           const int index = editor_.addSong(song.file.toStdString(),
                                                             song.title.toStdString());
                           if (index < 0) {
                               showMessage("Could not add that song.", kFault);
                               return;
                           }
                           refresh();
                           song_list_.selectRow(index);
                           showMessage("Added " + song.title + ". Not saved yet.", kOk);
                       });
}

void SetlistView::removeSelected() {
    commitPendingEdits();
    const int selected = song_list_.getSelectedRow();
    if (!editor_.removeEntry(selected)) return;

    refresh();
    // Keep a sensible selection rather than dropping to none: removing three songs in a row
    // should not mean re-selecting between each.
    if (editor_.size() > 0) {
        song_list_.selectRow(juce::jmin(selected, editor_.size() - 1));
    }
    showMessage("Removed. UNDO puts it back.", kMuted);
}

void SetlistView::moveSelected(int delta) {
    commitPendingEdits();
    const int from = song_list_.getSelectedRow();
    if (!editor_.moveEntry(from, from + delta)) return;   // refuses at the ends

    refresh();
    // Follow the song, not the row number — the performer is moving *this* song, and a
    // selection that stayed put would make the second click move a different one.
    song_list_.selectRow(from + delta);
}

void SetlistView::saveToDisk() {
    commitPendingEdits();

    juce::File written;
    const auto error = engine_.saveSetlist(editor_.setlist(), written);
    if (error.isNotEmpty()) {
        showMessage(error, kFault);
        return;
    }

    editor_.markSaved();
    showMessage("Saved " + written.getFileName(), kOk);
    refresh();
}

void SetlistView::loadIntoBand() {
    // Save first, then load from disk: loadSetlistFile is the only path that resolves each
    // entry's file and reports the gaps, and it is the same path a set opened at soundcheck
    // takes. Loading through a second, in-memory route would mean the two could diverge.
    juce::File written;
    const auto error = engine_.saveSetlist(editor_.setlist(), written);
    if (error.isNotEmpty()) {
        showMessage(error, kFault);
        return;
    }
    editor_.markSaved();

    const auto load_error = engine_.loadSetlistFile(written);
    if (load_error.isNotEmpty()) {
        showMessage(load_error, kFault);
        return;
    }

    const auto missing = engine_.missingSongs();
    if (!missing.empty()) {
        juce::String names;
        for (const auto& title : missing) {
            if (names.isNotEmpty()) names += ", ";
            names += juce::String(title);
        }
        showMessage(juce::String(static_cast<int>(missing.size()))
                        + " song(s) MISSING: " + names,
                    kFault);
    } else {
        showMessage("Loaded into the band - " + juce::String(engine_.setlist().size())
                        + " songs.",
                    kOk);
    }
    refresh();
}

void SetlistView::requestClose() {
    // Before isDirty() is consulted: a typed-but-uncommitted name is a change, and without
    // this the guard would wave it through as "nothing to lose" (KNOWN_ISSUES.md §25).
    commitPendingEdits();

    if (editor_.isDirty() && !confirming_discard_) {
        confirming_discard_ = true;
        close_button_.setButtonText("DISCARD CHANGES?");
        showMessage("Unsaved changes. Press again to discard, or SAVE SET.", kWarn);
        return;
    }
    confirming_discard_ = false;
    if (onCloseRequested) onCloseRequested();
}

void SetlistView::showMessage(const juce::String& text, juce::Colour colour) {
    status_label_.setColour(juce::Label::textColourId, colour);
    status_label_.setText(text, juce::dontSendNotification);
}

// --- Painting ----------------------------------------------------------------------------

void SetlistView::paint(juce::Graphics& g) {
    g.fillAll(kBackground);

    // The hairline between the two columns, and nothing else structural. The canvas holds
    // this whole screen together on one-pixel rules.
    g.setColour(kHairline);
    g.fillRect(getWidth() - kDetailsWidth, 0, 1, getHeight());

    drawTrackedCaps(g, kReorderHint, hint_area_, kKicker, 11.0f, 0.14f);

    // --- right column: the canvas's STAGE NOTES panel ---
    auto area = details_area_;
    drawTrackedCaps(g, "STAGE NOTES", area.removeFromTop(16), kKicker, 11.0f, 0.28f);
    area.removeFromTop(12);

    g.setColour(kText);
    g.setFont(displayFont(30.0f, 0.02f));
    g.drawText(detail_title_, area.removeFromTop(34), juce::Justification::topLeft);
    area.removeFromTop(18);

    for (const auto& row : details_) {
        // Height per row is measured from the value so a long prompt or a warning wraps
        // instead of being clipped — these are the rows most worth reading in full.
        const auto font = valueFont(13.0f);
        const int text_width = juce::jmax(40, area.getWidth() - 86);
        const int measured = juce::GlyphArrangement::getStringWidthInt(font, row.value);
        const int lines = juce::jmax(1, (measured + text_width - 1) / text_width);
        const int height = juce::jmax(30, lines * 18 + 12);

        auto row_area = area.removeFromTop(height);
        if (row_area.getHeight() <= 0) break;   // ran out of column; stop rather than overlap

        drawHairline(g, row_area, juce::Colour{0xff1b1e22});

        auto cells = row_area.reduced(0, 6);
        const auto label_cell = cells.removeFromLeft(78);
        drawTrackedCaps(g, row.label, label_cell.withHeight(16), kKicker, 10.0f, 0.22f);

        g.setColour(row.label == "WARNING" ? kWarn : kMuted);
        g.setFont(font);
        g.drawFittedText(row.value, cells.withTrimmedLeft(8), juce::Justification::topLeft,
                         lines + 1);
    }
}

// --- Layout ------------------------------------------------------------------------------

void SetlistView::resized() {
    auto area = getLocalBounds();

    auto details = area.removeFromRight(kDetailsWidth).reduced(kPad, kPad);
    details_area_ = details;

    auto left = area.reduced(kPad, kPad);

    // Header: the set name, editable in place, in display type.
    title_area_ = left.removeFromTop(44);
    name_editor_.setBounds(title_area_);
    left.removeFromTop(10);

    hint_area_ = left.removeFromTop(16);
    left.removeFromTop(16);

    // Footer first, so the list gets exactly what is left and can never overlap the
    // controls — the failure mode `layoutRow` exists to prevent, in vertical form.
    auto footer = left.removeFromBottom(44);
    close_button_.setBounds(footer.removeFromRight(110).reduced(2));
    footer.removeFromRight(8);
    save_button_.setBounds(footer.removeFromRight(130).reduced(2));
    footer.removeFromRight(8);
    load_button_.setBounds(footer.removeFromRight(160).reduced(2));

    left.removeFromBottom(10);
    auto actions = left.removeFromBottom(44);
    perform_button_.setBounds(actions.removeFromLeft(200).reduced(2));
    actions.removeFromLeft(10);
    add_button_.setBounds(actions.removeFromLeft(130).reduced(2));
    actions.removeFromLeft(6);
    up_button_.setBounds(actions.removeFromLeft(120).reduced(2));
    actions.removeFromLeft(6);
    down_button_.setBounds(actions.removeFromLeft(130).reduced(2));
    actions.removeFromLeft(6);
    remove_button_.setBounds(actions.removeFromLeft(110).reduced(2));
    actions.removeFromLeft(6);
    undo_button_.setBounds(actions.removeFromLeft(90).reduced(2));

    left.removeFromBottom(8);
    status_label_.setBounds(left.removeFromBottom(20));
    left.removeFromBottom(8);

    song_list_.setBounds(left);
}

} // namespace ghostband::app
