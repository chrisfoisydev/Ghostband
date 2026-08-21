// GhostBand — live AI accompaniment for singer-songwriters.
// Copyright 2026 GhostBand contributors. Licensed under Apache-2.0.
//
// ⚠️ macOS-only. Builds and runs on macOS/Apple Silicon; cannot be compiled on
// this Linux development machine at all. See KNOWN_ISSUES.md §1 — which means any change
// here is unverified until someone builds it on the Mac.

#pragma once

#include "core/AiOutputStage.h"
#include "core/EngineState.h"
#include "core/IGenerationBackend.h"
#include "core/ControlLatency.h"
#include "core/DeviceRecovery.h"
#include "core/ModelCatalog.h"
#include "core/SongMapPlayer.h"
#include "core/SoundingChord.h"
#include "core/IntensityMacro.h"
#include "core/MidiHarmonyState.h"
#include "core/MidiMapping.h"
#include "core/PerformanceEngine.h"
#include "core/Persistence.h"
#include "core/Setlist.h"
#include "core/Song.h"

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace ghostband::app {

/// Owns the audio device, the generation backend, and the safety stage; nothing else.
///
/// The UI talks to this class and never to the backend directly, so there is exactly one
/// place where the audio callback's invariants are enforced.
class GhostBandAudioEngine : private juce::AudioIODeviceCallback,
                             private juce::MidiInputCallback,
                             private juce::Timer {
public:
    GhostBandAudioEngine();
    ~GhostBandAudioEngine() override;

    /// Bring up the audio device. Stereo out, 48 kHz requested.
    /// Returns an error string on failure, or an empty string on success.
    juce::String initialise();

    juce::AudioDeviceManager& deviceManager() noexcept { return device_manager_; }

    /// @name Model lifecycle — message thread; loading happens on a worker
    /// @{
    /// Kicks off asset init + model load on a background thread and returns immediately.
    /// `onFinished` is invoked on the message thread.
    void loadModelAsync(const juce::File& resourceDir, const juce::File& modelPath,
                        std::function<void(bool, juce::String)> onFinished);
    void startGeneration();
    void stopGeneration();
    /// @}

    /// @name Performance controls
    /// @{
    /// Fade the AI to silence and latch. Safe from any thread.
    void panic();
    void clearPanic();
    bool isPanicked() const noexcept { return output_stage_.isPanicked(); }

    void setAiBandOn(bool on) { ai_band_on_ = on; updateAiAudible(); }
    bool isAiBandOn() const noexcept { return ai_band_on_; }

    void setOutputLevelDb(float db) { output_stage_.setOutputLevelDb(db); }
    float outputLevelDb() const noexcept { return output_stage_.outputLevelDb(); }

    /// Encodes THREE slots — sparse / base / full density variants of this prompt — so
    /// that intensity afterwards costs only a blend-weight write. Encoding per knob
    /// movement would stall on MusicCoCa mid-performance.
    void setTextPrompt(const juce::String& prompt);

    /// The prompt currently encoded into MRT2 — not what is typed in the editor. The UI
    /// compares the two so an unapplied edit is visible rather than silently ignored.
    juce::String appliedPrompt() const { return base_prompt_; }

    /// Transport delay from a control change to affected audio leaving the device.
    /// Computed from live buffer state; excludes MRT2's own musical response time.
    core::ControlLatencyEstimate controlLatency() const;

    /// Generation buffer, in whole MRT2 frames (1 frame = 40 ms). Fewer frames means
    /// lower latency and less margin against a late inference frame.
    void setGenerationBufferFrames(int frames);
    int generationBufferFrames() const noexcept { return buffer_frames_; }

    /// **How much the band plays.** Distinct from AI Output Level, which is how loud.
    void setAiIntensity(float intensity);
    float aiIntensity() const noexcept { return intensity_.intensity(); }
    int aiIntensityPercent() const noexcept { return intensity_.percent(); }
    core::IntensityParams intensityParams() const noexcept { return intensity_.compute(); }
    /// @}

    /// @name Songs and sections (Phase 2)
    /// @{
    /// Load a song. The engine keeps its own copy, so the caller need not outlive it.
    bool loadSong(const core::Song& song);
    void loadDemoSong();
    bool hasSong() const noexcept { return performance_.hasSong(); }
    core::PerformanceEngine& performance() noexcept { return performance_; }
    const core::PerformanceEngine& performance() const noexcept { return performance_; }

    /// Section navigation. Each re-applies AI-enabled state, so a section that turns the
    /// band off mutes the output stage rather than stopping generation — stopping would
    /// cost a restart on the way back in.
    bool nextSection();
    bool previousSection();
    bool repeatSection();
    /// @}

    /// @name Songs and setlists on disk (Phase 2.7 / 2.8)
    ///
    /// Songs live under ~/Documents/GhostBand, not Application Support: they are the
    /// performer's own work, so they must be visible in Finder, easy to back up, and easy
    /// to carry to another machine. Foot-controller mappings are app state and stay in
    /// Application Support.
    /// @{
    static juce::File songsDirectory();
    static juce::File setlistsDirectory();

    /// Save under a name derived from the title. Returns an empty string on success, or a
    /// performer-readable reason.
    juce::String saveSongAs(const core::Song& song, juce::File& fileWritten);
    /// Load and make current. Empty string on success.
    juce::String loadSongFile(const juce::File& file);

    /// Load a setlist and every song it names. Songs that fail to load leave a visible gap
    /// rather than being dropped; `missingSongs()` reports them. Empty string on success —
    /// missing songs are **not** a failure, because 11 of 12 songs is still a playable set.
    juce::String loadSetlistFile(const juce::File& file);
    juce::String saveSetlist(const core::Setlist& list, juce::File& fileWritten);

    /// One song on disk, as the setlist builder needs to show it.
    struct SongOnDisk {
        /// File name only, not a path — this is what a `SetlistEntry` stores, so that a
        /// setlist copied to another machine still resolves.
        juce::String file;
        /// Title read from the file. Falls back to the file name if it will not parse, so a
        /// damaged song is still visible and removable rather than silently absent.
        juce::String title;
        /// False when the file exists but could not be parsed. Shown, never hidden.
        bool readable = true;
    };

    /// Every song in the songs directory, sorted by title.
    ///
    /// Reads each file to recover its real title rather than guessing from the filename:
    /// `toSafeFileStem` is lossy (spaces and punctuation go), so "Ghost Light (Reprise)"
    /// would come back as "Ghost-Light-Reprise" and the performer would be picking from a
    /// list of near-miss names. Called on demand, not on a timer.
    std::vector<SongOnDisk> availableSongs() const;

    bool hasSetlist() const noexcept { return setlist_.isLoaded(); }
    const core::SetlistController& setlist() const noexcept { return setlist_; }
    std::vector<std::string> missingSongs() const { return setlist_.missingTitles(); }

    /// Move through the set. Each loads the song at the new position and resets it to its
    /// first section — a song should always start at the top, never wherever the last
    /// visit left it.
    bool nextSong();
    bool previousSong();
    bool goToSong(int index);
    /// @}

    /// @name Harmony (Phase 1)
    ///
    /// Fed from two sources that are deliberately indistinguishable downstream: a real
    /// MIDI device, and the on-screen keyboard. Both land in the same MidiHarmonyState,
    /// so testing with the computer keyboard exercises the identical path a controller
    /// will use — not a parallel one that might diverge.
    /// @{
    core::MidiHarmonyState& harmony() noexcept { return harmony_; }
    const core::MidiHarmonyState& harmony() const noexcept { return harmony_; }

    /// Feed one MIDI message through the full control path: foot-control matching first,
    /// then harmony with whatever was not consumed.
    ///
    /// Public so the on-screen keyboard can use it. That keyboard exists precisely so the
    /// control path is testable without buying hardware, and it only earns that if it is
    /// the *same* path — a parallel one would drift, and did: for one commit the on-screen
    /// keyboard reached harmony directly and could not exercise foot control at all.
    ///
    /// Callable from the MIDI thread or the message thread. Both are bounded here; see
    /// ARCHITECTURE.md §3.2.
    void handleMidiMessage(const juce::MidiMessage& message);

    /// Open every available MIDI input. Called at startup and on hot-plug.
    void refreshMidiInputs();
    juce::StringArray midiInputNames() const;
    bool anyMidiDeviceConnected() const noexcept;
    /// @}

    /// @name Foot control (Phase 2.6)
    ///
    /// A footswitch press arrives on JUCE's MIDI thread, which may be one of several
    /// (one per open device) and must stay bounded. Only PANIC is executed there — it is
    /// an atomic latch by construction, and queueing the one control that must always
    /// work behind a message thread that might be busy would defeat its purpose. Every
    /// other action is queued and drained by the 50 Hz timer, adding at most 20 ms to a
    /// change that already carries ~128 ms of transport latency.
    /// @{
    /// Snapshot of the current mappings. Message thread — this copies, so do not call it
    /// from a poll; use `mappedActionCount()` for status readouts.
    core::MidiMappingSet midiMappings() const;
    void setMidiMappings(const core::MidiMappingSet& mappings);

    /// How many actions are bound. Locks briefly without copying, so it is cheap enough
    /// for the 10 Hz diagnostics refresh.
    int mappedActionCount() const;

    /// Arm MIDI Learn for `action`. The next press on any open input binds it and is
    /// swallowed, so mapping PANIC does not also silence the band.
    void beginMidiLearn(core::PerformanceAction action);
    void cancelMidiLearn();
    bool isMidiLearning() const;
    core::PerformanceAction midiLearningAction() const;

    /// The binding most recently taken from another action by a learn, or None. Cleared
    /// by reading it, so the UI reports the theft exactly once.
    core::PerformanceAction takeDisplacedAction();

    /// The last action a pedal fired, with a counter that increments on every fire.
    ///
    /// The setup screen uses this to light a row when its pedal is pressed — the answer to
    /// "is my pedal reaching the app", which otherwise can only be tested by triggering
    /// the action for real. The two fields live in one atomic word so a poll can never
    /// pair a fresh count with a stale action and flash the wrong row.
    struct FiredAction {
        core::PerformanceAction action = core::PerformanceAction::None;
        std::uint32_t sequence = 0;
    };
    FiredAction lastFiredAction() const;

    /// Where GhostBand keeps app state — mappings and logs, not the performer's songs.
    ///
    /// `~/Library/Application Support/GhostBand` on macOS. JUCE's
    /// `userApplicationDataDirectory` is `~/Library` there, so it needs the extra
    /// component; using it raw put everything in `~/Library/GhostBand`, where nothing
    /// looks for it.
    static juce::File supportDirectory();
    /// The pre-fix location, kept only so its contents can be recovered.
    static juce::File legacySupportDirectory();
    /// Copy anything an older build left in the legacy directory. Never overwrites and
    /// never deletes — a botched migration must not be able to lose a pedal layout.
    static void migrateSupportDirectory();

    /// Plain-text mappings under `supportDirectory()`. Saved on every change, loaded at
    /// startup. Deliberately not versioned yet — task 2.8 owns schema migration for songs
    /// and setlists, and this file will move under it rather than growing a second,
    /// parallel versioning scheme now.
    static juce::File midiMappingFile();
    void saveMidiMappings();
    void loadMidiMappings();
    /// @}

    core::EngineState engineState() const noexcept { return state_.state(); }
    juce::String engineError() const { return juce::String(state_.errorReason()); }
    core::PromptStatus promptStatus() const;

    /// Snapshot for the diagnostics view. Message thread.
    core::DiagnosticsSnapshot diagnostics() const;

    /// Resident set size in GB. Message thread only — this makes a Mach syscall.
    static double residentMemoryGb();

    core::AiOutputStage& outputStage() noexcept { return output_stage_; }
    core::Health health() const noexcept { return output_stage_.safetyMonitor().health(); }
    void recoverFromDegraded();

    /// True when the loaded backend actually generates music. False for NullBackend, in
    /// which case the UI must not claim an AI band is available.
    bool hasRealBackend() const noexcept;

    /// What chip this machine has, as reported by the OS — e.g. `"Apple M2 Pro"`.
    ///
    /// Empty when it cannot be determined, which `core::realtimeVerdictFor` treats as
    /// Unknown rather than guessing. Cached after the first call: it is a syscall and it
    /// cannot change while the app is running.
    static juce::String chipName();

    /// Non-empty when the device is not at 48 kHz — MRT2 generates 48 kHz and GhostBand does
    /// not resample, so this is surfaced rather than silently accepted.
    juce::String sampleRateWarning() const;

    /// @name Song Map
    ///
    /// Harmony driven by the section's chord chart rather than by what the performer is
    /// holding. Only active when the loaded song's `harmonySource` is `SongMap`; in every
    /// other mode the player is stopped and nothing it holds is sounding.
    /// @{
    const core::SongMapPlayer& songMap() const noexcept { return song_map_; }
    /// True when the loaded song asks for Song Map **and** the current section has at
    /// least one chord that parses. Both halves matter: a Song Map song whose section is
    /// empty must steer nothing rather than steer badly.
    bool songMapActive() const noexcept;
    /// Move the chart by hand. Safe to call in any mode — a no-op when Song Map is not
    /// running, so a footswitch bound to it cannot do anything surprising in MIDI mode.
    void songMapAdvance();
    void songMapRetreat();
    void songMapRestart();
    /// @}

    /// @name Audio device loss and recovery
    ///
    /// An interface being unplugged, a sleep/wake, a hub browning out — expected runtime
    /// conditions on a stage. GhostBand reopens the device by itself and does **not**
    /// restart the band by itself; see `core::DeviceRecovery` for why those are different
    /// decisions. Message thread only.
    /// @{
    core::DeviceHealth deviceHealth() const noexcept { return device_recovery_.health(); }
    /// One line for the performer, or empty while the device is fine.
    juce::String deviceMessage() const { return juce::String(device_recovery_.displayReason()); }
    /// Attempts since the device was lost — shown so a failing reconnect looks like work
    /// in progress rather than a frozen app.
    int deviceRetryCount() const noexcept { return device_recovery_.attempts(); }
    /// The performer asked for the band back after the device returned. False if there was
    /// nothing to acknowledge.
    bool acknowledgeDeviceRestored();
    /// @}

private:
    // juce::AudioIODeviceCallback
    void audioDeviceIOCallbackWithContext(const float* const* inputChannelData,
                                          int numInputChannels,
                                          float* const* outputChannelData,
                                          int numOutputChannels,
                                          int numSamples,
                                          const juce::AudioIODeviceCallbackContext& context) override;
    void audioDeviceAboutToStart(juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;
    void audioDeviceError(const juce::String& errorMessage) override;

    // juce::Timer — drives section transitions. Control thread, ~50 Hz.
    void timerCallback() override;

    /// The band is audible only when the performer has it on AND the current section
    /// wants it. Recomputing from both avoids one silently overriding the other.
    void updateAiAudible();

    // juce::MidiInputCallback — called on the MIDI thread.
    void handleIncomingMidiMessage(juce::MidiInput* source,
                                   const juce::MidiMessage& message) override;

    /// Resolve a message against the mappings. Returns true if it was consumed as a
    /// performance action, in which case it must not also reach harmony — a footswitch
    /// bound to a note should not add that note to the chord.
    /// MIDI thread.
    bool routePerformanceAction(const juce::MidiMessage& message);

    /// Drain the queue on the message thread. Called from timerCallback.
    void drainPerformanceActions();
    void performAction(core::PerformanceAction action);

    juce::AudioDeviceManager device_manager_;

    /// Walks the current section's chord chart. Message thread only.
    core::SongMapPlayer song_map_;
    /// What the chart is currently holding, so a chord change only moves what changes —
    /// MRT2 re-articulates on every note-on, and G to Em shares two notes.
    core::SoundingChord song_map_sounding_;
    /// Detects a section change without the controller having to notify: comparing the
    /// change counter is cheaper than a callback and cannot be missed if a tick is late.
    std::uint64_t last_section_change_seen_ = 0;

    /// Load the current section into the chart player and start it, or stop everything if
    /// the song is not in Song Map mode. Message thread.
    void resyncSongMap();
    /// Push the chart's current chord to the backend, moving only what changed.
    void pushSongMapChord();
    /// Release everything the chart is holding. Called when leaving Song Map mode, on
    /// PANIC, and when a song is unloaded.
    void releaseSongMapChord();

    /// Retry schedule and state for a lost output device. Lives in core so the part that
    /// has to be right under stress — the schedule — is testable off-Mac.
    core::DeviceRecovery device_recovery_;
    /// Set from audioDeviceError, which may be called on the audio thread. Everything that
    /// allocates, logs or reopens a device happens on the message thread in timerCallback.
    std::atomic<bool> device_error_pending_{false};
    /// True while PANIC is latched because the *performer* asked for it, as opposed to
    /// because the device vanished. Both raise the same flag in the output stage, and
    /// without this the device recovering would quietly release a PANIC someone pressed on
    /// purpose — the one control in the app that must never be undone by anything except a
    /// deliberate press. Message thread only.
    bool performer_panic_ = false;
    /// The setup that last worked, so a reconnect can prefer the performer's interface over
    /// whatever macOS considers the default. Written on the message thread only.
    juce::AudioDeviceManager::AudioDeviceSetup last_good_setup_;
    bool has_last_good_setup_ = false;

    /// Called from timerCallback. Tries the remembered device first, then the default.
    /// Blocking — hundreds of milliseconds — which is acceptable here because there is no
    /// audio to interrupt while the device is gone.
    void attemptDeviceReopen();
    /// Turn a pending audio-thread error into recovery state. Message thread.
    void servicePendingDeviceError();
    /// shared_ptr because backend ownership has to be handed across a std::function
    /// during an async load, and Mrt2Backend is neither copyable nor movable.
    /// Swaps are serialised by detaching the audio callback first — see loadModelAsync.
    std::shared_ptr<core::IGenerationBackend> backend_;
    core::AiOutputStage output_stage_;
    core::EngineStateMachine state_;

    /// Preallocated in audioDeviceAboutToStart. The audio callback never allocates.
    std::vector<float> scratch_l_;
    std::vector<float> scratch_r_;

    std::atomic<double> current_sample_rate_{0.0};
    std::atomic<int> current_block_size_{0};

    /// Which output pair the AI bus lands on. Phase 0 is fixed at 0/1; the routing UI
    /// (FOH channels 3+4) arrives with Setup Mode.
    std::atomic<int> out_channel_left_{0};
    std::atomic<int> out_channel_right_{1};

    std::unique_ptr<juce::ThreadPool> load_pool_;

    /// Point the performance engine at a song and bring the output state into line.
    /// Shared by loadSong and every setlist move, so a song entered from a set behaves
    /// exactly like one opened on its own.
    bool activateSong(const core::Song& song);

    core::MidiHarmonyState harmony_;
    core::PerformanceEngine performance_;
    /// Owned copy: Song is cheap to hold and this removes a lifetime trap where a UI
    /// component's song outlives, or fails to outlive, the engine driving it.
    core::Song loaded_song_;
    core::SetlistController setlist_;
    bool ai_band_on_ = false;
    double last_tick_ms_ = 0.0;
    core::IntensityMacro intensity_;
    juce::String base_prompt_;
    int buffer_frames_ = 2;
    juce::StringArray open_midi_inputs_;

    /// Guards `midi_mappings_` and `action_queue_`. A SpinLock rather than a mutex because
    /// every critical section is a handful of instructions over a fixed-size container
    /// with no allocation. The MIDI thread only ever *tries* it: if the settings screen
    /// holds it, that press is dropped rather than blocking a device thread.
    mutable juce::SpinLock mapping_lock_;
    core::MidiMappingSet midi_mappings_{core::MidiMappingSet::makeDefault()};

    /// Fixed capacity, written under `mapping_lock_`. 16 is far more than the handful of
    /// presses that can land inside one 20 ms timer period; overflow drops the newest,
    /// which is the right end to drop — a queue backed up that far is already wrong.
    static constexpr int kActionQueueCapacity = 16;
    std::array<core::PerformanceAction, kActionQueueCapacity> action_queue_{};
    int action_queue_size_ = 0;

    /// Low 8 bits: the action. Upper 24: a fire counter. One word so the pair is always
    /// consistent — see FiredAction.
    std::atomic<std::uint32_t> fired_{0};
    std::atomic<core::PerformanceAction> displaced_action_{core::PerformanceAction::None};

    /// Set when a MIDI Learn press changes the mappings, cleared when they are written.
    ///
    /// The learn happens on a MIDI thread, which must not touch the filesystem — so the
    /// save is deferred to the 50 Hz timer on the message thread. Without this, learning
    /// was the one route that never persisted: CLEAR and RESTORE DEFAULTS both go through
    /// setMidiMappings and save, but the press that actually maps a pedal did not.
    std::atomic<bool> mappings_dirty_{false};

    /// Intensity step per pedal press. Coarse on purpose: a foot is not a knob, and five
    /// presses should cross the useful range rather than nudge it.
    static constexpr float kIntensityPedalStep = 0.1f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GhostBandAudioEngine)
};

} // namespace ghostband::app
