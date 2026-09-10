// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#ifndef EVEREST_CORE_MODULE_LEMDCBM400600_POLL_SCHEDULER_H
#define EVEREST_CORE_MODULE_LEMDCBM400600_POLL_SCHEDULER_H

#include <chrono>

namespace module::main {

/// \brief Fixed-rate schedule for the live measurement poll loop.
///
/// A plain "sleep(interval), then perform the requests" loop has a period of interval + request
/// duration rather than interval, so the publication rate silently degrades with the latency of the
/// device. That matters because consumers watch the measurement stream for gaps - EvseManager aborts a
/// DC cable check when it sees no power supply measurement for two seconds - so the headroom against
/// such a timeout must not depend on how fast the device happens to answer.
///
/// This scheduler keeps the phase of the schedule instead: each poll is due exactly one interval after
/// the previous one was due, and the time spent in the requests is absorbed by a correspondingly
/// shorter wait. If a poll overruns its interval entirely, the schedule is resynchronized to the
/// current time rather than firing a burst of catch-up polls.
class PollScheduler {
public:
    using clock = std::chrono::steady_clock;

    /// \param now the reference point of the schedule; the first poll is due one interval after it
    /// \param interval the desired time between the start of two consecutive polls
    PollScheduler(clock::time_point now, std::chrono::milliseconds interval) : deadline(now), interval(interval) {
    }

    /// \brief Advances the schedule and returns the time point at which the next poll is due.
    ///
    /// The returned time point is never in the future by more than one interval, and is clamped to
    /// \p now when the schedule has fallen behind, so it is always safe to pass to sleep_until().
    clock::time_point next(clock::time_point now) {
        deadline += interval;
        if (deadline < now) {
            // The previous poll took longer than a full interval. Resynchronize to now, so that the
            // lost time does not turn into a series of immediately-firing catch-up polls.
            deadline = now;
            overran = true;
        } else {
            overran = false;
        }
        return deadline;
    }

    /// \brief Restarts the schedule, so the next poll is due one interval after \p now.
    void reset(clock::time_point now) {
        deadline = now;
        overran = false;
    }

    /// \brief Whether the most recent next() call had to resynchronize because the poll overran.
    [[nodiscard]] bool last_poll_overran() const {
        return overran;
    }

private:
    clock::time_point deadline;
    std::chrono::milliseconds interval;
    bool overran = false;
};

} // namespace module::main

#endif // EVEREST_CORE_MODULE_LEMDCBM400600_POLL_SCHEDULER_H
