// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 - 2026 Pionix GmbH and Contributors to EVerest

// Matching sub-machine: listens for CM_SLAC_PARM.REQ, runs one Session per EV in parallel and
// handles CM_VALIDATE. Exits to Matched once a session completed, or to Failed.

#pragma once
#include "../validate_handler.hpp"
#include "common.hpp"
#include "matching_actions.hpp"
#include "session.hpp"

namespace everest::lib::slac::msm::matching_sm {

struct Matching_def : public state_machine_def<Matching_def> {
    // States
    // clang-format off
    struct Init    : public state<> { };
    struct Listen  : public state<> { };
    struct Pipe    : public state<> { };
    struct Matched : public exit_pseudo_state<update> { };
    struct Failed  : public exit_pseudo_state<update> { };
    // clang-format on

    // Transitions
    using Session = state_machine<session_sm::Session_def>;
    using initial_state = boost::mpl::vector<Init, Listen, Pipe>;
    using fail_matching = should_transition_to_failed_matching;
    using reset_matching = should_reset_instead_of_fail;
    using not_validate_req = Not_<is_validate_req>;
    using new_parm_req = And_<is_slac_param_req, Not_<has_matched_session>>;
    using late_parm_req = And_<is_slac_param_req, has_matched_session>;
    // clang-format off
    struct transition_table : boost::mpl::vector<
        //    +--------+---------+---------+-----------------------+------------------------+
        //    | Source | Event   | Target  | Action                | Guard                  |
        //    +--------+---------+---------+-----------------------+------------------------+
        Row   < Init   , update  , Matched , none                  , has_matched_session    >,
        Row   < Init   , update  , Failed  , none                  , fail_matching          >,
        Row   < Init   , update  , Init    , reset_matching_subfsm , reset_matching         >,
        //    +--------+---------+---------+-----------------------+------------------------+
        Row   < Listen , message , Listen  , add_session           , new_parm_req           >,
        Row   < Listen , message , Listen  , ignore_parm_req       , late_parm_req          >,
        Row   < Listen , message , Listen  , handle_validate_req   , is_validate_req        >,
        Row   < Listen , update  , Listen  , validate_tick         , validate_needs_service >,
        //    +--------+---------+---------+-----------------------+------------------------+
        Row   < Pipe   , message , none    , pipe_event            , not_validate_req       >,
        Row   < Pipe   , update  , none    , pipe_event            , none                   >
        //    +--------+---------+---------+-----------------------+------------------------+
        >{};
    // clang-format on

    template <class FSM, class Event> void no_transition(Event const&, FSM&, int) {
    }

    // Entry / exit
    template <class Event, class Fsm> void on_entry(Event const&, Fsm& fsm) {
        ctx = fsm.ctx;
        to.setDuration(std::chrono::milliseconds(ctx->slac_config.slac_init_timeout_ms));
        to.reset();
        failed_matching_reset_once = false;
        validate.reset();
        ctx->validation_done = false;
        ctx->enter_state(SlacState::Matching, D3State::Matching,
                         "Entered Matching state, waiting for CM_SLAC_PARM.REQ");
    }

    template <class Event, class Fsm> void on_exit(Event const&, Fsm&) {
        sessions.clear();
        ctx->status.session_count = 0;
    }

    // Members
    std::vector<Session> sessions;
    fsm::evse::Context* ctx;
    timer to;
    bool failed_matching_reset_once{false};
    fsm::evse::ValidateHandler validate; // CM_VALIDATE (ISO 15118-3 9.4) BCB-toggle validation

    static int clamp_max_matching_sessions(int max_matching_sessions) {
        return std::max(1, max_matching_sessions);
    }
    int max_matching_sessions() const {
        return clamp_max_matching_sessions(ctx->slac_config.max_matching_sessions);
    }

    bool state_timeout() {
        return to.timeout();
    }
};

} // namespace everest::lib::slac::msm::matching_sm
