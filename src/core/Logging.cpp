// GhostBand — live AI accompaniment for singer-songwriters.
// Copyright 2026 GhostBand contributors. Licensed under Apache-2.0.

#include "Logging.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace ghostband::core {

const char* toString(LogLevel l) noexcept {
    switch (l) {
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info:  return "INFO";
        case LogLevel::Warn:  return "WARN";
        case LogLevel::Error: return "ERROR";
    }
    return "?";
}

const char* toString(LogCategory c) noexcept {
    switch (c) {
        case LogCategory::Model:       return "model";
        case LogCategory::Audio:       return "audio";
        case LogCategory::Midi:        return "midi";
        case LogCategory::Generation:  return "generation";
        case LogCategory::Performance: return "performance";
        case LogCategory::Panic:       return "panic";
        case LogCategory::System:      return "system";
    }
    return "unknown";
}

namespace {

std::string timestamp() {
    using namespace std::chrono;
    const auto now = system_clock::now();
    const auto t = system_clock::to_time_t(now);
    const auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;

    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif

    std::ostringstream os;
    os << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S")
       << '.' << std::setw(3) << std::setfill('0') << ms.count() << 'Z';
    return os.str();
}

/// Quote a value only when it needs it, so common lines stay readable.
std::string formatValue(const std::string& v) {
    const bool needs_quotes = v.empty()
        || v.find_first_of(" \t\"=") != std::string::npos;
    if (!needs_quotes) return v;

    std::string out;
    out.reserve(v.size() + 2);
    out.push_back('"');
    for (char ch : v) {
        if (ch == '"' || ch == '\\') out.push_back('\\');
        out.push_back(ch);
    }
    out.push_back('"');
    return out;
}

} // namespace

Logger& Logger::instance() {
    static Logger logger;
    return logger;
}

void Logger::setMinLevel(LogLevel level) {
    std::lock_guard<std::mutex> lock(mutex_);
    min_level_ = level;
}

LogLevel Logger::minLevel() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return min_level_;
}

void Logger::setSink(std::function<void(const std::string&)> sink) {
    std::lock_guard<std::mutex> lock(mutex_);
    sink_ = std::move(sink);
}

void Logger::log(LogLevel level, LogCategory category, const std::string& message,
                 std::initializer_list<LogField> fields) {
    // Build the line outside the lock where possible; the timestamp call is the only
    // syscall-ish part and does not need serialising.
    std::ostringstream os;
    os << timestamp()
       << ' ' << toString(level)
       << " [" << toString(category) << "] "
       << message;

    // Fields are emitted in a stable (sorted) order so that diffing two gig logs shows
    // real differences rather than map-iteration noise.
    std::vector<LogField> sorted(fields.begin(), fields.end());
    std::sort(sorted.begin(), sorted.end(),
              [](const LogField& a, const LogField& b) { return a.first < b.first; });
    for (const auto& [key, value] : sorted) {
        os << ' ' << key << '=' << formatValue(value);
    }

    std::string line = os.str();

    std::function<void(const std::string&)> sink_copy;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (level < min_level_) return;

        if (level == LogLevel::Error) ++error_count_;
        if (level == LogLevel::Warn) ++warn_count_;

        if (ring_.size() < kRingCapacity) {
            ring_.push_back(line);
        } else {
            ring_[ring_next_] = line;
        }
        ring_next_ = (ring_next_ + 1) % kRingCapacity;

        sink_copy = sink_;
    }

    // Emit outside the lock: a slow sink (file, UI) must not serialise other threads'
    // logging, and must never be able to deadlock by re-entering the logger.
    if (sink_copy) {
        sink_copy(line);
    } else {
        std::fprintf(stderr, "%s\n", line.c_str());
    }
}

std::vector<std::string> Logger::recent(std::size_t maxLines) const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<std::string> out;
    if (ring_.empty()) return out;

    const std::size_t count = std::min(maxLines, ring_.size());
    // Walk backwards from the newest entry, then reverse, so callers get oldest-first.
    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t idx = (ring_next_ + ring_.size() - 1 - i) % ring_.size();
        out.push_back(ring_[idx]);
    }
    std::reverse(out.begin(), out.end());
    return out;
}

std::size_t Logger::errorCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return error_count_;
}

std::size_t Logger::warnCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return warn_count_;
}

void Logger::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    ring_.clear();
    ring_next_ = 0;
    error_count_ = 0;
    warn_count_ = 0;
    sink_ = nullptr;
    min_level_ = LogLevel::Info;
}

} // namespace ghostband::core
