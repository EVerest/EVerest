// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <chrono>
#include <functional>

// Absolute instants rather than tick counts, deliberately: the module collapses however many timer
// expirations accumulated into a single update(), so counting ticks would silently stretch every
// deadline whenever the event loop is busy. Comparing against a real instant is self correcting.
namespace everest::slac {

using TimePoint = std::chrono::steady_clock::time_point;
using TimeSource = std::function<TimePoint()>;

class Timer {
public:
    /// Remembers the duration, so rearm() need not restate it.
    void arm(TimePoint now, std::chrono::milliseconds duration) {
        m_duration = duration;
        m_at = now + duration;
    }

    void rearm(TimePoint now) {
        m_at = now + m_duration;
    }

    [[nodiscard]] bool expired(TimePoint now) const {
        return now > m_at;
    }

    [[nodiscard]] TimePoint deadline() const {
        return m_at;
    }

private:
    TimePoint m_at{};
    std::chrono::milliseconds m_duration{0};
};

} // namespace everest::slac
