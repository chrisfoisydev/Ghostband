// GhostBand — live AI accompaniment for singer-songwriters.
// Copyright 2026 GhostBand contributors. Licensed under Apache-2.0.

#pragma once

#include <optional>
#include <string>
#include <vector>

namespace ghostband::core {

/// Where the band's harmony comes from.
enum class HarmonySource {
    Midi,                 ///< live MIDI notes — the reliable path
    SongMap,              ///< chord progression entered per section
    GuitarExperimental    ///< Phase 3. Labelled experimental wherever it appears.
};

const char* toString(HarmonySource s) noexcept;
const char* toDisplayString(HarmonySource s) noexcept;

/// Inverse of `toString`. Derived from `toString` itself rather than a second switch, so
/// the persisted spelling and the parsed spelling cannot drift apart — they already had,
/// once, between "guitar" and "guitar_experimental".
/// @return false if the text matches no source, leaving `out` untouched.
bool parseHarmonySource(const std::string& text, HarmonySource& out) noexcept;

/// One bar of 4/4 — the default a chord chart assumes unless told otherwise.
inline constexpr int kDefaultBeatsPerChord = 4;
/// Bounds for `SongSection::beatsPerChord`. One beat per chord is a real (if frantic)
/// chart; sixteen is four bars, past which a chart is not really what is being written.
inline constexpr int kMinBeatsPerChord = 1;
inline constexpr int kMaxBeatsPerChord = 16;

/// Section transition times offered by the brief (§15). Stored as milliseconds so a
/// future custom value needs no schema change.
inline constexpr int kTransitionInstantMs = 0;
inline constexpr int kTransitionFastMs = 250;
inline constexpr int kTransitionMediumMs = 500;
inline constexpr int kTransitionSlowMs = 1000;
inline constexpr int kTransitionVerySlowMs = 2000;
inline constexpr int kMaxTransitionMs = 5000;

/// One part of a song: Verse, Chorus, Bridge.
///
/// A section is an *arrangement* instruction, not a timeline entry. It has no duration and
/// no position in time — the performer decides when it starts and when it ends. That is
/// deliberate and load-bearing: the brief (§30.5) requires that repeating a chorus,
/// extending a bridge, or stopping early all remain possible, which a duration field would
/// quietly work against.
struct SongSection {
    std::string name = "Section";

    /// Empty means "inherit the song's default prompt". Storing empty rather than copying
    /// the default means editing the song-level prompt updates every section that never
    /// overrode it, which is what a performer expects.
    std::string stylePrompt;

    /// How much the band plays, 0..1. Fed to IntensityMacro.
    float aiIntensity = 0.5f;

    /// A section with the band off is a real musical choice — a solo verse — not a
    /// disabled feature.
    bool aiEnabled = true;

    int transitionMs = kTransitionMediumMs;

    /// Song Map mode only: chord symbols as typed by the performer, e.g. {"G","D","Em","C"}.
    /// Display/parse only — MRT2 always receives note numbers.
    std::vector<std::string> chordProgression;

    /// Song Map mode only: how many beats each chord in `chordProgression` is held for.
    ///
    /// Four is one bar of 4/4, which is the common case in the kind of song this app is
    /// for. Two-bar chords (8) and half-bar changes (2) are the next most common, which is
    /// why this is a number rather than a bar/half-bar toggle.
    ///
    /// Per section rather than per song: a chorus that moves twice as fast as its verse is
    /// ordinary songwriting, and per song would not express it. Per *chord* would express
    /// more still, and is not done — it would turn a chord chart into a sequencer, and the
    /// performer already has a footswitch for the irregular case.
    int beatsPerChord = kDefaultBeatsPerChord;

    std::optional<double> tempoBpm;
    std::string notes;

    /// The prompt this section actually uses, resolving inheritance.
    std::string resolvedPrompt(const std::string& songDefault) const;
};

/// A song: an ordered set of sections plus the settings they share.
struct Song {
    /// Bumped whenever the persisted shape changes. Migration infrastructure exists from
    /// the first version precisely so a schema change cannot destroy a performer's songs.
    ///
    /// **Version 2** (2026-08-20) added `SongSection::beatsPerChord`. A version 1 file has
    /// no such key, and the struct default of 4 is exactly what those songs meant, so the
    /// migration adds nothing — but it is written out explicitly rather than relying on
    /// that coincidence, because the next schema change will not be so lucky and the
    /// branch needs to exist before it is needed under pressure.
    ///
    /// Note the one-way cost: a file written by this build cannot be opened by a build
    /// that only knows version 1. `parseHeader` refuses newer files by design rather than
    /// reading them partially, which is the right failure but is worth knowing before
    /// downgrading mid-tour.
    static constexpr int kSchemaVersion = 2;

    std::string title = "Untitled";
    std::string defaultStylePrompt;
    std::string modelName = "mrt2_small";

    /// Song-level AI output level in dB — how loud, not how much.
    float masterAiLevelDb = 0.0f;

    HarmonySource harmonySource = HarmonySource::Midi;
    std::vector<SongSection> sections;

    /// Human-readable reason if the song cannot be performed. Empty when valid.
    std::string validate() const;
    bool isValid() const { return validate().empty(); }

    /// Distinct resolved prompts, in first-appearance order.
    ///
    /// This is the number that matters for section changes: MRT2 holds `kMaxPrompts` (6)
    /// encoded prompts at once, and a section whose prompt is resident switches instantly
    /// while one that is not pays an async MusicCoCa encode. See KNOWN_ISSUES.md §4.
    std::vector<std::string> distinctPrompts() const;

    /// True when every distinct prompt fits in MRT2's slots, so no section change in this
    /// song can ever stall.
    bool fitsPromptSlots() const;
};

/// A safe demo song (brief §24): generic instrumental prompts, no copyrighted melodies,
/// and a chord progression common enough to belong to nobody.
Song makeDemoSong();

} // namespace ghostband::core
