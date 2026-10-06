// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 - 2026 Pionix GmbH and Contributors to EVerest

#pragma once

#include <chrono>
#include <optional>

namespace everest::lib::slac {

// A deadline that never reads a clock. The owner samples "now" once per event (Context::sample_time)
// and passes it in, so every timer in one event agrees on the time, the state machines are a pure
// function of their inputs, and tests advance time instead of waiting for it.
class timer {
public:
    using clock = std::chrono::steady_clock;
    using tp = clock::time_point;
    using tick = std::chrono::microseconds;

    // Set the duration without moving the reference; the deadline becomes reference + duration.
    template <class Rep, class Period> void set_duration(std::chrono::duration<Rep, Period> value) {
        duration = std::chrono::duration_cast<tick>(value);
    }
    void set_duration_ms(long long value);

    // (Re)start the countdown at \p now, keeping the duration.
    void reset(tp now);

    // Expire at \p deadline, whatever the duration was.
    void expire_at(tp deadline);

    // set_duration followed by reset.
    template <class Rep, class Period> void arm(tp now, std::chrono::duration<Rep, Period> value) {
        set_duration(value);
        reset(now);
    }

    [[nodiscard]] tp deadline() const;

    // Strictly after the deadline: in the tick the deadline falls on the timer has not expired yet.
    // A default-constructed timer (reference at the clock epoch, zero duration) counts as expired.
    [[nodiscard]] bool expired(tp now) const;

    // Time left until the deadline; negative once expired.
    [[nodiscard]] tick remaining(tp now) const;

private:
    tp reference{};
    tick duration{0};
};

// Folds timers into the wait until the earliest one still ahead of `now`. An expired timer is
// skipped: the machine already had its chance to act on it, so it needs no wake-up for it.
class earliest_deadline {
public:
    explicit earliest_deadline(timer::tp at);
    void offer(timer const& t);
    // Time to wait from `now` until the earliest deadline has passed: one tick beyond it, since
    // expired() is strictly after.
    [[nodiscard]] std::optional<timer::tick> wait() const;

private:
    timer::tp now;
    std::optional<timer::tp> earliest;
};

} // namespace everest::lib::slac
