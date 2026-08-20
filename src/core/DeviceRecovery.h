// GhostBand — live AI accompaniment for singer-songwriters.
// Copyright 2026 GhostBand contributors. Licensed under Apache-2.0.

#pragma once

#include <cstdint>
#include <string>

namespace ghostband::core {

/// What the audio output path is doing, from the performer's point of view.
enum class DeviceHealth {
    /// A device is open and the callback is running.
    Running,
    /// The device went away. There is no audio output at all right now.
    Lost,
    /// The device went away and came back; audio output works again, but the band is
    /// deliberately still silent and needs an explicit press to resume.
    Restored,
};

const char* toString(DeviceHealth h) noexcept;
/// Wording for the performer, not the log. Never says "error".
const char* toDisplayString(DeviceHealth h) noexcept;

/// Decides *when* to retry opening an audio device after it disappears, and remembers why
/// it disappeared. Contains no audio code and no platform code, which is the point: the
/// part that has to be right under stress is the schedule, and the schedule is testable.
///
/// ## Why this exists
///
/// An interface being unplugged, a sleep/wake, a hub browning out, someone changing the
/// sample rate in another app — these are **expected runtime conditions** on a stage, not
/// exceptional ones (`CLAUDE.md`). Until now GhostBand handled them by fading the band out
/// and stopping there: correct as far as it went, and a dead app for the rest of the night.
///
/// ## The two rules that shape the design
///
/// **1. Recover the device automatically. Do not recover the band automatically.**
/// Restoring audio output is safe — without a device there is no output at all, so
/// reopening one cannot surprise anyone. Restarting the *accompaniment* is not safe: the
/// performer has kept singing through the dropout, and a band reappearing mid-phrase is
/// precisely the failure this project refuses to ship. So a successful reopen lands in
/// `Restored`, not `Running`, and something has to ask for the band back. This mirrors how
/// `Health::Degraded` already works.
///
/// **2. Never stop trying.** `maxAttempts` defaults to unlimited. A set lasts hours and a
/// device may come back at any point in them; a recovery loop that gives up after five
/// tries is one that has decided the gig is over. Backoff keeps the cost of that
/// negligible — after the first few seconds it is one attempt every 8 seconds.
class DeviceRecovery {
public:
    struct Config {
        /// First retry is fast, because the overwhelmingly common case — a sample-rate
        /// change, a brief USB glitch — resolves in well under a second.
        std::int64_t firstRetryMs = 400;
        /// Ceiling. Long enough not to churn, short enough that plugging the interface
        /// back in feels like it worked immediately rather than "eventually".
        std::int64_t maxRetryMs = 8000;
        double backoffFactor = 2.0;
        /// 0 means never give up. See rule 2 above.
        int maxAttempts = 0;
    };

    DeviceRecovery() = default;
    explicit DeviceRecovery(Config config) noexcept : config_(config) {}

    /// The device opened and is running. Clears any recorded failure.
    void noteRunning() noexcept;

    /// The device went away. `reason` is kept verbatim for the log; the UI gets
    /// `displayReason()`. Calling this while already lost does not restart the backoff —
    /// a device that emits five errors as it dies must not reset the schedule five times.
    void noteLost(std::string reason, std::int64_t nowMs) noexcept;

    /// Should the caller try to reopen a device right now? Call as often as convenient;
    /// it is a comparison, not a timer.
    bool shouldRetryNow(std::int64_t nowMs) const noexcept;

    /// Record the outcome of an attempt made because `shouldRetryNow` said yes.
    void noteRetryFailed(std::int64_t nowMs) noexcept;
    /// A reopen worked. Moves to `Restored`, **not** `Running` — see rule 1.
    void noteRetrySucceeded() noexcept;

    /// The performer asked for the band back after a `Restored`. Returns false if there
    /// was nothing to acknowledge, so a stray press cannot fake a recovery.
    bool acknowledgeRestored() noexcept;

    DeviceHealth health() const noexcept { return health_; }
    /// Attempts made since the device was lost. Reset by `noteRunning`.
    int attempts() const noexcept { return attempts_; }
    /// Milliseconds until the next attempt, or 0 if one is due now. Meaningless unless
    /// `health() == DeviceHealth::Lost`.
    std::int64_t nextRetryInMs(std::int64_t nowMs) const noexcept;
    /// True once `maxAttempts` is exhausted. Always false with the default config.
    bool hasGivenUp() const noexcept;

    /// The verbatim reason the device was lost, for the log.
    const std::string& reason() const noexcept { return reason_; }
    /// One line for the performer. States what stopped, then what did not — the same
    /// shape as the PANIC and Degraded banners, because the message under stage lights
    /// has to answer "is my voice still working?" before anything else.
    std::string displayReason() const;

private:
    std::int64_t currentIntervalMs() const noexcept;

    Config config_{};
    DeviceHealth health_ = DeviceHealth::Running;
    std::string reason_;
    int attempts_ = 0;
    std::int64_t next_attempt_at_ms_ = 0;
};

} // namespace ghostband::core
