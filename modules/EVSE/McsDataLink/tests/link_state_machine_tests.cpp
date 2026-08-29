// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
//
// Tests for the MCS data link state machine (main/link_state_machine.cpp) through its effect
// seam. Actions in the machine never do I/O; they append effects, so a test can assert the exact
// sequence of publishes and timer operations a transition produces - which is the whole
// observable behaviour of the module minus the netlink socket.
//
// Effects are compared as strings (see describe()): an ordered list of short tokens reads like
// the trace one would look for in a log, and a wrong order fails as clearly as a wrong content.

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
    // The standard's maxima make the repetition window close exactly when the first
    // TT_EV_link_detect expires, so the default config never repeats. Cases that exercise
    // repetition shorten link_detect_timeout_ms, as an integrator would have to.
    config.sync_repetition_ms = 4000;
    config.retry_wait_ms = 3000;
    config.publish_ev_mac = true;
    return config;
}

/// The machine plus the started-and-drained bookkeeping every case needs.
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

    /// Drive the machine to MATCHED via the plain path and drop the effects.
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

// SPE is point to point: on a PLCA link the PHY can be operational before the EV is detected by
// basic signalling. V2G10-023 needs state B *and* link up, in either order, so enter_bcd with the
// carrier already up must match straight away instead of waiting for an edge that never comes.
TEST(LinkStateMachine, EnterBcdWithCarrierAlreadyUpMatchesImmediately) {
    fixture f;
    f.fsm().enter_bcd(true);

    EXPECT_EQ(trace({"state:MATCHED", "ready:1"}), f.taken());
    EXPECT_EQ(internal_state::matched, f.fsm().state());
    EXPECT_TRUE(f.fsm().dlink_ready());
}

TEST(LinkStateMachine, LinkDetectTimeoutFailsCommunicationInitialisation) {
    fixture f;
    f.fsm().enter_bcd(false);
    (void)f.taken();

    f.fsm().link_detect_timeout(false);

    // V2G10-054: back to UNMATCHED. No dlink_ready(false) - it was never true.
    EXPECT_EQ(trace({"timer-link_detect", "state:UNMATCHED"}), f.taken());
    EXPECT_EQ(internal_state::unmatched, f.fsm().state());
    EXPECT_EQ(0, f.fsm().retry_count()) << "a failed first attempt does not spend the retry budget";
}

TEST(LinkStateMachine, LeaveBcdTearsDownTheLink) {
    fixture f;
    f.reach_matched();

    f.fsm().leave_bcd();

    EXPECT_EQ(trace({"ready:0", "state:UNMATCHED"}), f.taken());
    EXPECT_EQ(internal_state::unmatched, f.fsm().state());
    EXPECT_FALSE(f.fsm().dlink_ready());
}

TEST(LinkStateMachine, LeaveBcdWhileMatchingCancelsTheTimerAndDoesNotWithdrawWhatWasNeverIssued) {
    fixture f;
    f.fsm().enter_bcd(false);
    (void)f.taken();

    f.fsm().leave_bcd();

    EXPECT_EQ(trace({"timer-link_detect", "state:UNMATCHED"}), f.taken());
}

// --- reset ------------------------------------------------------------------------------------

// reset(false) is the session-end teardown EvseManager sends, and it is the only reset it ever
// sends - the matching reset(true) call is commented out there. It must therefore leave the module
// ready for the next session; latching matching off would serve exactly one EV after startup.
TEST(LinkStateMachine, ResetDisableTearsDownButLeavesTheModuleReady) {
    fixture f;
    f.reach_matched();

    f.fsm().reset(false);
    EXPECT_EQ(trace({"ready:0", "state:UNMATCHED"}), f.taken());
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

    EXPECT_EQ(trace({"ready:0", "state:UNMATCHED"}), f.taken());
    EXPECT_EQ(internal_state::unmatched, f.fsm().state());
}

// --- carrier loss while matched ---------------------------------------------------------------

// V2G10-036: report D-LINK_READY(no link) upward. UNMATCHED is published explicitly before the
// machine goes back to MATCHING so a consumer sees the link really went down, even though the
// retry starts in the same event-loop iteration.
TEST(LinkStateMachine, CarrierLossWhileMatchedReportsDownAndRestartsMatching) {
    fixture f;
    f.reach_matched();

    f.fsm().carrier_down();

    EXPECT_EQ(trace({"ready:0", "state:UNMATCHED", "state:MATCHING", "timer+link_detect@4000"}), f.taken());
    EXPECT_EQ(internal_state::matching, f.fsm().state());
    EXPECT_FALSE(f.fsm().dlink_ready());
    EXPECT_EQ(1, f.fsm().retry_count());
}

// After a loss the machine waits for a fresh carrier-up edge; it does not re-derive "the carrier
// is still up" from anywhere. That is what keeps a liveness-detected loss (where the carrier
// genuinely never dropped) from re-matching instantly and looping.
TEST(LinkStateMachine, RestartedMatchingWaitsForAFreshCarrierEdge) {
    fixture f;
    f.reach_matched();
    f.fsm().link_lost();
    (void)f.taken();
    ASSERT_EQ(internal_state::matching, f.fsm().state());

    // Nothing happens on its own ...
    EXPECT_TRUE(f.taken().empty());
    EXPECT_EQ(internal_state::matching, f.fsm().state());

    // ... until either the link really comes back ...
    f.fsm().carrier_up();
    EXPECT_EQ(trace({"timer-link_detect", "state:MATCHED", "ready:1"}), f.taken());
}

TEST(LinkStateMachine, LivenessLossThatDoesNotRecoverEndsInCommunicationInitialisationFailed) {
    fixture f;
    f.reach_matched();
    f.fsm().link_lost();
    (void)f.taken();

    f.fsm().link_detect_timeout(false);

    EXPECT_EQ(trace({"timer-link_detect", "state:UNMATCHED"}), f.taken());
    EXPECT_EQ(internal_state::unmatched, f.fsm().state());
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

// C_conn_retry is a per-connection budget. A successful match in between deliberately does not
// refund attempts, otherwise a flapping link would retry forever and conn_retry_max would bound
// nothing at all.
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
    EXPECT_EQ(trace({"ready:0", "state:UNMATCHED", "timer+retry_wait@3000"}), f.taken());
    EXPECT_EQ(internal_state::retry_wait, f.fsm().state());
    EXPECT_EQ(link_state::unmatched, f.fsm().published_state());
    EXPECT_EQ(1, f.fsm().retry_count());

    // The 23-3 restart method is the error routine: EvseManager's error sequence produces the
    // B0-to-B toggle FOR THE EV. The module re-arms matching itself - on MCS the synthesized CP
    // state never leaves B while mated, so no fresh enter_bcd can ever arrive (bench-found).
    // With the carrier still up, both V2G10-023 conditions hold again immediately.
    f.fsm().retry_wait_elapsed(true);
    EXPECT_EQ(trace({"timer-retry_wait", "error_routine", "state:MATCHED", "ready:1"}), f.taken());
    EXPECT_EQ(internal_state::matched, f.fsm().state());
}

TEST(LinkStateMachine, DlinkErrorRestartWithoutCarrierWaitsForTheLinkInMatching) {
    fixture f;
    f.reach_matched();

    f.fsm().dlink_error();
    (void)f.taken();
    f.fsm().carrier_down(); // deliberately no row in restart_wait: the guard keeps running
    EXPECT_TRUE(f.taken().empty());

    // Without carrier the restart lands in MATCHING: the EV gets TT_EV_link_detect to bring the
    // link back after the B0-to-B toggle (the T_conn_resume analog). Deliberately no
    // sync_repetition window: this is a C_conn_retry reconnect, not a new comm-init.
    f.fsm().retry_wait_elapsed(false);
    EXPECT_EQ(trace({"timer-retry_wait", "error_routine", "state:MATCHING", "timer+link_detect@4000"}), f.taken());
    EXPECT_EQ(internal_state::matching, f.fsm().state());

    f.fsm().carrier_up();
    EXPECT_EQ(trace({"timer-link_detect", "state:MATCHED", "ready:1"}), f.taken());
}

TEST(LinkStateMachine, DlinkErrorWithoutRetryBudgetNeverRequestsARestart) {
    auto config = default_config();
    config.conn_retry_max = 0;
    fixture f(config);
    f.reach_matched();

    f.fsm().dlink_error();

    EXPECT_EQ(trace({"ready:0", "state:UNMATCHED"}), f.taken());
    EXPECT_EQ(internal_state::unmatched, f.fsm().state());
}

TEST(LinkStateMachine, DlinkErrorIsAcceptedFromEveryLiveState) {
    // matching
    {
        fixture f;
        f.fsm().enter_bcd(false);
        (void)f.taken();
        f.fsm().dlink_error();
        EXPECT_EQ(trace({"timer-link_detect", "state:UNMATCHED", "timer+retry_wait@3000"}), f.taken());
        EXPECT_EQ(internal_state::retry_wait, f.fsm().state());
    }
    // paused
    {
        fixture f;
        f.reach_matched();
        f.fsm().dlink_pause();
        (void)f.taken();
        f.fsm().dlink_error();
        EXPECT_EQ(trace({"ready:0", "state:UNMATCHED", "timer+retry_wait@3000"}), f.taken());
        EXPECT_EQ(internal_state::retry_wait, f.fsm().state());
    }
    // unmatched
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

// The wait is a mandatory >= 3 s guard with S S3 open; a bare carrier edge is not enough to
// shortcut it, the link has to be re-established through the B0-B restart.
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

// The outer machine handles dlink_error for the whole session, so the restart wait needs a row of
// its own to swallow a repeated one: a second D-LINK_ERROR while the guard is already running must
// not restart the wait or spend another attempt.
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
    // The row that swallows it is what keeps the outer machine's dlink_error rows from firing, and
    // taking a transition is also why this is not counted as ignored. Asserting the counter here
    // makes a future removal of that row fail rather than silently restart the guard.
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

// V2G10-041: D-LINK_PAUSE keeps the link logically up. Nothing is published, the state variable
// stays MATCHED and dlink_ready stays outstanding.
TEST(LinkStateMachine, DlinkPauseKeepsEverythingPublishedAsItWas) {
    fixture f;
    f.reach_matched();

    f.fsm().dlink_pause();

    EXPECT_TRUE(f.taken().empty());
    EXPECT_EQ(internal_state::paused, f.fsm().state());
    EXPECT_EQ(link_state::matched, f.fsm().published_state());
    EXPECT_TRUE(f.fsm().dlink_ready());
}

// The EVSE goes to B0 and the PHY may power down, so losing the carrier while paused is the
// expected course of events - it must not look like a failure.
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

// V2G10-042: the wake-up re-issues D-LINK_READY. The state variable never left MATCHED, so only
// dlink_ready is published again - it has to be, even though its value did not change.
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
        EXPECT_EQ(trace({"ready:0", "state:UNMATCHED"}), f.taken());
        EXPECT_EQ(internal_state::unmatched, f.fsm().state());
    }
    {
        fixture f;
        f.reach_matched();
        f.fsm().dlink_pause();
        (void)f.taken();
        f.fsm().reset(false);
        EXPECT_EQ(trace({"ready:0", "state:UNMATCHED"}), f.taken());
        EXPECT_EQ(internal_state::unmatched, f.fsm().state());
    }
    {
        fixture f;
        f.reach_matched();
        f.fsm().dlink_pause();
        (void)f.taken();
        f.fsm().dlink_terminate();
        EXPECT_EQ(trace({"ready:0", "state:UNMATCHED"}), f.taken());
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
    // A neighbour answering also ends the pause (see PausedResumesOnANeighbourAnswering), hence
    // the D-LINK_READY re-issue alongside the MAC.
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

// The window opens when communication initialization is triggered, i.e. on the enter_bcd that
// starts MATCHING - not on a re-match after a link loss, which is a C_conn_retry reconnect.
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

TEST(LinkStateMachine, TheWindowIsNotOpenedWhenRepetitionIsDisabled) {
    auto config = default_config();
    config.sync_repetition_ms = 0;
    fixture f(config);

    f.fsm().enter_bcd(false);

    EXPECT_EQ(trace({"state:MATCHING", "timer+link_detect@4000"}), f.taken());
}

// V2G10-056: FAILED, but the window is still open and the EV is still there, so restart.
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

// V2G10-058: once the window is gone the initialization stops.
TEST(LinkStateMachine, TheInitialisationStopsOnceTheWindowClosed) {
    auto config = default_config();
    config.link_detect_timeout_ms = 1000;
    fixture f(config);
    f.fsm().enter_bcd(false);
    (void)f.taken();
    f.fsm().link_detect_timeout(true);
    (void)f.taken();

    f.fsm().link_detect_timeout(false);

    EXPECT_EQ(trace({"timer-link_detect", "state:UNMATCHED"}), f.taken());
    EXPECT_EQ(internal_state::unmatched, f.fsm().state());
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

    // Window still open, but the budget is gone.
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

// The realistic pause: the LAN8650 low-power mode is not implemented, so the carrier never drops
// and no wake-up edge exists. Staying paused would leave carrier and liveness supervision disarmed
// for the whole resumed session, so a link loss would never produce dlink_ready(false)
// (V2G10-036) and the V2G10-042 re-issue would never happen. A neighbour answering is the evidence
// that the session came back.
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

    // The point of resuming: this loss is reported instead of being swallowed as an expected
    // pause-time carrier drop.
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
