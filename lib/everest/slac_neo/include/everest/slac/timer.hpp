// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 - 2026 Pionix GmbH and Contributors to EVerest

#pragma once
#include <chrono>

namespace everest::lib::slac {

class timer {
public:
    using clock = std::chrono::steady_clock;
    using tp = clock::time_point;
    using tick = std::chrono::microseconds;
    timer();
    template <class Rep, class Period>
    timer(std::chrono::duration<Rep, Period> duration, bool initial_timeout = false) :
        reference(initial_timeout ? clock::now() - duration : clock::now()), target(clock::now()), duration(duration) {
    }

    virtual ~timer() = default;

    explicit operator bool() const;

    void reset();
    void resetReference();
    void forceTimeoutState();

    template <class Rep, class Period> void setDuration(std::chrono::duration<Rep, Period> value) {
        duration = value;
        target = reference + value;
    }

    void setDurationMicroSeconds(long long value);
    void setDurationMilliSeconds(long long value);
    void setDurationSeconds(long long value);
    void setDurationMinutes(long long value);

    template <class Rep, class Periode> bool getRemainingTime(std::chrono::duration<Rep, Periode>& remaining) {
        auto now = clock::now();
        remaining = target - now;
        return now > target;
    }
    long long getRemainingMicroSeconds() const;
    long long getRemainingMilliSeconds() const;
    long long getRemainingSeconds() const;
    long long getRemainingMinutes() const;

    void setTimePoint(tp const& value);
    tp getTargetTime() const;

    bool timeout() const;
    void wait() const;

private:
    tp reference;
    tp target;
    tick duration;
};

} // namespace everest::lib::slac
