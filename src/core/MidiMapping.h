// GhostBand — live AI accompaniment for singer-songwriters.
// Copyright 2026 GhostBand contributors. Licensed under Apache-2.0.

#pragma once

#include <string>
#include <vector>

namespace ghostband::core {

/// Things a foot controller can do. Deliberately small: these are the actions worth
/// reaching with a foot mid-song, not everything the app can do.
enum class PerformanceAction {
    None,
    NextSection,
    PreviousSection,
    RepeatSection,
    ToggleAiBand,
    Panic,
    IntensityUp,
    IntensityDown,
    /// Moving between songs of a setlist. A set the performer has to walk back to the
    /// laptop to advance is not a set they can perform.
    NextSong,
    PreviousSong,
    /// Song Map only: step the chord chart by hand.
    ///
    /// Not a convenience. A section with no tempo is `SongMapAdvance::Manual` and the
    /// chart moves *only* when told to — so without a footswitch bound to this, Song Map
    /// on an unmetered song is a mode the performer cannot actually use. In MIDI mode
    /// both are harmless no-ops rather than errors.
    NextChord,
    PreviousChord,
};

const char* toString(PerformanceAction a) noexcept;
const char* toDisplayString(PerformanceAction a) noexcept;
PerformanceAction parsePerformanceAction(const std::string& s) noexcept;

/// All mappable actions, in the order a settings screen should list them.
std::vector<PerformanceAction> allPerformanceActions();

/// One MIDI message shape a footswitch can send.
struct MidiBinding {
    enum class Type { None, Note, ControlChange };

    Type type = Type::None;
    /// 1..16, or 0 for "any channel". Any is the default because performers rarely know
    /// or care what channel their pedal transmits on, and a channel mismatch produces a
    /// pedal that silently does nothing.
    int channel = 0;
    int number = -1; ///< note number or CC number

    bool isValid() const noexcept { return type != Type::None && number >= 0; }
    bool matches(const MidiBinding& incoming) const noexcept;
    bool operator==(const MidiBinding& other) const noexcept;

    std::string toString() const;
    static MidiBinding parse(const std::string& s) noexcept;
};

/// The performer's foot-controller mappings, plus the MIDI Learn state machine.
///
/// **Why press-only.** A footswitch sends a message when pressed and usually another when
/// released. Firing on both would advance two sections per stomp. Only the press edge
/// triggers an action.
///
/// **Why debounce.** Mechanical footswitches bounce, and a bounce that fires NEXT twice
/// skips a section mid-song — a failure the performer cannot undo gracefully in front of
/// an audience. Repeats of the same binding inside the debounce window are dropped.
///
/// Time is passed in rather than read from a clock, so the debounce is testable and
/// behaves identically under test and on stage.
/// **Why this allocates nothing after construction.** `handleMessage` runs on JUCE's MIDI
/// thread, and during MIDI Learn that call also writes a binding. Holding one entry per
/// action from the constructor onwards means no path taken from the MIDI thread can resize
/// the vector. The set is not itself thread-safe — the caller serialises access (see
/// `GhostBandAudioEngine::mapping_lock_`) — but it never allocates under that lock.
class MidiMappingSet {
public:
    MidiMappingSet();

    /// A five-switch pedal layout applied to a fresh install, so a controller does
    /// something useful before the performer maps anything.
    static MidiMappingSet makeDefault();

    void bind(PerformanceAction action, const MidiBinding& binding);
    void clear(PerformanceAction action);
    void clearAll();

    MidiBinding bindingFor(PerformanceAction action) const;
    /// The action this message triggers, or None.
    PerformanceAction actionFor(const MidiBinding& incoming) const;
    bool hasBinding(PerformanceAction action) const;
    /// How many actions have a valid binding. Exists so a status readout can be refreshed
    /// without copying the whole set ten times a second.
    int mappedCount() const noexcept;

    /// @name MIDI Learn
    /// The flow from the brief: pick an action, press the pedal, mapping saved.
    /// @{
    void beginLearn(PerformanceAction action) noexcept;
    void cancelLearn() noexcept;
    bool isLearning() const noexcept { return learning_ != PerformanceAction::None; }
    PerformanceAction learningAction() const noexcept { return learning_; }

    /// The action displaced by the most recent learn, or None.
    ///
    /// Binding a pedal that was already assigned steals it, because two actions on one
    /// switch is never what anyone means. The UI reports the theft rather than letting a
    /// previously-working pedal go quietly dead.
    PerformanceAction lastDisplacedAction() const noexcept { return displaced_; }
    /// @}

    /// Feed an incoming MIDI message.
    ///
    /// @param pressed   true for note-on / CC >= 64. Releases never trigger.
    /// @param timeMs    monotonic milliseconds, for debouncing.
    /// @return the action to perform, or None (also None while learning, since the press
    ///         that teaches a mapping should not also fire it).
    PerformanceAction handleMessage(const MidiBinding& incoming, bool pressed,
                                    double timeMs);

    void setDebounceMs(double ms) noexcept { debounce_ms_ = ms > 0.0 ? ms : 0.0; }
    double debounceMs() const noexcept { return debounce_ms_; }

    /// @name Persistence — plain text, one mapping per line
    /// @{
    std::string serialise() const;
    static MidiMappingSet deserialise(const std::string& text);
    /// @}

    /// Address of the entry storage. Exists so a test can assert that no operation
    /// reachable from the MIDI thread reallocates it.
    const void* bindingProbeAddress() const noexcept { return entries_.data(); }

private:
    /// Far enough in the past that the first press of a session always clears the
    /// debounce window, whatever the caller's clock epoch happens to be.
    static constexpr double kNeverFired = -1.0e9;

    struct Entry {
        PerformanceAction action = PerformanceAction::None;
        MidiBinding binding;
        double last_fired_ms = kNeverFired;
    };

    Entry* find(PerformanceAction action);
    const Entry* find(PerformanceAction action) const;

    std::vector<Entry> entries_;
    PerformanceAction learning_ = PerformanceAction::None;
    PerformanceAction displaced_ = PerformanceAction::None;
    /// 120 ms: longer than any mechanical bounce, far shorter than a deliberate
    /// double-stomp.
    double debounce_ms_ = 120.0;
};

} // namespace ghostband::core
