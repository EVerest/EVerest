// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

// Unit tests for the ConnectivityManager slot-selection and websocket attempt-budget decision
// logic, covering OCPP 2.0.1 B10.FR.07 (fallback to the last-successful network profile) and the
// NetworkProfileConnectionAttempts off-by-one fix. The connect loop itself creates a real
// Websocket, so the pure decision helpers are exercised in isolation here.

#include <gtest/gtest.h>

#include <ocpp/common/connectivity_manager.hpp>
#include <ocpp/common/websocket/websocket_base.hpp>

namespace ocpp {
namespace {

// ---------------------------------------------------------------------------
// select_next_network_slot (B10.FR.07 fallback)
// ---------------------------------------------------------------------------

// Acceptance: connected on slot 1 -> priority becomes [2], slot 2 fails its single attempt ->
// after exactly one exhausted slot the manager falls back to slot 1 (last successful).
TEST(SelectNextNetworkSlot, FallsBackToLastSuccessfulAfterSingleEntryExhausted) {
    const std::vector<std::int32_t> priority_slots{2};
    const auto selection = select_next_network_slot(priority_slots, /*current_slot=*/2,
                                                    /*failed_slots_since_success=*/1,
                                                    /*last_successful_slot=*/2 - 1, /*in_fallback=*/false);
    ASSERT_TRUE(selection.has_value());
    EXPECT_EQ(selection->slot, 1);
    EXPECT_TRUE(selection->is_fallback);
}

// With a two-entry list, the fallback only fires once BOTH entries have been exhausted.
TEST(SelectNextNetworkSlot, CyclesThroughListBeforeFallingBack) {
    const std::vector<std::int32_t> priority_slots{1, 2};

    // Slot 1 exhausted first: not yet a full pass, advance to slot 2.
    auto after_first = select_next_network_slot(priority_slots, /*current_slot=*/1,
                                                /*failed_slots_since_success=*/1,
                                                /*last_successful_slot=*/1, /*in_fallback=*/false);
    ASSERT_TRUE(after_first.has_value());
    EXPECT_EQ(after_first->slot, 2);
    EXPECT_FALSE(after_first->is_fallback);

    // Slot 2 exhausted next: full pass done, fall back to last successful (slot 1).
    auto after_second = select_next_network_slot(priority_slots, /*current_slot=*/2,
                                                 /*failed_slots_since_success=*/2,
                                                 /*last_successful_slot=*/1, /*in_fallback=*/false);
    ASSERT_TRUE(after_second.has_value());
    EXPECT_EQ(after_second->slot, 1);
    EXPECT_TRUE(after_second->is_fallback);
}

// While in fallback but not yet dialing the fallback target, stay pinned to the last-successful
// profile regardless of the list.
TEST(SelectNextNetworkSlot, StaysPinnedWhileTargetNotYetTried) {
    const std::vector<std::int32_t> priority_slots{2};
    const auto selection = select_next_network_slot(priority_slots, /*current_slot=*/2,
                                                    /*failed_slots_since_success=*/99,
                                                    /*last_successful_slot=*/1, /*in_fallback=*/true);
    ASSERT_TRUE(selection.has_value());
    EXPECT_EQ(selection->slot, 1);
    EXPECT_TRUE(selection->is_fallback);
}

// Sticky pin (B10.FR.07 / OCTT TC_B_49_CS): a failed dial to the fallback target keeps re-dialing
// that same last-successful slot rather than resuming priority-list cycling. The tool deliberately
// rejects the first fallback connection attempt; the pin must survive it.
TEST(SelectNextNetworkSlot, StaysPinnedWhenFallbackTargetFails) {
    const std::vector<std::int32_t> priority_slots{1, 2};
    const auto selection = select_next_network_slot(priority_slots, /*current_slot=*/1,
                                                    /*failed_slots_since_success=*/99,
                                                    /*last_successful_slot=*/1, /*in_fallback=*/true);
    ASSERT_TRUE(selection.has_value());
    EXPECT_EQ(selection->slot, 1);
    EXPECT_TRUE(selection->is_fallback);
}

// Without a known last-successful slot there is no fallback target: keep cycling the list.
TEST(SelectNextNetworkSlot, NoFallbackWithoutLastSuccessful) {
    const std::vector<std::int32_t> priority_slots{2};
    const auto selection = select_next_network_slot(priority_slots, /*current_slot=*/2,
                                                    /*failed_slots_since_success=*/5,
                                                    /*last_successful_slot=*/std::nullopt, /*in_fallback=*/false);
    ASSERT_TRUE(selection.has_value());
    EXPECT_EQ(selection->slot, 2);
    EXPECT_FALSE(selection->is_fallback);
}

// The normal (pre-exhaustion) path advances to the next entry, wrapping around the list.
TEST(SelectNextNetworkSlot, WrapsAroundList) {
    const std::vector<std::int32_t> priority_slots{1, 2, 3};
    auto next = select_next_network_slot(priority_slots, /*current_slot=*/3, /*failed_slots_since_success=*/1,
                                         /*last_successful_slot=*/std::nullopt, /*in_fallback=*/false);
    ASSERT_TRUE(next.has_value());
    EXPECT_EQ(next->slot, 1);
    EXPECT_FALSE(next->is_fallback);
}

TEST(SelectNextNetworkSlot, EmptyListReturnsNullopt) {
    const std::vector<std::int32_t> priority_slots{};
    EXPECT_FALSE(select_next_network_slot(priority_slots, /*current_slot=*/1, /*failed_slots_since_success=*/1,
                                          /*last_successful_slot=*/1, /*in_fallback=*/false)
                     .has_value());
}

// ---------------------------------------------------------------------------
// should_attempt_reconnect (NetworkProfileConnectionAttempts budget)
// ---------------------------------------------------------------------------

// N == 1 must yield exactly one attempt: after the first attempt (attempts_made == 1) no reconnect.
TEST(ShouldAttemptReconnect, ExactlyOneAttemptWhenBudgetIsOne) {
    EXPECT_FALSE(should_attempt_reconnect(/*attempts_made=*/1, /*max_connection_attempts=*/1));
}

// N == 3 must yield exactly three attempts.
TEST(ShouldAttemptReconnect, ExactlyNAttempts) {
    EXPECT_TRUE(should_attempt_reconnect(1, 3));  // after attempt 1 -> retry
    EXPECT_TRUE(should_attempt_reconnect(2, 3));  // after attempt 2 -> retry
    EXPECT_FALSE(should_attempt_reconnect(3, 3)); // after attempt 3 -> stop
}

// -1 means unlimited attempts.
TEST(ShouldAttemptReconnect, UnlimitedWhenNegativeOne) {
    EXPECT_TRUE(should_attempt_reconnect(1, -1));
    EXPECT_TRUE(should_attempt_reconnect(1000, -1));
}

// get_reconnect_backoff_ms (OCPP 2.0.1 part 4 section 5.3 reconnect backoff): these tests replay
// the ConnectivityManager loop, feeding the previous result back in as previous_backoff_ms, with
// RandomRange pinned to 0 for determinism.

// WaitMinimum=13, Random=0, Repeat=0: no doublings, so every interval is exactly 13s. This is the
// TC_B_49_CS configuration, where the regression scheduled re-dials 2s apart instead of >= 13s.
TEST(GetReconnectBackoffMs, ConstantWhenRepeatIsZero) {
    long prev = 0;
    for (int attempt = 1; attempt <= 5; ++attempt) {
        prev = get_reconnect_backoff_ms(attempt, prev, /*wait_minimum_s=*/13, /*repeat_times=*/0,
                                        /*random_range_s=*/0);
        EXPECT_EQ(prev, 13000) << "attempt " << attempt;
    }
}

// WaitMinimum=13, Random=0, Repeat=2: doubles twice (13, 26, 52) then stays flat at 52s.
TEST(GetReconnectBackoffMs, DoublesUpToRepeatTimesThenFlat) {
    const long expected[] = {13000, 26000, 52000, 52000, 52000};
    long prev = 0;
    for (int attempt = 1; attempt <= 5; ++attempt) {
        prev = get_reconnect_backoff_ms(attempt, prev, /*wait_minimum_s=*/13, /*repeat_times=*/2,
                                        /*random_range_s=*/0);
        EXPECT_EQ(prev, expected[attempt - 1]) << "attempt " << attempt;
    }
}

// The first attempt (attempt_number == 1) always yields exactly WaitMinimum regardless of the base,
// which is what makes an attempt-counter reset on a successful connection restart the sequence at
// WaitMinimum. Simulate a reset by feeding attempt_number == 1 again with a stale previous value.
TEST(GetReconnectBackoffMs, FirstAttemptIgnoresPreviousBackoff) {
    // Ramp up a few attempts...
    long prev = get_reconnect_backoff_ms(1, 0, 13, 5, 0);
    prev = get_reconnect_backoff_ms(2, prev, 13, 5, 0);
    EXPECT_EQ(prev, 26000);
    // ...then a reset (counter back to 1) starts over at WaitMinimum, ignoring the stale 26000.
    const long after_reset = get_reconnect_backoff_ms(1, prev, 13, 5, 0);
    EXPECT_EQ(after_reset, 13000);
}

// attempt_number <= 0 is treated as the first wait (defensive; the counter is 1-based in practice).
TEST(GetReconnectBackoffMs, NonPositiveAttemptTreatedAsFirst) {
    EXPECT_EQ(get_reconnect_backoff_ms(0, 999999, 13, 3, 0), 13000);
}

// With a positive RandomRange the jitter stays within [0, range] seconds on top of the schedule.
TEST(GetReconnectBackoffMs, RandomRangeStaysWithinBounds) {
    for (int i = 0; i < 50; ++i) {
        const long first = get_reconnect_backoff_ms(1, 0, /*wait_minimum_s=*/13, /*repeat_times=*/3,
                                                    /*random_range_s=*/5);
        EXPECT_GE(first, 13000);
        EXPECT_LE(first, 13000 + 5000);
    }
}

// WebsocketBase reconnect suppression (TC_B_45_CS / TC_A_10_CS): the full drop-and-redial loop
// needs a live server, so the decision is exercised through should_reconnect(), the single gate
// the client loop consults before scheduling another attempt.

class FakeWebsocket : public WebsocketBase {
public:
    // Mirror the real connection options setup and the fresh-connect clear of the suppression flag
    // (WebsocketLibwebsockets::start_connecting clears reconnect_suppressed next to shutting_down).
    void configure(int max_connection_attempts) {
        WebsocketConnectionOptions options{};
        options.max_connection_attempts = max_connection_attempts;
        this->set_connection_options_base(options);
    }
    bool start_connecting() override {
        this->connection_attempts = 1;
        this->clear_reconnect_suppression();
        return true;
    }
    void set_connection_options(const WebsocketConnectionOptions& options) override {
        this->set_connection_options_base(options);
    }
    void reconnect(long /*delay*/) override {
    }
    void close(const WebsocketCloseReason /*code*/, const std::string& /*reason*/) override {
    }
    bool send(const std::string& /*message*/) override {
        return true;
    }

private:
    void ping() override {
    }
};

// Baseline: with attempts left and no suppression, the loop is allowed to reconnect.
TEST(WebsocketReconnectSuppression, ReconnectsWhenBudgetLeftAndNotSuppressed) {
    FakeWebsocket ws;
    ws.configure(/*max_connection_attempts=*/-1); // unlimited
    EXPECT_TRUE(ws.should_reconnect());
}

// TC_B_45_CS / TC_A_10_CS: once suppressed, should_reconnect() is false even with attempts left and
// even for the "always reconnect" peer-close path, so the loop finalizes instead of re-dialing.
TEST(WebsocketReconnectSuppression, SuppressBlocksReconnectDespiteBudget) {
    FakeWebsocket ws;
    ws.configure(/*max_connection_attempts=*/-1); // unlimited: budget alone would always retry
    ws.suppress_reconnect();
    EXPECT_FALSE(ws.should_reconnect());
}

// A fresh connect (post-reboot, or a within-process re-connect) clears the suppression so normal
// auto-reconnect resumes; the flag is armed only for the imminent-reset window.
TEST(WebsocketReconnectSuppression, FreshConnectClearsSuppression) {
    FakeWebsocket ws;
    ws.configure(/*max_connection_attempts=*/-1);
    ws.suppress_reconnect();
    ASSERT_FALSE(ws.should_reconnect());
    ws.start_connecting(); // fresh connect clears the flag (mirrors start_connecting)
    EXPECT_TRUE(ws.should_reconnect());
}

// Suppression is orthogonal to the section 5.3 attempt budget: an exhausted budget still stops reconnect
// regardless of the flag (TC_B_49/B_46 backoff path untouched).
TEST(WebsocketReconnectSuppression, ExhaustedBudgetStopsRegardlessOfFlag) {
    FakeWebsocket ws;
    ws.configure(/*max_connection_attempts=*/1); // exactly one attempt allowed
    // connection_attempts starts at 1 -> after the first attempt the budget is spent.
    EXPECT_FALSE(ws.should_reconnect());
}

} // namespace
} // namespace ocpp
