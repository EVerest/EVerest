// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gtest/gtest.h>

#include <optional>

#include <generated/types/grid_support.hpp>

#include "grid_event.hpp"

namespace {

using module::GridEventEdgeDetector;
using module::Transition;

module::EdgeResult step(module::GridEventEdgeDetector& detector, uint8_t condition) {
    const auto result = detector.peek(condition);
    detector.commit(condition);
    return result;
}

TEST(GridEventMapTest, zero_condition_is_no_event) {
    EXPECT_FALSE(module::map_grid_event_condition(0).has_value());
}

TEST(GridEventMapTest, nonzero_condition_maps_to_a_fault) {
    const auto fault = module::map_grid_event_condition(1);
    ASSERT_TRUE(fault.has_value());
    // A concrete enum value is produced; the exact value is the documented default.
    EXPECT_EQ(*fault, types::grid_support::GridEventFault::LocalEmergency);
}

TEST(GridEventMapTest, max_condition_maps_to_a_fault) {
    // Every nonzero code, up to the full uint8_t range, is a fault.
    EXPECT_TRUE(module::map_grid_event_condition(255).has_value());
}

TEST(GridEventEdgeTest, no_event_with_no_prior_fault_yields_none) {
    GridEventEdgeDetector detector;
    const auto result = step(detector, 0);
    EXPECT_EQ(result.transition, Transition::None);
    EXPECT_FALSE(result.fault.has_value());
}

TEST(GridEventEdgeTest, rising_edge_emitted_once_then_deduped) {
    GridEventEdgeDetector detector;

    const auto first = step(detector, 1);
    EXPECT_EQ(first.transition, Transition::Rising);
    ASSERT_TRUE(first.fault.has_value());

    // Same condition asserted again must not re-emit.
    const auto second = step(detector, 1);
    EXPECT_EQ(second.transition, Transition::None);
}

TEST(GridEventEdgeTest, falling_edge_emitted_once_carrying_prior_fault) {
    GridEventEdgeDetector detector;

    const auto rising = step(detector, 1);
    ASSERT_EQ(rising.transition, Transition::Rising);
    const auto prior_fault = rising.fault;

    const auto falling = step(detector, 0);
    EXPECT_EQ(falling.transition, Transition::Falling);
    ASSERT_TRUE(falling.fault.has_value());
    EXPECT_EQ(falling.fault, prior_fault);

    // Clearing again with no active fault must not re-emit.
    const auto again = step(detector, 0);
    EXPECT_EQ(again.transition, Transition::None);
}

TEST(GridEventEdgeTest, asserted_twice_then_cleared_is_one_rising_one_falling) {
    GridEventEdgeDetector detector;

    EXPECT_EQ(step(detector, 1).transition, Transition::Rising);
    EXPECT_EQ(step(detector, 1).transition, Transition::None);
    EXPECT_EQ(step(detector, 0).transition, Transition::Falling);
}

TEST(GridEventEdgeTest, change_to_a_different_nonzero_code_stays_in_fault) {
    GridEventEdgeDetector detector;

    EXPECT_EQ(step(detector, 1).transition, Transition::Rising);
    // A different nonzero code while already in fault is treated as still-in-fault:
    // no falling+rising churn for this milestone.
    EXPECT_EQ(step(detector, 2).transition, Transition::None);
    // Clearing still produces exactly one falling edge.
    EXPECT_EQ(step(detector, 0).transition, Transition::Falling);
}

TEST(GridEventEdgeTest, fault_cleared_then_reasserted_re_arms_rising) {
    // The detector's whole purpose: a fault that clears must be able to fire again.
    GridEventEdgeDetector detector;

    EXPECT_EQ(step(detector, 1).transition, Transition::Rising);
    EXPECT_EQ(step(detector, 0).transition, Transition::Falling);
    // Re-arm: the same nonzero condition must produce a fresh Rising.
    EXPECT_EQ(step(detector, 1).transition, Transition::Rising);
}

TEST(GridEventEdgeTest, reset_clears_a_held_fault_so_next_session_re_arms) {
    // Mirrors a session that ends with a fault still held (EV unplugged, loop
    // stops sending condition==0). The next session must start clean and be able
    // to emit Rising again for the same fault.
    GridEventEdgeDetector detector;

    EXPECT_EQ(step(detector, 1).transition, Transition::Rising);
    // Fault never cleared; session ends abruptly.
    detector.reset();
    // Fresh session: the same nonzero condition is a genuinely new fault.
    EXPECT_EQ(step(detector, 1).transition, Transition::Rising);
}

TEST(GridEventEdgeTest, peek_does_not_mutate_state) {
    // A peeked Rising that is never committed (publish failed) must still be Rising
    // on the next peek, so a dropped publish is retried rather than lost.
    GridEventEdgeDetector detector;

    const auto first = detector.peek(1);
    EXPECT_EQ(first.transition, Transition::Rising);
    ASSERT_TRUE(first.fault.has_value());

    // No commit happened: peeking again must report the same Rising.
    const auto retry = detector.peek(1);
    EXPECT_EQ(retry.transition, Transition::Rising);
}

TEST(GridEventEdgeTest, commit_advances_state_so_peek_then_dedupes) {
    GridEventEdgeDetector detector;

    EXPECT_EQ(detector.peek(1).transition, Transition::Rising);
    detector.commit(1);
    // After commit the fault is active: peeking the same condition dedupes.
    EXPECT_EQ(detector.peek(1).transition, Transition::None);
}

} // namespace
