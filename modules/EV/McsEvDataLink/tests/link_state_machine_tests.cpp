// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
//
// Tests for the EV-side data link state machine (main/link_state_machine.cpp) through its effect
// seam. Actions in the machine never do I/O; they append effects, so a test can assert the exact
// sequence of publishes and timer operations a transition produces - which is the whole observable
// behaviour of the module minus the netlink socket.
//
// Effects are compared as strings (see describe()): an ordered list of short tokens reads like the
// trace one would look for in a log, and a wrong order fails as clearly as a wrong content.

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
    case effect::kind::publish_connector_mac:
        return "mac:" + item.mac;
    case effect::kind::start_timer:
        return "timer+" + std::to_string(item.timeout_ms);
    case effect::kind::stop_timer:
        return "timer-";
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
    config.link_detect_timeout_ms = 4000;
    config.publish_connector_mac = true;
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
        m_fsm.trigger_matching(false);
        m_fsm.carrier_up();
        (void)m_fsm.take_effects();
        EXPECT_EQ(link_state::matched, m_fsm.state());
    }

private:
    link_state_machine m_fsm;
    trace m_start_trace;
};

// --- start up and the plain success paths -----------------------------------------------------

TEST(EvLinkStateMachine, StartPublishesUnmatchedOnce) {
    fixture f;
    EXPECT_EQ(trace({"state:UNMATCHED"}), f.start_trace());
    EXPECT_EQ(link_state::unmatched, f.fsm().state());
    EXPECT_FALSE(f.fsm().dlink_ready());
}

TEST(EvLinkStateMachine, TriggerMatchingWithoutCarrierStartsTheSetupDeadline) {
    fixture f;
    f.fsm().trigger_matching(false);

    EXPECT_EQ(trace({"state:MATCHING", "timer+4000"}), f.taken());
    EXPECT_EQ(link_state::matching, f.fsm().state());
    EXPECT_FALSE(f.fsm().dlink_ready());
}

TEST(EvLinkStateMachine, CarrierUpWhileMatchingMatchesAndCancelsTheDeadline) {
    fixture f;
    f.fsm().trigger_matching(false);
    (void)f.taken();

    f.fsm().carrier_up();

    EXPECT_EQ(trace({"timer-", "state:MATCHED", "ready:1"}), f.taken());
    EXPECT_EQ(link_state::matched, f.fsm().state());
    EXPECT_TRUE(f.fsm().dlink_ready());
}

// V2G10-028 has the EV establish the link right after plug-in detection, before state B, and SPE is
// point to point - so by the time the EV stack asks for matching the PHY is usually already
// operational. V2G10-030 needs the link *and* the basic-signalling condition in either order, so
// this must match at once instead of waiting for an edge that already happened.
TEST(EvLinkStateMachine, TriggerMatchingWithCarrierAlreadyUpMatchesImmediately) {
    fixture f;
    f.fsm().trigger_matching(true);

    EXPECT_EQ(trace({"state:MATCHED", "ready:1"}), f.taken());
    EXPECT_EQ(link_state::matched, f.fsm().state());
    EXPECT_TRUE(f.fsm().dlink_ready());
}

TEST(EvLinkStateMachine, TheDeadlineExpiringFailsCommunicationInitialisation) {
    fixture f;
    f.fsm().trigger_matching(false);
    (void)f.taken();

    f.fsm().link_detect_timeout();

    // V2G10-054: back to UNMATCHED. No dlink_ready(false) - it was never true.
    EXPECT_EQ(trace({"timer-", "state:UNMATCHED"}), f.taken());
    EXPECT_EQ(link_state::unmatched, f.fsm().state());
}

// The EV-side counterpart of the EVSE's retry loop: there is none. V2G10-039 has the EV wait for
// the EVSE's restart indication, which reaches the module as another trigger_matching.
TEST(EvLinkStateMachine, AFailedInitialisationJustWaitsToBeTriggeredAgain) {
    fixture f;
    f.fsm().trigger_matching(false);
    f.fsm().link_detect_timeout();
    (void)f.taken();
    ASSERT_EQ(link_state::unmatched, f.fsm().state());

    // Nothing happens on its own: no retry timer, no repetition, no state change.
    EXPECT_TRUE(f.taken().empty());
    EXPECT_EQ(link_state::unmatched, f.fsm().state());

    // The EVSE's restart indication arrives as a fresh trigger from the EV stack.
    f.fsm().trigger_matching(true);
    EXPECT_EQ(trace({"state:MATCHED", "ready:1"}), f.taken());
}

// --- losing the link -------------------------------------------------------------------------

// V2G10-036, and no retry: the EV reports the link down and waits. The EVSE is the side that
// relaunches (7.5.3), so re-entering MATCHING here would be the EV second-guessing it.
TEST(EvLinkStateMachine, CarrierLossWhileMatchedReportsTheLinkDownAndStops) {
    fixture f;
    f.reach_matched();

    f.fsm().carrier_down();

    EXPECT_EQ(trace({"ready:0", "state:UNMATCHED"}), f.taken());
    EXPECT_EQ(link_state::unmatched, f.fsm().state());
    EXPECT_FALSE(f.fsm().dlink_ready());
}

TEST(EvLinkStateMachine, ALivenessLossIsHandledLikeACarrierLoss) {
    fixture f;
    f.reach_matched();

    f.fsm().link_lost();

    EXPECT_EQ(trace({"ready:0", "state:UNMATCHED"}), f.taken());
    EXPECT_EQ(link_state::unmatched, f.fsm().state());
}

TEST(EvLinkStateMachine, ALostLinkCanBeReestablishedByANewTrigger) {
    fixture f;
    f.reach_matched();
    f.fsm().carrier_down();
    (void)f.taken();

    f.fsm().trigger_matching(false);
    EXPECT_EQ(trace({"state:MATCHING", "timer+4000"}), f.taken());
    f.fsm().carrier_up();
    EXPECT_EQ(trace({"timer-", "state:MATCHED", "ready:1"}), f.taken());
}

// --- reset ------------------------------------------------------------------------------------

// EvManager calls reset() immediately followed by trigger_matching() every time it sees UNMATCHED,
// so reset must be a plain teardown that leaves the module ready - never a latch.
TEST(EvLinkStateMachine, ResetFromUnmatchedIsIdempotentAndLeavesTheModuleReady) {
    fixture f;

    f.fsm().reset();
    EXPECT_TRUE(f.taken().empty()) << "already published UNMATCHED, nothing changed";
    EXPECT_EQ(link_state::unmatched, f.fsm().state());

    f.fsm().trigger_matching(true);
    EXPECT_EQ(trace({"state:MATCHED", "ready:1"}), f.taken());
}

TEST(EvLinkStateMachine, ResetWhileMatchedTearsTheLinkDown) {
    fixture f;
    f.reach_matched();

    f.fsm().reset();

    EXPECT_EQ(trace({"ready:0", "state:UNMATCHED"}), f.taken());
    EXPECT_EQ(link_state::unmatched, f.fsm().state());
    EXPECT_FALSE(f.fsm().dlink_ready());
}

TEST(EvLinkStateMachine, ResetWhileMatchingCancelsTheDeadline) {
    fixture f;
    f.fsm().trigger_matching(false);
    (void)f.taken();

    f.fsm().reset();

    EXPECT_EQ(trace({"timer-", "state:UNMATCHED"}), f.taken());
    EXPECT_EQ(link_state::unmatched, f.fsm().state());
}

// The consumer's actual call pattern, start to finish.
TEST(EvLinkStateMachine, TheEvManagerCallPatternWorks) {
    fixture f;

    // iso_wait_slac_matched: sees UNMATCHED, calls reset() then trigger_matching().
    f.fsm().reset();
    f.fsm().trigger_matching(false);
    EXPECT_EQ(trace({"state:MATCHING", "timer+4000"}), f.taken());

    // The link comes up and the consumer polls MATCHED.
    f.fsm().carrier_up();
    EXPECT_EQ(trace({"timer-", "state:MATCHED", "ready:1"}), f.taken());
    EXPECT_EQ(link_state::matched, f.fsm().state());

    // Unplug reaches this module only through reset - see the interface gap in docs/index.rst.
    f.fsm().reset();
    EXPECT_EQ(trace({"ready:0", "state:UNMATCHED"}), f.taken());
}

// --- ev_mac_address --------------------------------------------------------------------------

TEST(EvLinkStateMachine, ReachableNeighbourPublishesTheConnectorMacOncePerLink) {
    fixture f;
    f.reach_matched();

    f.fsm().neighbor_reachable("0A:1B:2C:D3:E4:F5");
    EXPECT_EQ(trace({"mac:0A:1B:2C:D3:E4:F5"}), f.taken());

    f.fsm().neighbor_reachable("0A:1B:2C:D3:E4:F5");
    EXPECT_TRUE(f.taken().empty()) << "the same address is not republished";

    f.fsm().neighbor_reachable("0A:1B:2C:D3:E4:F6");
    EXPECT_EQ(trace({"mac:0A:1B:2C:D3:E4:F6"}), f.taken());
}

TEST(EvLinkStateMachine, TheConnectorMacIsNotPublishedOutsideMatched) {
    fixture f;
    auto const ignored_before = f.fsm().ignored_events();

    f.fsm().neighbor_reachable("0A:1B:2C:D3:E4:F5");
    EXPECT_TRUE(f.taken().empty());

    f.fsm().trigger_matching(false);
    (void)f.taken();
    f.fsm().neighbor_reachable("0A:1B:2C:D3:E4:F5");
    EXPECT_TRUE(f.taken().empty());

    EXPECT_EQ(ignored_before + 2, f.fsm().ignored_events());
}

TEST(EvLinkStateMachine, TheConnectorMacIsForgottenWhenTheLinkGoesDown) {
    fixture f;
    f.reach_matched();
    f.fsm().neighbor_reachable("0A:1B:2C:D3:E4:F5");
    (void)f.taken();

    f.fsm().reset();
    f.fsm().trigger_matching(true);
    (void)f.taken();

    f.fsm().neighbor_reachable("0A:1B:2C:D3:E4:F5");
    EXPECT_EQ(trace({"mac:0A:1B:2C:D3:E4:F5"}), f.taken()) << "a new link republishes it";
}

TEST(EvLinkStateMachine, ConnectorMacPublishingCanBeDisabled) {
    auto config = default_config();
    config.publish_connector_mac = false;
    fixture f(config);
    f.reach_matched();

    f.fsm().neighbor_reachable("0A:1B:2C:D3:E4:F5");

    EXPECT_TRUE(f.taken().empty());
    EXPECT_EQ(link_state::matched, f.fsm().state()) << "still an internal transition, not an ignored event";
}

TEST(EvLinkStateMachine, AnEmptyMacIsNotPublished) {
    fixture f;
    f.reach_matched();

    f.fsm().neighbor_reachable("");

    EXPECT_TRUE(f.taken().empty());
}

// --- events that must be ignored --------------------------------------------------------------

TEST(EvLinkStateMachine, CarrierUpWithoutATriggerDoesNotMatch) {
    fixture f;
    auto const ignored_before = f.fsm().ignored_events();

    f.fsm().carrier_up();

    EXPECT_TRUE(f.taken().empty()) << "V2G10-030 needs the basic-signalling condition as well";
    EXPECT_EQ(link_state::unmatched, f.fsm().state());
    EXPECT_EQ(ignored_before + 1, f.fsm().ignored_events());
}

TEST(EvLinkStateMachine, ARepeatedTriggerWhileAlreadyBusyIsIgnored) {
    fixture f;
    f.fsm().trigger_matching(false);
    (void)f.taken();
    auto const ignored_before = f.fsm().ignored_events();

    f.fsm().trigger_matching(true);
    EXPECT_TRUE(f.taken().empty()) << "the deadline must not be restarted";
    EXPECT_EQ(link_state::matching, f.fsm().state());

    f.fsm().carrier_up();
    (void)f.taken();
    f.fsm().trigger_matching(true);
    EXPECT_TRUE(f.taken().empty());
    EXPECT_EQ(link_state::matched, f.fsm().state());

    EXPECT_EQ(ignored_before + 2, f.fsm().ignored_events());
}

TEST(EvLinkStateMachine, RedundantCarrierEdgesAndStrayTimeoutsAreIgnored) {
    fixture f;
    f.reach_matched();
    auto const ignored_before = f.fsm().ignored_events();

    f.fsm().carrier_up();
    f.fsm().link_detect_timeout();

    EXPECT_TRUE(f.taken().empty());
    EXPECT_EQ(link_state::matched, f.fsm().state());
    EXPECT_EQ(ignored_before + 2, f.fsm().ignored_events());
}

TEST(EvLinkStateMachine, ACarrierLossWhileUnmatchedIsIgnored) {
    fixture f;
    auto const ignored_before = f.fsm().ignored_events();

    f.fsm().carrier_down();
    f.fsm().link_lost();

    EXPECT_TRUE(f.taken().empty());
    EXPECT_EQ(ignored_before + 2, f.fsm().ignored_events());
}

// A carrier drop while the deadline runs is not a failure of its own: the deadline is what decides.
TEST(EvLinkStateMachine, ACarrierLossWhileMatchingDoesNotShortcutTheDeadline) {
    fixture f;
    f.fsm().trigger_matching(false);
    (void)f.taken();
    auto const ignored_before = f.fsm().ignored_events();

    f.fsm().carrier_down();

    EXPECT_TRUE(f.taken().empty());
    EXPECT_EQ(link_state::matching, f.fsm().state());
    EXPECT_EQ(ignored_before + 1, f.fsm().ignored_events());
}

// --- the EV sleep case, which this interface cannot name ---------------------------------------

// `ev_slac` has no pause command, so a comm module switched off for an EV sleep (V2G10-040) is
// indistinguishable from a link that failed. The machine reports it as what it observes - the link
// is down - which is truthful in both cases, and the EV stack that initiated the sleep knows which
// one it is. Contrast the EVSE side, where dlink_pause exists and D-LINK_READY has to survive the
// pause (V2G10-041).
TEST(EvLinkStateMachine, ASleepingCommModuleLooksLikeALinkLossAndIsReportedAsOne) {
    fixture f;
    f.reach_matched();

    f.fsm().carrier_down();
    EXPECT_EQ(trace({"ready:0", "state:UNMATCHED"}), f.taken());

    // Waking up (V2G10-043: comm setup within T_conn_resume of the wake trigger) is a fresh
    // trigger from the stack, and the deadline enforces the bound.
    f.fsm().trigger_matching(false);
    EXPECT_EQ(trace({"state:MATCHING", "timer+4000"}), f.taken());
    f.fsm().carrier_up();
    EXPECT_EQ(trace({"timer-", "state:MATCHED", "ready:1"}), f.taken());
}

} // namespace
