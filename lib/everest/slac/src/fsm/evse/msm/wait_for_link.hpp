// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

// WaitForLink sub-machine: after CM_SLAC_MATCH.CNF, poll the modem until it reports the AVLN as
// linked (or the link-status timeout elapses). Re-sends the cached CM_SLAC_MATCH.CNF on request.

#pragma once
#include "../../msm_helpers.hpp"
#include "common.hpp"
#include "guards_and_actions/wait_for_link_logic.hpp"

#include <everest/slac/fsm/evse/context.hpp>
#include <everest/slac/telemetry.hpp>
#include <everest/slac/timer.hpp>

#include <boost/mpl/vector.hpp>
#include <boost/msm/front/completion_event.hpp>
#include <boost/msm/front/functor_row.hpp>
#include <boost/msm/front/state_machine_def.hpp>
#include <boost/msm/front/states.hpp>

#include <chrono>

namespace everest::lib::slac::msm::wait_for_link_sm {

struct WaitForLink_def : public state_machine_def<WaitForLink_def> {
    // States
    // clang-format off
    struct Init          : public state<> { };
    struct NoDetect      : public state<> { };
    struct Failed        : public exit_pseudo_state<none> { };
    struct Matched       : public exit_pseudo_state<message> { };
    // clang-format on

    // Transitions
    // Rows sharing Init and the completion event must be mutually exclusive (msm_helpers.hpp); a
    // vendor without a link status query fails at once.
    using no_link_status = And_<Not_<is_lumissil>, Not_<is_qualcomm>>;

    using initial_state = Init;
    // clang-format off
    struct transition_table : boost::mpl::vector<
        //    +----------+---------+----------+-----------------+-----------------+
        //    | Source   | Event   | Target   | Action          | Guard           |
        //    +----------+---------+----------+-----------------+-----------------+
        Row   < Init     , none    , Failed   , none            , no_link_status  >,
        Row   < Init     , none    , Lumissil , link_status_req , is_lumissil     >,
        Row   < Init     , none    , Qualcomm , link_status_req , is_qualcomm     >,
        //    +----------+---------+----------+-----------------+-----------------+
        Row   < Lumissil , update  , none     , link_status_req , timeout         >,
        Row   < Lumissil , message , none     , send_match_cnf  , is_match_req    >,
        Row   < Lumissil , message , Matched  , none            , link_status_cnf >,
        //    +----------+---------+----------+-----------------+-----------------+
        Row   < Qualcomm , update  , none     , link_status_req , timeout         >,
        Row   < Qualcomm , message , none     , send_match_cnf  , is_match_req    >,
        Row   < Qualcomm , message , Matched  , none            , link_status_cnf >
        //    +----------+---------+----------+-----------------+-----------------+
        >{};
    // clang-format on

    // No try/catch around the event: the callbacks the machine calls never throw (the context parks
    // their failures, see rethrow_recorded), so a throw here is a bug and must not be swallowed.
    typedef int no_exception_thrown;
    template <class FSM, class Event> void no_transition(Event const&, FSM&, int) {
    }

    // Entry / exit
    template <class Event, class Fsm> void on_entry(Event const&, Fsm& fsm) {
        ctx = fsm.ctx;
        // Still in the matching phase (post CM_SLAC_MATCH.CNF, awaiting link) -> published as MATCHING.
        ctx->enter_state(SlacState::WaitForLink, D3State::Matching, "Waiting for Link to be ready...");
        link_check_to = ctx->slac_config.link_status.retry;
        to.arm(ctx->current_time, ctx->slac_config.link_status.timeout);
    }

    // Members
    fsm::evse::Context* ctx;
    std::chrono::milliseconds link_check_to{0};
    timer to;
    bool state_timeout(timer::tp now) const {
        return to.expired(now);
    }
    void deadlines(earliest_deadline& next) const {
        next.offer(to);
    }
};
static_assert(has_deadlines<WaitForLink_def>::value);

} // namespace everest::lib::slac::msm::wait_for_link_sm
