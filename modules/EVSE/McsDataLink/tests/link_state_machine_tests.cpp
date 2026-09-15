// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
//
// Tests for the MCS data link state machine (main/link_state_machine.cpp) through its effect seam:
// actions only append effects, so each test asserts the exact publish/timer sequence, see describe().

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "link_state_machine.hpp"

namespace {

using namespace module::main;

std::string describe(effect const& item) {
    switch (item.what) {
    case effect::kind::publish_state:
        return std::string("state:") + to_string(item.state);
    case effect::kind::publish_dlink_ready:
        return item.ready ? "ready:1" : "ready:0";
    case effect::kind::publish_request_error_routine:
        return "error_routine";
    case effect::kind::publish_ev_mac:
        return "mac:" + item.mac;
    case effect::kind::start_timer:
        return std::string("timer+") + to_string(item.timer) + "@" + std::to_string(item.timeout_ms);
    case effect::kind::stop_timer:
        return std::string("timer-") + to_string(item.timer);
    }
    return "?";
}

using trace = std::vector<std::string>;

trace describe(std::vector<effect> const& items) {
    trace out;
    out.reserve(items.size());
    for (auto const& item : items) {
        out.push_back(describe(item));
    }
    return out;
}

link_config default_config() {
    link_config config;
    config.conn_retry_max = 3;
    config.link_detect_timeout_ms = 4000;
    // At the standard's maxima the window closes with the first TT_EV_link_detect expiry, so the
    // default never repeats. Repetition cases shorten link_detect_timeout_ms.
    config.sync_repetition_ms = 4000;
    config.retry_wait_ms = 3000;
    config.publish_ev_mac = true;
    return config;
}

/// The machine, started, with the start effects drained.
class fixture {
public:
    explicit fixture(link_config config = default_config()) : m_fsm(config) {
        m_fsm.start();
        m_start_trace = describe(m_fsm.take_effects());
    }

    link_state_machine& fsm() {
        return m_fsm;
    }

    /// Effects since the previous call.
    trace taken() {
        return describe(m_fsm.take_effects());
    }

    trace const& start_trace() const {
        return m_start_trace;
    }

    /// Reach MATCHED via enter_bcd + carrier_up and drop the effects.
    void reach_matched() {
        m_fsm.enter_bcd(false);
        m_fsm.carrier_up();
        (void)m_fsm.take_effects();
        EXPECT_EQ(internal_state::matched, m_fsm.state());
    }

private:
    link_state_machine m_fsm;
    trace m_start_trace;
};

// --- start up and the plain success paths -----------------------------------------------------

TEST(LinkStateMachine, StartPublishesUnmatchedOnce) {
    fixture f;
    EXPECT_EQ(trace({"state:UNMATCHED"}), f.start_trace());
    EXPECT_EQ(internal_state::unmatched, f.fsm().state());
    EXPECT_EQ(link_state::unmatched, f.fsm().published_state());
    EXPECT_FALSE(f.fsm().dlink_ready());
    EXPECT_EQ(0, f.fsm().retry_count());
}

TEST(LinkStateMachine, EnterBcdWithoutCarrierStartsMatchingAndTheLinkDetectTimer) {
    fixture f;
    f.fsm().enter_bcd(false);

    EXPECT_EQ(trace({"timer+sync_repetition@4000", "state:MATCHING", "timer+link_detect@4000"}), f.taken());
    EXPECT_EQ(internal_state::matching, f.fsm().state());
    EXPECT_FALSE(f.fsm().dlink_ready());
}

TEST(LinkStateMachine, CarrierUpWhileMatchingMatchesAndCancelsTheTimer) {
    fixture f;
    f.fsm().enter_bcd(false);
    (void)f.taken();

    f.fsm().carrier_up();

    EXPECT_EQ(trace({"timer-link_detect", "state:MATCHED", "ready:1"}), f.taken());
    EXPECT_EQ(internal_state::matched, f.fsm().state());
    EXPECT_TRUE(f.fsm().dlink_ready());
}

// V2G10-023 needs state B and link up in either order; on SPE the PHY can be up before state B.
TEST(LinkStateMachine, EnterBcdWithCarrierAlreadyUpMatchesImmediately) {
    fixture f;
    f.fsm().enter_bcd(true);

    EXPECT_EQ(trace({"state:MATCHED", "ready:1"}), f.taken());
    EXPECT_EQ(internal_state::matched, f.fsm().state());
    EXPECT_TRUE(f.fsm().dlink_ready());
}

// V2G10-054/-058 with budget left: UNMATCHED, no dlink_ready(false), then the CC.5.2.3.2 restart.
TEST(LinkStateMachine, LinkDetectTimeoutHandsOverToTheRestart) {
    fixture f;
    f.fsm().enter_bcd(false);
    (void)f.taken();

    f.fsm().link_detect_timeout(false);

    EXPECT_EQ(trace({"timer-link_detect", "state:UNMATCHED", "timer+retry_wait@3000"}), f.taken());
    EXPECT_EQ(internal_state::retry_wait, f.fsm().state());
    EXPECT_EQ(1, f.fsm().retry_count());

    f.fsm().retry_wait_elapsed(true);
    EXPECT_EQ(trace({"timer-retry_wait", "error_routine", "state:MATCHED", "ready:1"}), f.taken())
        << "a carrier that came up late is picked up by the re-arm";
}

TEST(LinkStateMachine, LinkDetectTimeoutRestartWithoutCarrierRunsLinkDetectAgain) {
    fixture f;
    f.fsm().enter_bcd(false);
    (void)f.taken();
    f.fsm().link_detect_timeout(false);
    (void)f.taken();

    f.fsm().retry_wait_elapsed(false);
    EXPECT_EQ(trace({"timer-retry_wait", "error_routine", "state:MATCHING", "timer+link_detect@4000"}), f.taken())
        << "no sync_repetition window: this is a C_conn_retry, not a new comm-init";

    f.fsm().carrier_up();
    EXPECT_EQ(trace({"timer-link_detect", "state:MATCHED", "ready:1"}), f.taken());
}

TEST(LinkStateMachine, LinkDetectTimeoutWithoutBudgetFailsCommunicationInitialisation) {
    auto config = default_config();
    config.conn_retry_max = 0;
    fixture f(config);
    f.fsm().enter_bcd(false);
    (void)f.taken();

    f.fsm().link_detect_timeout(false);

    EXPECT_EQ(trace({"timer-link_detect", "state:UNMATCHED"}), f.taken());
    EXPECT_EQ(internal_state::unmatched, f.fsm().state());
    EXPECT_EQ(0, f.fsm().retry_count());
}

TEST(LinkStateMachine, RepeatedInitialisationFailuresExhaustTheBudget) {
    auto config = default_config();
    config.conn_retry_max = 2;
    fixture f(config);
    f.fsm().enter_bcd(false);
    (void)f.taken();

    for (int attempt = 1; attempt <= 2; ++attempt) {
        f.fsm().link_detect_timeout(false);
        ASSERT_EQ(internal_state::retry_wait, f.fsm().state()) << "attempt " << attempt;
        ASSERT_EQ(attempt, f.fsm().retry_count());
        f.fsm().retry_wait_elapsed(false);
        (void)f.taken();
        ASSERT_EQ(internal_state::matching, f.fsm().state());
    }

    f.fsm().link_detect_timeout(false);
    EXPECT_EQ(trace({"timer-link_detect", "state:UNMATCHED"}), f.taken());
    EXPECT_EQ(internal_state::unmatched, f.fsm().state()) << "budget exhausted: no further restart";
}

TEST(LinkStateMachine, LeaveBcdTearsDownTheLink) {
    fixture f;
    f.reach_matched();

    f.fsm().leave_bcd();

    EXPECT_EQ(trace({"timer-sync_repetition", "ready:0", "state:UNMATCHED"}), f.taken());
    EXPECT_EQ(internal_state::unmatched, f.fsm().state());
    EXPECT_FALSE(f.fsm().dlink_ready());
}

TEST(LinkStateMachine, LeaveBcdWhileMatchingCancelsTheTimerAndDoesNotWithdrawWhatWasNeverIssued) {
    fixture f;
    f.fsm().enter_bcd(false);
    (void)f.taken();

    f.fsm().leave_bcd();

    EXPECT_EQ(trace({"timer-link_detect", "timer-sync_repetition", "state:UNMATCHED"}), f.taken());
}

// --- reset ------------------------------------------------------------------------------------

// reset(false) is the only reset EvseManager sends (reset(true) is commented out); it must not latch.
TEST(LinkStateMachine, ResetDisableTearsDownButLeavesTheModuleReady) {
    fixture f;
    f.reach_matched();

    f.fsm().reset(false);
    EXPECT_EQ(trace({"timer-sync_repetition", "ready:0", "state:UNMATCHED"}), f.taken());
    EXPECT_EQ(internal_state::unmatched, f.fsm().state());

    f.fsm().enter_bcd(true);
    EXPECT_EQ(trace({"state:MATCHED", "ready:1"}), f.taken()) << "the next session must still work";
    EXPECT_EQ(internal_state::matched, f.fsm().state());
}

TEST(LinkStateMachine, RepeatedResetsAreIdempotent) {
    fixture f;
    f.fsm().reset(false);
    EXPECT_TRUE(f.taken().empty()) << "already published UNMATCHED, nothing changed";
    f.fsm().reset(true);
    EXPECT_TRUE(f.taken().empty());
    EXPECT_EQ(internal_state::unmatched, f.fsm().state());

    f.fsm().enter_bcd(true);
    EXPECT_EQ(trace({"state:MATCHED", "ready:1"}), f.taken());
}

TEST(LinkStateMachine, ResetEnableWhileMatchedTearsTheLinkDown) {
    fixture f;
    f.reach_matched();

    f.fsm().reset(true);

    EXPECT_EQ(trace({"timer-sync_repetition", "ready:0", "state:UNMATCHED"}), f.taken());
    EXPECT_EQ(internal_state::unmatched, f.fsm().state());
}

// --- carrier loss while matched ---------------------------------------------------------------

// V2G10-036: D-LINK_READY(no link). UNMATCHED is published before MATCHING so consumers see the drop.
TEST(LinkStateMachine, CarrierLossWhileMatchedReportsDownAndRestartsMatching) {
    fixture f;
    f.reach_matched();

    f.fsm().carrier_down();

    EXPECT_EQ(trace({"ready:0", "state:UNMATCHED", "state:MATCHING", "timer+link_detect@4000"}), f.taken());
    EXPECT_EQ(internal_state::matching, f.fsm().state());
    EXPECT_FALSE(f.fsm().dlink_ready());
    EXPECT_EQ(1, f.fsm().retry_count());
}

// Only a fresh carrier_up edge re-matches, so a liveness loss with the carrier still up cannot loop.
TEST(LinkStateMachine, RestartedMatchingWaitsForAFreshCarrierEdge) {
    fixture f;
    f.reach_matched();
    f.fsm().link_lost();
    (void)f.taken();
    ASSERT_EQ(internal_state::matching, f.fsm().state());

    EXPECT_TRUE(f.taken().empty());
    EXPECT_EQ(internal_state::matching, f.fsm().state());

    f.fsm().carrier_up();
    EXPECT_EQ(trace({"timer-link_detect", "state:MATCHED", "ready:1"}), f.taken());
}

TEST(LinkStateMachine, LivenessLossThatDoesNotRecoverHandsOverToTheRestart) {
    fixture f;
    f.reach_matched();
    f.fsm().link_lost();
    (void)f.taken();
    ASSERT_EQ(1, f.fsm().retry_count());

    f.fsm().link_detect_timeout(false);

    EXPECT_EQ(trace({"timer-link_detect", "state:UNMATCHED", "timer+retry_wait@3000"}), f.taken());
    EXPECT_EQ(internal_state::retry_wait, f.fsm().state());
    EXPECT_EQ(2, f.fsm().retry_count());
}

TEST(LinkStateMachine, CarrierLossWithoutRetryBudgetStaysUnmatched) {
    auto config = default_config();
    config.conn_retry_max = 0;
    fixture f(config);
    f.reach_matched();

    f.fsm().carrier_down();

    EXPECT_EQ(trace({"ready:0", "state:UNMATCHED"}), f.taken());
    EXPECT_EQ(internal_state::unmatched, f.fsm().state());
    EXPECT_EQ(0, f.fsm().retry_count());
}

// C_conn_retry is per connection: a match in between refunds nothing, or a flapping link retries forever.
TEST(LinkStateMachine, RetryBudgetIsSpentAcrossSuccessfulMatchesAndThenExhausts) {
    auto config = default_config();
    config.conn_retry_max = 2;
    fixture f(config);
    f.reach_matched();

    f.fsm().carrier_down();
    ASSERT_EQ(internal_state::matching, f.fsm().state());
    EXPECT_EQ(1, f.fsm().retry_count());
    f.fsm().carrier_up();
    ASSERT_EQ(internal_state::matched, f.fsm().state());

    f.fsm().carrier_down();
    ASSERT_EQ(internal_state::matching, f.fsm().state());
    EXPECT_EQ(2, f.fsm().retry_count());
    f.fsm().carrier_up();
    ASSERT_EQ(internal_state::matched, f.fsm().state());
    (void)f.taken();

    f.fsm().carrier_down();
    EXPECT_EQ(trace({"ready:0", "state:UNMATCHED"}), f.taken());
    EXPECT_EQ(internal_state::unmatched, f.fsm().state()) << "budget exhausted, no third attempt";
    EXPECT_EQ(2, f.fsm().retry_count());
}

TEST(LinkStateMachine, EndingTheConnectionRefillsTheRetryBudget) {
    auto config = default_config();
    config.conn_retry_max = 1;
    fixture f(config);
    f.reach_matched();
    f.fsm().carrier_down();
    ASSERT_EQ(1, f.fsm().retry_count());

    f.fsm().leave_bcd();
    EXPECT_EQ(0, f.fsm().retry_count());

    f.fsm().enter_bcd(true);
    (void)f.taken();
    f.fsm().carrier_down();
    EXPECT_EQ(internal_state::matching, f.fsm().state()) << "a new connection retries again";
    EXPECT_EQ(1, f.fsm().retry_count());
}

TEST(LinkStateMachine, DlinkTerminateAndResetAlsoRefillTheRetryBudget) {
    auto config = default_config();
    config.conn_retry_max = 1;

    {
        fixture f(config);
        f.reach_matched();
        f.fsm().carrier_down();
        ASSERT_EQ(1, f.fsm().retry_count());
        f.fsm().dlink_terminate();
        EXPECT_EQ(0, f.fsm().retry_count());
        EXPECT_EQ(internal_state::unmatched, f.fsm().state());
    }
    {
        fixture f(config);
        f.reach_matched();
        f.fsm().carrier_down();
        ASSERT_EQ(1, f.fsm().retry_count());
        f.fsm().reset(true);
        EXPECT_EQ(0, f.fsm().retry_count());
    }
}

// --- dlink_error: the CC.5.2.3.2 host side restart --------------------------------------------

TEST(LinkStateMachine, DlinkErrorWaitsThenRequestsTheErrorRoutineAndRematchesOnStandingCarrier) {
    fixture f;
    f.reach_matched();

    f.fsm().dlink_error();
    EXPECT_EQ(trace({"timer-sync_repetition", "ready:0", "state:UNMATCHED", "timer+retry_wait@3000"}), f.taken());
    EXPECT_EQ(internal_state::retry_wait, f.fsm().state());
    EXPECT_EQ(link_state::unmatched, f.fsm().published_state());
    EXPECT_EQ(1, f.fsm().retry_count());

    // The CC.5.2.3.2 restart is EvseManager's error routine (B0-to-B toggle). The module re-arms
    // matching itself: on MCS the synthesized CP state stays B while mated, so no enter_bcd follows.
    f.fsm().retry_wait_elapsed(true);
    EXPECT_EQ(trace({"timer-retry_wait", "error_routine", "state:MATCHED", "ready:1"}), f.taken());
    EXPECT_EQ(internal_state::matched, f.fsm().state());
}

TEST(LinkStateMachine, DlinkErrorRestartWithoutCarrierWaitsForTheLinkInMatching) {
    fixture f;
    f.reach_matched();

    f.fsm().dlink_error();
    (void)f.taken();
    f.fsm().carrier_down(); // no row in restart_wait: the guard keeps running
    EXPECT_TRUE(f.taken().empty());

    // No carrier: restart lands in MATCHING, TT_EV_link_detect as T_conn_resume analog, no sync_repetition.
    f.fsm().retry_wait_elapsed(false);
    EXPECT_EQ(trace({"timer-retry_wait", "error_routine", "state:MATCHING", "timer+link_detect@4000"}), f.taken());
    EXPECT_EQ(internal_state::matching, f.fsm().state());

    f.fsm().carrier_up();
    EXPECT_EQ(trace({"timer-link_detect", "state:MATCHED", "ready:1"}), f.taken());
}

// Charger::request_error_sequence() fires signal_slac_reset -> reset(false) besides the CP toggle.
TEST(LinkStateMachine, TheErrorRoutinesOwnResetIsAbsorbed) {
    fixture f;
    f.reach_matched();
    f.fsm().dlink_error();
    f.fsm().retry_wait_elapsed(true);
    (void)f.taken();
    ASSERT_EQ(internal_state::matched, f.fsm().state());

    f.fsm().reset(false);
    EXPECT_TRUE(f.taken().empty());
    EXPECT_EQ(internal_state::matched, f.fsm().state());
    EXPECT_TRUE(f.fsm().dlink_ready());
    EXPECT_EQ(1, f.fsm().retry_count()) << "the routine's reset must not refill the budget";
    EXPECT_EQ(0, f.fsm().ignored_events()) << "consumed, not ignored";

    f.fsm().reset(false);
    EXPECT_EQ(trace({"ready:0", "state:UNMATCHED"}), f.taken()) << "only one reset is the routine's";
    EXPECT_EQ(0, f.fsm().retry_count());
}

TEST(LinkStateMachine, TheErrorRoutinesOwnResetIsAbsorbedWhileMatchingToo) {
    fixture f;
    f.reach_matched();
    f.fsm().dlink_error();
    f.fsm().retry_wait_elapsed(false);
    (void)f.taken();
    ASSERT_EQ(internal_state::matching, f.fsm().state());

    f.fsm().reset(false);
    EXPECT_TRUE(f.taken().empty());
    EXPECT_EQ(internal_state::matching, f.fsm().state()) << "TT_EV_link_detect keeps running";

    f.fsm().carrier_up();
    EXPECT_EQ(trace({"timer-link_detect", "state:MATCHED", "ready:1"}), f.taken());
}

TEST(LinkStateMachine, ConnRetryMaxBoundsTheErrorRoutineRestarts) {
    auto config = default_config();
    config.conn_retry_max = 1;
    fixture f(config);
    f.reach_matched();

    f.fsm().dlink_error();
    (void)f.taken();
    f.fsm().retry_wait_elapsed(true);
    EXPECT_EQ(trace({"timer-retry_wait", "error_routine", "state:MATCHED", "ready:1"}), f.taken());
    f.fsm().reset(false);
    EXPECT_TRUE(f.taken().empty());

    f.fsm().dlink_error();
    EXPECT_EQ(trace({"ready:0", "state:UNMATCHED"}), f.taken()) << "budget spent: no second routine";
    EXPECT_EQ(internal_state::unmatched, f.fsm().state());
}

TEST(LinkStateMachine, LeaveBcdAfterTheErrorRoutineForgetsThePendingReset) {
    fixture f;
    f.reach_matched();
    f.fsm().dlink_error();
    f.fsm().retry_wait_elapsed(true);
    f.fsm().leave_bcd();
    f.fsm().enter_bcd(true);
    (void)f.taken();
    ASSERT_EQ(internal_state::matched, f.fsm().state());

    f.fsm().reset(false);
    EXPECT_EQ(trace({"ready:0", "state:UNMATCHED"}), f.taken()) << "a new connection's reset is a teardown";
}

TEST(LinkStateMachine, DlinkTerminateAfterTheErrorRoutineForgetsThePendingReset) {
    fixture f;
    f.reach_matched();
    f.fsm().dlink_error();
    f.fsm().retry_wait_elapsed(true);
    f.fsm().dlink_terminate();
    f.fsm().enter_bcd(true);
    (void)f.taken();
    ASSERT_EQ(internal_state::matched, f.fsm().state());

    f.fsm().reset(false);
    EXPECT_EQ(trace({"ready:0", "state:UNMATCHED"}), f.taken());
}

TEST(LinkStateMachine, ADlinkErrorBeforeTheRoutinesResetArrivesKeepsItPending) {
    fixture f;
    f.reach_matched();
    f.fsm().dlink_error();
    f.fsm().retry_wait_elapsed(true);
    f.fsm().dlink_error();
    (void)f.taken();
    ASSERT_EQ(internal_state::retry_wait, f.fsm().state());

    f.fsm().reset(false);
    EXPECT_TRUE(f.taken().empty()) << "the guard keeps running";
    EXPECT_EQ(internal_state::retry_wait, f.fsm().state());
    EXPECT_EQ(2, f.fsm().retry_count());
}

TEST(LinkStateMachine, DlinkErrorWithoutRetryBudgetNeverRequestsARestart) {
    auto config = default_config();
    config.conn_retry_max = 0;
    fixture f(config);
    f.reach_matched();

    f.fsm().dlink_error();

    EXPECT_EQ(trace({"timer-sync_repetition", "ready:0", "state:UNMATCHED"}), f.taken());
    EXPECT_EQ(internal_state::unmatched, f.fsm().state());
}

TEST(LinkStateMachine, DlinkErrorIsAcceptedFromEveryLiveState) {
    {
        fixture f;
        f.fsm().enter_bcd(false);
        (void)f.taken();
        f.fsm().dlink_error();
        EXPECT_EQ(trace({"timer-link_detect", "timer-sync_repetition", "state:UNMATCHED", "timer+retry_wait@3000"}),
                  f.taken());
        EXPECT_EQ(internal_state::retry_wait, f.fsm().state());
    }
    {
        fixture f;
        f.reach_matched();
        f.fsm().dlink_pause();
        (void)f.taken();
        f.fsm().dlink_error();
        EXPECT_EQ(trace({"timer-sync_repetition", "ready:0", "state:UNMATCHED", "timer+retry_wait@3000"}), f.taken());
        EXPECT_EQ(internal_state::retry_wait, f.fsm().state());
    }
    {
        fixture f;
        f.fsm().dlink_error();
        EXPECT_EQ(trace({"timer+retry_wait@3000"}), f.taken());
        EXPECT_EQ(internal_state::retry_wait, f.fsm().state());
    }
}

TEST(LinkStateMachine, EnterBcdDuringTheRestartWaitCancelsIt) {
    fixture f;
    f.reach_matched();
    f.fsm().dlink_error();
    (void)f.taken();
    ASSERT_EQ(internal_state::retry_wait, f.fsm().state());

    f.fsm().enter_bcd(true);

    EXPECT_EQ(trace({"timer-retry_wait", "state:MATCHED", "ready:1"}), f.taken());
    EXPECT_EQ(internal_state::matched, f.fsm().state());
}

// The wait is a mandatory >= 3 s guard with S3 open; a carrier edge does not shortcut it.
TEST(LinkStateMachine, CarrierUpDuringTheRestartWaitIsIgnored) {
    fixture f;
    f.reach_matched();
    f.fsm().dlink_error();
    (void)f.taken();

    auto const ignored_before = f.fsm().ignored_events();
    f.fsm().carrier_up();

    EXPECT_TRUE(f.taken().empty());
    EXPECT_EQ(internal_state::retry_wait, f.fsm().state());
    EXPECT_EQ(ignored_before + 1, f.fsm().ignored_events());
}

// restart_wait swallows a repeated dlink_error itself, or the outer row would restart the guard.
TEST(LinkStateMachine, ARepeatedDlinkErrorDuringTheRestartWaitChangesNothing) {
    fixture f;
    f.reach_matched();
    f.fsm().dlink_error();
    (void)f.taken();
    ASSERT_EQ(internal_state::retry_wait, f.fsm().state());
    ASSERT_EQ(1, f.fsm().retry_count());

    auto const ignored_before = f.fsm().ignored_events();
    f.fsm().dlink_error();

    EXPECT_TRUE(f.taken().empty()) << "the guard must not be restarted";
    EXPECT_EQ(internal_state::retry_wait, f.fsm().state());
    EXPECT_EQ(1, f.fsm().retry_count()) << "and no second attempt is spent";
    EXPECT_EQ(ignored_before, f.fsm().ignored_events()) << "consumed by a transition, not unhandled";
}

TEST(LinkStateMachine, LeaveBcdDuringTheRestartWaitCancelsIt) {
    fixture f;
    f.reach_matched();
    f.fsm().dlink_error();
    (void)f.taken();

    f.fsm().leave_bcd();

    EXPECT_EQ(trace({"timer-retry_wait"}), f.taken());
    EXPECT_EQ(internal_state::unmatched, f.fsm().state());
    EXPECT_EQ(0, f.fsm().retry_count());
}

// --- dlink_pause / resume ---------------------------------------------------------------------

// V2G10-041: D-LINK_PAUSE keeps the link logically up; published state stays MATCHED.
TEST(LinkStateMachine, DlinkPauseKeepsEverythingPublishedAsItWas) {
    fixture f;
    f.reach_matched();

    f.fsm().dlink_pause();

    EXPECT_TRUE(f.taken().empty());
    EXPECT_EQ(internal_state::paused, f.fsm().state());
    EXPECT_EQ(link_state::matched, f.fsm().published_state());
    EXPECT_TRUE(f.fsm().dlink_ready());
}

// The EVSE goes to B0 and the PHY may power down, so a carrier drop while paused is not a failure.
TEST(LinkStateMachine, CarrierLossWhilePausedIsExpectedAndSilent) {
    fixture f;
    f.reach_matched();
    f.fsm().dlink_pause();
    (void)f.taken();

    auto const ignored_before = f.fsm().ignored_events();
    f.fsm().carrier_down();

    EXPECT_TRUE(f.taken().empty()) << "no dlink_ready(false) while paused";
    EXPECT_EQ(internal_state::paused, f.fsm().state());
    EXPECT_TRUE(f.fsm().dlink_ready());
    EXPECT_EQ(0, f.fsm().retry_count()) << "an expected power-down does not spend the retry budget";
    EXPECT_EQ(ignored_before + 1, f.fsm().ignored_events());
}

TEST(LinkStateMachine, LivenessLossWhilePausedIsIgnoredToo) {
    fixture f;
    f.reach_matched();
    f.fsm().dlink_pause();
    (void)f.taken();

    f.fsm().link_lost();

    EXPECT_TRUE(f.taken().empty());
    EXPECT_EQ(internal_state::paused, f.fsm().state());
    EXPECT_TRUE(f.fsm().dlink_ready());
}

// V2G10-042: the wake-up re-issues D-LINK_READY although its value did not change.
TEST(LinkStateMachine, CarrierReturnWhilePausedReissuesDlinkReady) {
    fixture f;
    f.reach_matched();
    f.fsm().dlink_pause();
    f.fsm().carrier_down();
    (void)f.taken();

    f.fsm().carrier_up();

    EXPECT_EQ(trace({"ready:1"}), f.taken());
    EXPECT_EQ(internal_state::matched, f.fsm().state());
    EXPECT_TRUE(f.fsm().dlink_ready());
}

TEST(LinkStateMachine, ResumedLinkIsSupervisedAgain) {
    fixture f;
    f.reach_matched();
    f.fsm().dlink_pause();
    f.fsm().carrier_down();
    f.fsm().carrier_up();
    (void)f.taken();
    ASSERT_EQ(internal_state::matched, f.fsm().state());

    f.fsm().carrier_down();

    EXPECT_EQ(trace({"ready:0", "state:UNMATCHED", "state:MATCHING", "timer+link_detect@4000"}), f.taken());
}

TEST(LinkStateMachine, LeaveBcdAndResetStillTearDownFromPaused) {
    {
        fixture f;
        f.reach_matched();
        f.fsm().dlink_pause();
        (void)f.taken();
        f.fsm().leave_bcd();
        EXPECT_EQ(trace({"timer-sync_repetition", "ready:0", "state:UNMATCHED"}), f.taken());
        EXPECT_EQ(internal_state::unmatched, f.fsm().state());
    }
    {
        fixture f;
        f.reach_matched();
        f.fsm().dlink_pause();
        (void)f.taken();
        f.fsm().reset(false);
        EXPECT_EQ(trace({"timer-sync_repetition", "ready:0", "state:UNMATCHED"}), f.taken());
        EXPECT_EQ(internal_state::unmatched, f.fsm().state());
    }
    {
        fixture f;
        f.reach_matched();
        f.fsm().dlink_pause();
        (void)f.taken();
        f.fsm().dlink_terminate();
        EXPECT_EQ(trace({"timer-sync_repetition", "ready:0", "state:UNMATCHED"}), f.taken());
        EXPECT_EQ(internal_state::unmatched, f.fsm().state());
    }
}

TEST(LinkStateMachine, DlinkPauseOutsideMatchedIsIgnored) {
    fixture f;
    auto const ignored_before = f.fsm().ignored_events();

    f.fsm().dlink_pause();
    EXPECT_TRUE(f.taken().empty());
    EXPECT_EQ(internal_state::unmatched, f.fsm().state());

    f.fsm().enter_bcd(false);
    (void)f.taken();
    f.fsm().dlink_pause();
    EXPECT_TRUE(f.taken().empty());
    EXPECT_EQ(internal_state::matching, f.fsm().state());

    EXPECT_EQ(ignored_before + 2, f.fsm().ignored_events());
}

// --- ev_mac_address ---------------------------------------------------------------------------

TEST(LinkStateMachine, ReachableNeighbourPublishesTheEvMacOncePerLink) {
    fixture f;
    f.reach_matched();

    f.fsm().neighbor_reachable("0A:1B:2C:D3:E4:F5");
    EXPECT_EQ(trace({"mac:0A:1B:2C:D3:E4:F5"}), f.taken());

    f.fsm().neighbor_reachable("0A:1B:2C:D3:E4:F5");
    EXPECT_TRUE(f.taken().empty()) << "the same address is not republished";

    f.fsm().neighbor_reachable("0A:1B:2C:D3:E4:F6");
    EXPECT_EQ(trace({"mac:0A:1B:2C:D3:E4:F6"}), f.taken());
}

TEST(LinkStateMachine, EvMacIsPublishedWhilePausedButNotWhileUnmatchedOrMatching) {
    fixture f;
    f.reach_matched();
    f.fsm().dlink_pause();
    (void)f.taken();
    // A neighbour answering also ends the pause, hence the D-LINK_READY re-issue.
    f.fsm().neighbor_reachable("0A:1B:2C:D3:E4:F5");
    EXPECT_EQ(trace({"mac:0A:1B:2C:D3:E4:F5", "ready:1"}), f.taken());

    f.fsm().leave_bcd();
    (void)f.taken();
    f.fsm().neighbor_reachable("0A:1B:2C:D3:E4:F5");
    EXPECT_TRUE(f.taken().empty());

    f.fsm().enter_bcd(false);
    (void)f.taken();
    f.fsm().neighbor_reachable("0A:1B:2C:D3:E4:F5");
    EXPECT_TRUE(f.taken().empty());
}

TEST(LinkStateMachine, TheEvMacIsForgottenWhenTheLinkGoesDown) {
    fixture f;
    f.reach_matched();
    f.fsm().neighbor_reachable("0A:1B:2C:D3:E4:F5");
    (void)f.taken();

    f.fsm().leave_bcd();
    f.fsm().enter_bcd(true);
    (void)f.taken();

    f.fsm().neighbor_reachable("0A:1B:2C:D3:E4:F5");
    EXPECT_EQ(trace({"mac:0A:1B:2C:D3:E4:F5"}), f.taken()) << "a new link republishes for autocharge";
}

TEST(LinkStateMachine, EvMacPublishingCanBeDisabled) {
    auto config = default_config();
    config.publish_ev_mac = false;
    fixture f(config);
    f.reach_matched();

    f.fsm().neighbor_reachable("0A:1B:2C:D3:E4:F5");

    EXPECT_TRUE(f.taken().empty());
    EXPECT_EQ(internal_state::matched, f.fsm().state()) << "still an internal transition, not an ignored event";
}

TEST(LinkStateMachine, AnEmptyMacIsNotPublished) {
    fixture f;
    f.reach_matched();

    f.fsm().neighbor_reachable("");

    EXPECT_TRUE(f.taken().empty());
}

// --- events that must be ignored --------------------------------------------------------------

TEST(LinkStateMachine, CarrierUpWithoutEnterBcdDoesNotMatch) {
    fixture f;
    auto const ignored_before = f.fsm().ignored_events();

    f.fsm().carrier_up();

    EXPECT_TRUE(f.taken().empty()) << "V2G10-023 needs state B as well";
    EXPECT_EQ(internal_state::unmatched, f.fsm().state());
    EXPECT_EQ(ignored_before + 1, f.fsm().ignored_events());
}

TEST(LinkStateMachine, RepeatedEnterBcdAndCarrierEdgesAreIgnored) {
    fixture f;
    f.reach_matched();
    auto const ignored_before = f.fsm().ignored_events();

    f.fsm().enter_bcd(true);
    f.fsm().carrier_up();
    f.fsm().link_detect_timeout(false);

    EXPECT_TRUE(f.taken().empty());
    EXPECT_EQ(internal_state::matched, f.fsm().state());
    EXPECT_EQ(ignored_before + 3, f.fsm().ignored_events());
}

TEST(LinkStateMachine, StrayTimerExpiriesAreIgnored) {
    fixture f;
    auto const ignored_before = f.fsm().ignored_events();

    f.fsm().link_detect_timeout(false);
    f.fsm().retry_wait_elapsed(true);

    EXPECT_TRUE(f.taken().empty());
    EXPECT_EQ(internal_state::unmatched, f.fsm().state());
    EXPECT_EQ(ignored_before + 2, f.fsm().ignored_events());
}

// --- TT_sync_repetition (V2G10-055 to -058) ---------------------------------------------------

// The window opens on the enter_bcd that starts MATCHING, not on a C_conn_retry reconnect.
TEST(LinkStateMachine, CommunicationInitialisationOpensTheRepetitionWindow) {
    fixture f;

    f.fsm().enter_bcd(false);

    EXPECT_EQ(trace({"timer+sync_repetition@4000", "state:MATCHING", "timer+link_detect@4000"}), f.taken());
}

TEST(LinkStateMachine, ALinkLossRestartDoesNotReopenTheRepetitionWindow) {
    fixture f;
    f.reach_matched();

    f.fsm().carrier_down();

    EXPECT_EQ(trace({"ready:0", "state:UNMATCHED", "state:MATCHING", "timer+link_detect@4000"}), f.taken())
        << "a reconnect is governed by C_conn_retry, not by TT_sync_repetition";
}

// A leftover window would let the next connection's TT_EV_link_detect expiry take the repeat row.
TEST(LinkStateMachine, EndingTheConnectionClosesTheRepetitionWindow) {
    fixture f;
    f.fsm().enter_bcd(false);
    (void)f.taken();

    f.fsm().leave_bcd();
    EXPECT_EQ(trace({"timer-link_detect", "timer-sync_repetition", "state:UNMATCHED"}), f.taken());

    f.fsm().leave_bcd();
    EXPECT_TRUE(f.taken().empty()) << "closed once";

    f.fsm().enter_bcd(true);
    (void)f.taken();
    f.fsm().reset(false);
    EXPECT_EQ(trace({"ready:0", "state:UNMATCHED"}), f.taken()) << "no window was opened, none to close";
}

TEST(LinkStateMachine, ResetAndDlinkTerminateCloseTheRepetitionWindow) {
    {
        fixture f;
        f.reach_matched();
        f.fsm().reset(false);
        EXPECT_EQ(trace({"timer-sync_repetition", "ready:0", "state:UNMATCHED"}), f.taken());
    }
    {
        fixture f;
        f.reach_matched();
        f.fsm().dlink_terminate();
        EXPECT_EQ(trace({"timer-sync_repetition", "ready:0", "state:UNMATCHED"}), f.taken());
    }
}

// A session existed, so the initialization is over; the restart is a C_conn_retry.
TEST(LinkStateMachine, DlinkErrorClosesTheRepetitionWindow) {
    fixture f;
    f.reach_matched();

    f.fsm().dlink_error();
    EXPECT_EQ(trace({"timer-sync_repetition", "ready:0", "state:UNMATCHED", "timer+retry_wait@3000"}), f.taken());

    f.fsm().retry_wait_elapsed(false);
    EXPECT_EQ(trace({"timer-retry_wait", "error_routine", "state:MATCHING", "timer+link_detect@4000"}), f.taken())
        << "the re-arm does not reopen it";
}

TEST(LinkStateMachine, TheWindowIsNotOpenedWhenRepetitionIsDisabled) {
    auto config = default_config();
    config.sync_repetition_ms = 0;
    fixture f(config);

    f.fsm().enter_bcd(false);

    EXPECT_EQ(trace({"state:MATCHING", "timer+link_detect@4000"}), f.taken());
}

// V2G10-056: FAILED with the window still open, so the initialization restarts.
TEST(LinkStateMachine, CommunicationInitialisationIsRepeatedWhileTheWindowIsOpen) {
    auto config = default_config();
    config.link_detect_timeout_ms = 1000;
    fixture f(config);
    f.fsm().enter_bcd(false);
    (void)f.taken();

    f.fsm().link_detect_timeout(true);

    EXPECT_EQ(trace({"timer-link_detect", "timer+link_detect@1000"}), f.taken())
        << "still MATCHING, so no state publish; only the attempt restarts";
    EXPECT_EQ(internal_state::matching, f.fsm().state());
    EXPECT_EQ(1, f.fsm().retry_count()) << "a repetition costs an attempt so it cannot loop forever";
}

// V2G10-058: window closed, the initialization stops and the restart takes over.
TEST(LinkStateMachine, TheInitialisationStopsOnceTheWindowClosed) {
    auto config = default_config();
    config.link_detect_timeout_ms = 1000;
    fixture f(config);
    f.fsm().enter_bcd(false);
    (void)f.taken();
    f.fsm().link_detect_timeout(true);
    (void)f.taken();

    f.fsm().link_detect_timeout(false);

    EXPECT_EQ(trace({"timer-link_detect", "state:UNMATCHED", "timer+retry_wait@3000"}), f.taken());
    EXPECT_EQ(internal_state::retry_wait, f.fsm().state());
    EXPECT_EQ(2, f.fsm().retry_count());
}

TEST(LinkStateMachine, RepetitionIsAlsoBoundedByTheRetryBudget) {
    auto config = default_config();
    config.link_detect_timeout_ms = 1000;
    config.conn_retry_max = 1;
    fixture f(config);
    f.fsm().enter_bcd(false);
    (void)f.taken();

    f.fsm().link_detect_timeout(true);
    ASSERT_EQ(internal_state::matching, f.fsm().state());
    ASSERT_EQ(1, f.fsm().retry_count());
    (void)f.taken();

    f.fsm().link_detect_timeout(true);
    EXPECT_EQ(trace({"timer-link_detect", "state:UNMATCHED"}), f.taken());
    EXPECT_EQ(internal_state::unmatched, f.fsm().state());
}

TEST(LinkStateMachine, ARepeatedInitialisationStillMatchesWhenTheLinkArrives) {
    auto config = default_config();
    config.link_detect_timeout_ms = 1000;
    fixture f(config);
    f.fsm().enter_bcd(false);
    f.fsm().link_detect_timeout(true);
    (void)f.taken();

    f.fsm().carrier_up();

    EXPECT_EQ(trace({"timer-link_detect", "state:MATCHED", "ready:1"}), f.taken());
    EXPECT_EQ(internal_state::matched, f.fsm().state());
}

// --- resuming from paused ---------------------------------------------------------------------

// LAN8650 low-power mode is not implemented: the carrier never drops while paused, so there is no
// wake-up edge. A neighbour answering resumes instead and re-arms supervision (V2G10-036, -042).
TEST(LinkStateMachine, PausedResumesOnANeighbourAnswering) {
    fixture f;
    f.reach_matched();
    f.fsm().dlink_pause();
    (void)f.taken();
    ASSERT_EQ(internal_state::paused, f.fsm().state());

    f.fsm().neighbor_reachable("0A:1B:2C:D3:E4:F5");

    EXPECT_EQ(trace({"mac:0A:1B:2C:D3:E4:F5", "ready:1"}), f.taken())
        << "V2G10-042: the wake-up re-issues D-LINK_READY; the state never left MATCHED";
    EXPECT_EQ(internal_state::matched, f.fsm().state());
}

TEST(LinkStateMachine, APauseResumedByANeighbourIsSupervisedAgain) {
    fixture f;
    f.reach_matched();
    f.fsm().dlink_pause();
    f.fsm().neighbor_reachable("0A:1B:2C:D3:E4:F5");
    (void)f.taken();
    ASSERT_EQ(internal_state::matched, f.fsm().state());

    f.fsm().carrier_down();

    EXPECT_EQ(trace({"ready:0", "state:UNMATCHED", "state:MATCHING", "timer+link_detect@4000"}), f.taken());
}

TEST(LinkStateMachine, APausedLinkThatDroppedCarrierStillResumesOnANeighbour) {
    fixture f;
    f.reach_matched();
    f.fsm().dlink_pause();
    f.fsm().carrier_down();
    (void)f.taken();
    ASSERT_EQ(internal_state::paused, f.fsm().state());

    f.fsm().neighbor_reachable("0A:1B:2C:D3:E4:F5");

    EXPECT_EQ(internal_state::matched, f.fsm().state());
}

// --- published state mapping ------------------------------------------------------------------

TEST(LinkStateMachine, InternalStatesMapOntoTheThreePublishedOnes) {
    fixture f;
    EXPECT_EQ(link_state::unmatched, f.fsm().published_state());

    f.fsm().enter_bcd(false);
    EXPECT_EQ(link_state::matching, f.fsm().published_state());

    f.fsm().carrier_up();
    EXPECT_EQ(link_state::matched, f.fsm().published_state());

    f.fsm().dlink_pause();
    EXPECT_EQ(link_state::matched, f.fsm().published_state()) << "paused stays MATCHED";

    f.fsm().dlink_error();
    EXPECT_EQ(link_state::unmatched, f.fsm().published_state()) << "retry_wait is UNMATCHED";

    f.fsm().reset(false);
    EXPECT_EQ(link_state::unmatched, f.fsm().published_state());
}

} // namespace
