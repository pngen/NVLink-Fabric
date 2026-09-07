#pragma once
#include <cstdint>
#include <chrono>
#include <string>
#include <cstdio>

namespace nvlinkfabric {

//---------------------------------------------------------------------------
// Timestamp: wall-clock nanoseconds since the Unix epoch. Stored as an
// explicitly-typed value so freshness can be computed and serialized.
//---------------------------------------------------------------------------
struct Timestamp {
    std::int64_t unix_ns{0};
    bool valid{false};

    Timestamp() = default;
    Timestamp(std::int64_t ns, bool v) : unix_ns(ns), valid(v) {}

    bool is_valid() const noexcept { return valid; }

    // Approximate ISO-8601 UTC string (no sub-second dependence on locale).
    std::string to_string() const {
        if (!valid) { return "invalid"; }
        const std::time_t secs = static_cast<std::time_t>(unix_ns / 1000000000LL);
        const std::int64_t ms = (unix_ns % 1000000000LL) / 1000000LL;
        std::tm tmv{};
        // G: windows-safe localtime_r equivalent uses localtime_s.
        #ifdef _WIN32
        localtime_s(&tmv, &secs);
        #else
        localtime_r(&secs, &tmv);
        #endif
        char buf[40];
        std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d.%03d",
                      tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
                      tmv.tm_hour, tmv.tm_min, tmv.tm_sec,
                      static_cast<int>(ms));
        return std::string(buf);
    }

    friend bool operator==(const Timestamp& a, const Timestamp& b) noexcept {
        return a.unix_ns == b.unix_ns && a.valid == b.valid;
    }
};

//---------------------------------------------------------------------------
// Wall-clock helpers.
//---------------------------------------------------------------------------
inline Timestamp now_timestamp() {
    const auto now = std::chrono::system_clock::now();
    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        now.time_since_epoch()).count();
    return Timestamp(ns, true);
}

// Monotonic clock for performance/measurement durations.
inline std::chrono::steady_clock::time_point steady_now() {
    return std::chrono::steady_clock::now();
}

}  // namespace nvlinkfabric
