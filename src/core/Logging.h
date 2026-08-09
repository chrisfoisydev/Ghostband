// GhostBand — live AI accompaniment for singer-songwriters.
// Copyright 2026 GhostBand contributors. Licensed under Apache-2.0.

#pragma once

#include <cstddef>
#include <functional>
#include <initializer_list>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace ghostband::core {

enum class LogLevel { Debug = 0, Info = 1, Warn = 2, Error = 3 };

/// Categories chosen from the brief's §26 list of what must be diagnosable after a
/// failed rehearsal — not from a generic taxonomy.
enum class LogCategory {
    Model,       ///< load, unload, errors, model switching
    Audio,       ///< device changes, sample-rate mismatches, underruns (summarised)
    Midi,        ///< device connect/disconnect, mapping changes
    Generation,  ///< start, stop, restart, stalls
    Performance, ///< song and section changes
    Panic,       ///< panic engaged/released
    System       ///< startup, shutdown, crashes
};

const char* toString(LogLevel l) noexcept;
const char* toString(LogCategory c) noexcept;

using LogField = std::pair<std::string, std::string>;

/// Structured logger.
///
/// **This must never be called from the audio thread.** It allocates and takes a mutex.
/// The audio thread bumps atomic counters in `Diagnostics`; a control-thread poller reads
/// those and logs a *summary* — "14 underruns in the last 2 s" — rather than one line per
/// block. That distinction is the whole reason the brief says "never log huge volumes
/// from the real-time audio thread": a logger in the callback is itself a cause of the
/// underruns it would report.
///
/// Records are emitted as one line per event with sorted key=value fields, so a gig log
/// is greppable without tooling.
class Logger {
public:
    static Logger& instance();

    void log(LogLevel level, LogCategory category, const std::string& message,
             std::initializer_list<LogField> fields = {});

    void debug(LogCategory c, const std::string& m, std::initializer_list<LogField> f = {}) {
        log(LogLevel::Debug, c, m, f);
    }
    void info(LogCategory c, const std::string& m, std::initializer_list<LogField> f = {}) {
        log(LogLevel::Info, c, m, f);
    }
    void warn(LogCategory c, const std::string& m, std::initializer_list<LogField> f = {}) {
        log(LogLevel::Warn, c, m, f);
    }
    void error(LogCategory c, const std::string& m, std::initializer_list<LogField> f = {}) {
        log(LogLevel::Error, c, m, f);
    }

    void setMinLevel(LogLevel level);
    LogLevel minLevel() const;

    /// Redirect output (file writer, UI console, test capture). Default writes to stderr.
    void setSink(std::function<void(const std::string&)> sink);

    /// Most recent lines, oldest first, for the diagnostics view. Bounded ring, so this
    /// can never grow without limit during a long set.
    std::vector<std::string> recent(std::size_t maxLines = 200) const;

    std::size_t errorCount() const;
    std::size_t warnCount() const;

    /// Test/teardown helper: clears the ring and counters, restores the default sink.
    void reset();

private:
    Logger() = default;

    static constexpr std::size_t kRingCapacity = 512;

    mutable std::mutex mutex_;
    LogLevel min_level_ = LogLevel::Info;
    std::function<void(const std::string&)> sink_;
    std::vector<std::string> ring_;
    std::size_t ring_next_ = 0;
    std::size_t error_count_ = 0;
    std::size_t warn_count_ = 0;
};

/// Convenience macros keep call sites short without hiding the category.
#define GHOSTBAND_LOG_INFO(cat, msg, ...) \
    ::ghostband::core::Logger::instance().info(cat, msg, __VA_ARGS__)
#define GHOSTBAND_LOG_WARN(cat, msg, ...) \
    ::ghostband::core::Logger::instance().warn(cat, msg, __VA_ARGS__)
#define GHOSTBAND_LOG_ERROR(cat, msg, ...) \
    ::ghostband::core::Logger::instance().error(cat, msg, __VA_ARGS__)

} // namespace ghostband::core
