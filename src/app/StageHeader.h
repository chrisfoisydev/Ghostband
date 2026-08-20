// GhostBand — live AI accompaniment for singer-songwriters.
// Copyright 2026 GhostBand contributors. Licensed under Apache-2.0.
//
// ⚠️ macOS-only, NEVER COMPILED as of this commit. See KNOWN_ISSUES.md §1.

#pragma once

#include "StageChrome.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

namespace ghostband::app {

/// The 68px application bar from the design canvas: wordmark, screen nav, status lamps.
///
/// **This is the piece whose absence made two design passes look like no design pass.**
/// The canvas is not a styled version of a button stack — it is a shell with persistent
/// chrome and screens hung inside it. Restyling the button stack could never converge on
/// that, however correct the colours got.
///
/// ## What is here and what is deliberately not
///
/// The canvas's nav reads `HOME · SONGS · SETLISTS · PERFORM · SETUP`. GhostBand ships
/// four of those five, because `CLAUDE.md` rule 2 forbids a control that looks live and
/// does nothing:
///
/// - **HOME** is omitted. Its card wants a venue, a set time, a set length and a
///   recently-played list. GhostBand tracks none of those, and a HOME reading
///   "FRIDAY NIGHT SET / THE FOLD, 9:30PM" would be a screen of invented facts.
/// - **SETLISTS** is omitted. Setlists load and advance (`SetlistController` is real and
///   tested), but there is no setlist *screen* — no reordering, no building, no saving a
///   set. A nav entry leading to a list you cannot edit promises an editor.
/// - **PEDAL** is added, because foot control is a real screen and the canvas reaches it
///   from onboarding rather than from the bar.
///
/// Both omissions are recorded in `docs/DESIGN_GAP_ANALYSIS.md` §1 with what each would
/// need. They are gaps in the app, not in the design.
///
/// The three status lamps on the right are the canvas's, unchanged: AUDIO, BAND, PEDAL.
/// Each is driven by real state — none of them is decorative.
class StageHeader : public juce::Component {
public:
    /// Screens reachable from the bar. Order is display order.
    enum class Screen { Setup, Songs, Pedal, Perform };

    StageHeader();

    /// Fired when a nav entry is clicked. Never fired for a disabled entry.
    std::function<void(Screen)> onNavigate;

    /// Which entry is lit. Cheap enough to call on every refresh; repaints only on change.
    void setActiveScreen(Screen screen);

    /// PERFORM is disabled until a song exists — an empty stage screen would promise a set
    /// the app cannot run. Disabled entries are drawn at a third alpha and do not respond.
    void setScreenEnabled(Screen screen, bool enabled);

    /// The three lamps. Colours come from the semantic palette, so "green" means the same
    /// thing here as it does on the stage screen.
    void setLampColours(juce::Colour audio, juce::Colour band, juce::Colour pedal);

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;

    /// Height in the canvas, and the one number other views need in order to sit beneath it.
    static constexpr int kHeight = 68;

private:
    struct NavItem {
        Screen screen;
        juce::String text;
        bool enabled = true;
        juce::Rectangle<int> bounds;   // recomputed in resized()
    };

    /// Hit-test target under `p`, or nullptr. Const because both the click and the hover
    /// path need it and neither should be able to move anything.
    const NavItem* itemAt(juce::Point<int> p) const;

    std::vector<NavItem> items_;
    Screen active_ = Screen::Setup;
    int hovered_ = -1;

    juce::Colour lamp_audio_ = kKicker;
    juce::Colour lamp_band_ = kKicker;
    juce::Colour lamp_pedal_ = kKicker;

    /// Wordmark advance, measured once per resize so paint() does not re-measure glyphs at
    /// every repaint. The nav starts 40px past it, matching the canvas's `gap: 40px`.
    float wordmark_width_ = 150.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StageHeader)
};

} // namespace ghostband::app
