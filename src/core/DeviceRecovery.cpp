// GhostBand — live AI accompaniment for singer-songwriters.
// Copyright 2026 GhostBand contributors. Licensed under Apache-2.0.

#include "core/DeviceRecovery.h"

#include <algorithm>
#include <cmath>

namespace ghostband::core {

const char* toString(DeviceHealth h) noexcept {
    switch (h) {
        case DeviceHealth::Running:  return "Running";
        case DeviceHealth::Lost:     return "Lost";
        case DeviceHealth::Restored: return "Restored";
    }
    return "?";
}

const char* toDisplayString(DeviceHealth h) noexcept {
    switch (h) {
        case DeviceHealth::Running:  return "OK";
        case DeviceHealth::Lost:     return "NO OUTPUT";
        // Not "RECOVERED": the device recovered, the band has not. Saying "recovered"
        // would tell the performer the problem is over while the band is still silent.
        case DeviceHealth::Restored: return "AUDIO BACK";
    }
    return "?";
}

void DeviceRecovery::noteRunning() noexcept {
    health_ = DeviceHealth::Running;
    reason_.clear();
    attempts_ = 0;
    next_attempt_at_ms_ = 0;
}

void DeviceRecovery::noteLost(std::string reason, std::int64_t nowMs) noexcept {
    // Already lost: keep the original reason and the existing schedule. A dying interface
    // typically reports several errors in a row, and letting each one reset the backoff
    // would turn the schedule into a tight retry loop at exactly the moment the machine is
    // least able to afford one.
    if (health_ == DeviceHealth::Lost) return;

    health_ = DeviceHealth::Lost;
    reason_ = std::move(reason);
    attempts_ = 0;
    next_attempt_at_ms_ = nowMs + config_.firstRetryMs;
}

std::int64_t DeviceRecovery::currentIntervalMs() const noexcept {
    // Exponential from firstRetryMs, capped at maxRetryMs. Computed rather than stored so
    // the schedule is a pure function of the attempt count and cannot drift.
    double interval = static_cast<double>(config_.firstRetryMs);
    for (int i = 0; i < attempts_; ++i) {
        interval *= config_.backoffFactor;
        if (interval >= static_cast<double>(config_.maxRetryMs)) break;
    }
    return std::min(static_cast<std::int64_t>(interval), config_.maxRetryMs);
}

bool DeviceRecovery::shouldRetryNow(std::int64_t nowMs) const noexcept {
    if (health_ != DeviceHealth::Lost) return false;
    if (hasGivenUp()) return false;
    return nowMs >= next_attempt_at_ms_;
}

void DeviceRecovery::noteRetryFailed(std::int64_t nowMs) noexcept {
    if (health_ != DeviceHealth::Lost) return;
    ++attempts_;
    next_attempt_at_ms_ = nowMs + currentIntervalMs();
}

void DeviceRecovery::noteRetrySucceeded() noexcept {
    if (health_ != DeviceHealth::Lost) return;
    // Restored, not Running. The device is back; the band is still deliberately silent.
    health_ = DeviceHealth::Restored;
    next_attempt_at_ms_ = 0;
}

bool DeviceRecovery::acknowledgeRestored() noexcept {
    if (health_ != DeviceHealth::Restored) return false;
    noteRunning();
    return true;
}

std::int64_t DeviceRecovery::nextRetryInMs(std::int64_t nowMs) const noexcept {
    if (health_ != DeviceHealth::Lost) return 0;
    return std::max<std::int64_t>(0, next_attempt_at_ms_ - nowMs);
}

bool DeviceRecovery::hasGivenUp() const noexcept {
    return config_.maxAttempts > 0 && attempts_ >= config_.maxAttempts;
}

std::string DeviceRecovery::displayReason() const {
    switch (health_) {
        case DeviceHealth::Running:
            return {};
        case DeviceHealth::Lost:
            // What stopped, then what did not, then what is being done about it.
            return "AUDIO DEVICE LOST - the band has no output. Your voice and guitar are "
                   "unaffected. Reconnecting automatically.";
        case DeviceHealth::Restored:
            return "Audio device is back. The band is still silent - press RECOVER BAND "
                   "when you are ready for it.";
    }
    return {};
}

} // namespace ghostband::core
