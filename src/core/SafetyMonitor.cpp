// GhostBand — live AI accompaniment for singer-songwriters.
// Copyright 2026 GhostBand contributors. Licensed under Apache-2.0.

#include "SafetyMonitor.h"

namespace ghostband::core {

const char* toString(Health h) noexcept {
    switch (h) {
        case Health::Healthy:  return "Healthy";
        case Health::Warning:  return "Warning";
        case Health::Degraded: return "Degraded";
    }
    return "Unknown";
}

const char* toDisplayString(Health h) noexcept {
    switch (h) {
        case Health::Healthy:  return "AUDIO STABLE";
        case Health::Warning:  return "AUDIO GLITCHING";
        case Health::Degraded: return "AI MUTED - AUDIO UNSTABLE";
    }
    return "UNKNOWN";
}

} // namespace ghostband::core
