// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

// Unit tests for the PollScheduler of the live measurement loop

#include "poll_scheduler.hpp"
#include <gtest/gtest.h>

namespace module::main {

using namespace std::chrono_literals;
using clock = PollScheduler::clock;

/// \brief The first poll is due one interval after the reference point
TEST(PollSchedulerTest, first_poll_is_due_after_one_interval) {
    const auto start = clock::now();
    PollScheduler scheduler{start, 1000ms};

    EXPECT_EQ(scheduler.next(start), start + 1000ms);
    EXPECT_FALSE(scheduler.last_poll_overran());
}

/// \brief The time spent working is absorbed by the wait, keeping the period at exactly one interval
///
/// This is the property the whole class exists for: with a plain sleep-then-work loop the period would
/// grow to interval + work duration, silently degrading the publication rate as the device gets slower.
TEST(PollSchedulerTest, work_duration_does_not_extend_the_period) {
    const auto start = clock::now();
    PollScheduler scheduler{start, 1000ms};

    const auto first = scheduler.next(start);
    ASSERT_EQ(first, start + 1000ms);

    // The poll took 400 ms, so the next one is still due 1000 ms after the previous deadline.
    const auto second = scheduler.next(first + 400ms);
    EXPECT_EQ(second, start + 2000ms);
    EXPECT_FALSE(scheduler.last_poll_overran());

    // ... and a slower poll of 900 ms does not shift the schedule either.
    const auto third = scheduler.next(second + 900ms);
    EXPECT_EQ(third, start + 3000ms);
    EXPECT_FALSE(scheduler.last_poll_overran());
}

/// \brief A poll that overruns its interval resynchronizes instead of queueing catch-up polls
TEST(PollSchedulerTest, overrun_resynchronizes_the_schedule) {
    const auto start = clock::now();
    PollScheduler scheduler{start, 1000ms};

    const auto first = scheduler.next(start);
    ASSERT_EQ(first, start + 1000ms);

    // The poll took 2500 ms, so its deadline (start + 2000 ms) is already in the past.
    const auto overrun_now = first + 2500ms;
    const auto second = scheduler.next(overrun_now);
    EXPECT_EQ(second, overrun_now);
    EXPECT_TRUE(scheduler.last_poll_overran());

    // The schedule continues from there rather than firing the missed polls back to back.
    EXPECT_EQ(scheduler.next(second), overrun_now + 1000ms);
    EXPECT_FALSE(scheduler.last_poll_overran());
}

/// \brief A returned deadline is never in the past, so it is always safe for sleep_until()
TEST(PollSchedulerTest, deadline_is_never_in_the_past) {
    const auto start = clock::now();
    PollScheduler scheduler{start, 100ms};

    auto now = start;
    for (int i = 0; i < 10; i++) {
        const auto deadline = scheduler.next(now);
        EXPECT_GE(deadline, now);
        now = deadline + 250ms; // consistently overrunning the interval
    }
}

/// \brief reset() restarts the schedule from the given point
TEST(PollSchedulerTest, reset_restarts_the_schedule) {
    const auto start = clock::now();
    PollScheduler scheduler{start, 1000ms};

    ASSERT_EQ(scheduler.next(start), start + 1000ms);

    const auto resume = start + 30s;
    scheduler.reset(resume);
    EXPECT_FALSE(scheduler.last_poll_overran());
    EXPECT_EQ(scheduler.next(resume), resume + 1000ms);
}

} // namespace module::main
