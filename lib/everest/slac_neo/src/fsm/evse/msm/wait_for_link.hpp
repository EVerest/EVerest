// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 - 2026 Pionix GmbH and Contributors to EVerest

// WaitForLink sub-machine: after CM_SLAC_MATCH.CNF, poll the modem until it reports the AVLN as
// linked (or the link-status timeout elapses). Re-sends the cached CM_SLAC_MATCH.CNF on request.

#pragma once
#include "common.hpp"
#include "wait_for_link_actions.hpp"

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
    using initial_state = Init;
    // clang-format off
    struct transition_table : boost::mpl::vector<
        //    +----------+---------+----------+-----------------+-----------------+
        //    | Source   | Event   | Target   | Action          | Guard           |
        //    +----------+---------+----------+-----------------+-----------------+
        Row   < Init     , none    , Failed   , none            , none            >,
        Row   < Init     , none    , Lumissil , link_status_req , is_lumissil     >,
        Row   < Init     , none    , Qualcomm , link_status_req , is_qualcomm     >,
        //    +----------+---------+----------+-----------------+-----------------+
        Row   < Lumissil , update  , Lumissil , link_status_req , timeout         >,
        Row   < Lumissil , message , Lumissil , send_match_cnf  , is_match_req    >,
        Row   < Lumissil , message , Matched  , none            , link_status_cnf >,
        //    +----------+---------+----------+-----------------+-----------------+
        Row   < Qualcomm , update  , Qualcomm , link_status_req , timeout         >,
        Row   < Qualcomm , message , Qualcomm , send_match_cnf  , is_match_req    >,
        Row   < Qualcomm , message , Matched  , none            , link_status_cnf >
        //    +----------+---------+----------+-----------------+-----------------+
        >{};
    // clang-format on

    template <class FSM, class Event> void no_transition(Event const&, FSM&, int) {
    }

    // Entry / exit
    template <class Event, class Fsm> void on_entry(Event const&, Fsm& fsm) {
        ctx = fsm.ctx;
        // Still in the matching phase (post CM_SLAC_MATCH.CNF, awaiting link) -> published as MATCHING.
        ctx->enter_state(SlacState::WaitForLink, D3State::Matching, "Waiting for Link to be ready...");
        link_check_to_ms = ctx->slac_config.link_status.retry_ms;
        to.setDuration(std::chrono::milliseconds(ctx->slac_config.link_status.timeout_ms));
        to.reset();
    }

    // Members
    fsm::evse::Context* ctx;
    int link_check_to_ms{0};
    timer to;
    bool state_timeout() {
        return to.timeout();
    }
};

} // namespace everest::lib::slac::msm::wait_for_link_sm
