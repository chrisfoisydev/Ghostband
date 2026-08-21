// GhostBand — live AI accompaniment for singer-songwriters.
// Copyright 2026 GhostBand contributors. Licensed under Apache-2.0.

#pragma once

#include "core/Song.h"

#include <string>
#include <vector>

namespace ghostband::core {

/// How the map moves from one chord to the next.
enum class SongMapAdvance {
    /// Only when the performer says so — a footswitch, or a section change. This is the
    /// state a section is in when it has no tempo, and it is not a degraded mode.
    Manual,
    /// On a tempo clock, running from the moment the section started.
    Clock,
};

const char* toString(SongMapAdvance a) noexcept;

/// Walks a section's chord progression so the band can be steered from the chart rather
/// than from what the performer is holding.
///
/// ## The tension this class is built around
///
/// `CLAUDE.md`: *the musician leads; the AI is additive.* A chord map that runs on its own
/// clock inverts that — the app starts keeping time and the performer starts following it.
/// That is the opposite of the product, and it is very easy to build by accident.
///
/// Three rules keep the performer in front:
///
/// 1. **No tempo means no clock.** A section with no `tempoBpm` is `Manual`, and the map
///    moves only when something asks it to. GhostBand never invents a tempo — a default of
///    120 BPM here would be the app deciding how fast the song goes.
/// 2. **The clock starts when the performer starts the section**, not on a global
///    transport. There is no bar-line the performer has to hit; there is only "from here".
/// 3. **Explicit moves always work**, in either mode, and re-phase the clock. If the map
///    has drifted from where the singer actually is, one press puts it back rather than
///    requiring them to wait for it to catch up.
///
/// ## Chords that do not parse
///
/// A rejected symbol keeps its slot and yields **no notes**, so the caller simply does not
/// push a harmony change there and the band holds what it was playing. The alternative —
/// dropping the slot — would shift every chord after the typo earlier, putting the whole
/// rest of the section out of step with the chart the singer is reading. One held chord is
/// a much smaller failure than a progression that is silently off by one, and the editor
/// flags the bad symbol long before the gig.
///
/// Nothing here talks to MRT2 or to JUCE; it converts time into chord indices and note
/// numbers, which is what makes it testable off-Mac.
class SongMapPlayer {
public:
    struct Config {
        /// Beats each chord is held for. One bar of 4/4.
        ///
        /// Not persisted per section yet, so a chart where chords last two bars cannot be
        /// expressed. Adding it is a schema change, and `Persistence` has the migration
        /// machinery for exactly that — it is a deliberate follow-up, not an oversight.
        int beatsPerChord = 4;
        /// Where chords are voiced. See ChordParser.h on absolute placement.
        int octave = 4;
    };

    SongMapPlayer() = default;
    explicit SongMapPlayer(Config config) noexcept : config_(config) {}

    void setConfig(Config config) noexcept;
    const Config& config() const noexcept { return config_; }

    /// Load a section's progression, parsing every symbol. Leaves the player stopped and
    /// positioned at the first chord.
    ///
    /// @return false when the section has no chord that parses, in which case the player
    ///         is inactive and `currentNotes()` is empty. A Song Map section with nothing
    ///         playable must steer nothing at all rather than steer badly.
    bool loadSection(const SongSection& section);

    /// Indices into the section's progression whose symbols did not parse.
    const std::vector<int>& rejectedIndices() const noexcept { return rejected_; }
    bool hasRejectedChords() const noexcept { return !rejected_.empty(); }

    /// True when at least one symbol parsed, so the map has something to steer with.
    bool isActive() const noexcept { return active_; }

    void start() noexcept;
    void stop() noexcept;
    bool isRunning() const noexcept { return running_; }

    SongMapAdvance advanceMode() const noexcept { return mode_; }
    /// Milliseconds each chord is held. Zero in Manual mode.
    double msPerChord() const noexcept;

    /// Advance the clock by `deltaMs`. No-op unless running in `Clock` mode.
    /// @return true when the chord changed, so the caller knows to push new notes.
    bool tick(double deltaMs) noexcept;

    /// Explicit moves. Available in both modes; both re-phase the clock so the new chord
    /// gets its full duration rather than the remainder of the old one.
    /// @return true when the position changed.
    bool advance() noexcept;
    bool retreat() noexcept;
    /// Back to the first chord, clock re-phased. What a section change should call.
    void restart() noexcept;

    int index() const noexcept { return index_; }
    int size() const noexcept { return static_cast<int>(slots_.size()); }

    /// The symbol as the performer typed it. Empty when there is nothing loaded.
    const std::string& currentSymbol() const;
    /// The one after it, wrapping — the stage screen shows what is coming.
    const std::string& nextSymbol() const;

    /// Notes for the current chord, or empty when the current slot did not parse.
    const std::vector<int>& currentNotes() const;

    /// How far through the current chord, 0..1. Always 0 in Manual mode, where "through"
    /// has no meaning and a moving bar would imply a clock that is not running.
    double progress() const noexcept;

private:
    struct Slot {
        std::string symbol;
        std::vector<int> notes;    // empty when the symbol did not parse
    };

    void rephase() noexcept;

    Config config_{};
    std::vector<Slot> slots_;
    std::vector<int> rejected_;
    std::vector<int> empty_notes_;      // returned for rejected slots, kept for lifetime
    std::string empty_symbol_;

    int index_ = 0;
    bool active_ = false;
    bool running_ = false;
    SongMapAdvance mode_ = SongMapAdvance::Manual;
    double tempo_bpm_ = 0.0;
    double elapsed_in_chord_ms_ = 0.0;
};

} // namespace ghostband::core
