// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gtest/gtest.h>

#include "connector_lock_confirmation.hpp"

namespace {
using module::ConnectorLockConfirmation;
using Event = ConnectorLockConfirmation::Event;
using LockState = ConnectorLockConfirmation::LockState;
using namespace std::chrono_literals;

struct ConnectorLockConfirmationTest : public ::testing::Test {
    ConnectorLockConfirmation lock;
    const ConnectorLockConfirmation::Clock::time_point t0{};

    void SetUp() override {
        lock.set_timeout(5000ms);
    }
};

TEST_F(ConnectorLockConfirmationTest, DisabledCountsAsConfirmedAndNeverTimesOut) {
    lock.set_timeout(0ms);
    lock.lock_commanded(t0);

    EXPECT_TRUE(lock.confirmed());
    EXPECT_TRUE(lock.confirmed_or_start_wait(t0));
    EXPECT_EQ(lock.poll(t0 + 1h), Event::None);
}

TEST_F(ConnectorLockConfirmationTest, OnlyLockedIsConfirmed) {
    EXPECT_FALSE(lock.confirmed());
    lock.lock_state_reported(LockState::Locked);
    EXPECT_TRUE(lock.confirmed());
    lock.lock_state_reported(LockState::Unlocked);
    EXPECT_FALSE(lock.confirmed());
    lock.lock_state_reported(LockState::Unknown);
    EXPECT_FALSE(lock.confirmed());
}

TEST_F(ConnectorLockConfirmationTest, LockCommandTimesOutOnce) {
    lock.lock_commanded(t0);

    EXPECT_EQ(lock.poll(t0 + 4999ms), Event::None);
    EXPECT_EQ(lock.poll(t0 + 5000ms), Event::TimedOut);
    EXPECT_EQ(lock.poll(t0 + 1h), Event::None);
}

TEST_F(ConnectorLockConfirmationTest, NothingTimesOutWithoutLockCommandOrWait) {
    EXPECT_EQ(lock.poll(t0 + 1h), Event::None);
}

TEST_F(ConnectorLockConfirmationTest, LockedReportStopsTheTimeout) {
    lock.lock_commanded(t0);
    lock.lock_state_reported(LockState::Locked);

    EXPECT_EQ(lock.poll(t0 + 1h), Event::None);
}

TEST_F(ConnectorLockConfirmationTest, LockCommandWhileAlreadyLockedDoesNotTimeOut) {
    lock.lock_state_reported(LockState::Locked);
    lock.lock_commanded(t0);

    EXPECT_EQ(lock.poll(t0 + 1h), Event::None);
}

TEST_F(ConnectorLockConfirmationTest, UnlockCommandStopsTheTimeout) {
    lock.lock_commanded(t0);
    lock.unlock_commanded();

    EXPECT_EQ(lock.poll(t0 + 1h), Event::None);
}

TEST_F(ConnectorLockConfirmationTest, RepeatedLockCommandKeepsTheFirstStart) {
    lock.lock_commanded(t0);
    lock.lock_commanded(t0 + 3000ms);

    EXPECT_EQ(lock.poll(t0 + 5000ms), Event::TimedOut);
}

TEST_F(ConnectorLockConfirmationTest, WaitWithoutLockCommandStartsTheTimeout) {
    EXPECT_FALSE(lock.confirmed_or_start_wait(t0));

    EXPECT_EQ(lock.poll(t0 + 4999ms), Event::None);
    EXPECT_EQ(lock.poll(t0 + 5000ms), Event::TimedOut);
}

TEST_F(ConnectorLockConfirmationTest, WaitKeepsAPendingLockCommand) {
    lock.lock_commanded(t0);

    EXPECT_FALSE(lock.confirmed_or_start_wait(t0 + 3000ms));

    EXPECT_EQ(lock.poll(t0 + 5000ms), Event::TimedOut);
}

TEST_F(ConnectorLockConfirmationTest, LockedAfterTimeoutIsReportedOnce) {
    lock.lock_commanded(t0);
    ASSERT_EQ(lock.poll(t0 + 5000ms), Event::TimedOut);

    lock.lock_state_reported(LockState::Locked);

    EXPECT_EQ(lock.poll(t0 + 6000ms), Event::ConfirmedAfterTimeout);
    EXPECT_EQ(lock.poll(t0 + 7000ms), Event::None);
}

TEST_F(ConnectorLockConfirmationTest, LockedWithoutTimeoutReportsNothing) {
    lock.lock_commanded(t0);
    lock.lock_state_reported(LockState::Locked);

    EXPECT_EQ(lock.poll(t0 + 1000ms), Event::None);
}

TEST_F(ConnectorLockConfirmationTest, NewLockCommandAfterTimeoutTimesOutAgain) {
    lock.lock_commanded(t0);
    ASSERT_EQ(lock.poll(t0 + 5000ms), Event::TimedOut);
    lock.unlock_commanded();

    lock.lock_commanded(t0 + 10s);

    EXPECT_EQ(lock.poll(t0 + 15s), Event::TimedOut);
}

} // namespace
