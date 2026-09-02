// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 - 2026 Pionix GmbH and Contributors to EVerest

// Matching sub-machine: listens for CM_SLAC_PARM.REQ, runs one Session per EV in parallel and
// handles CM_VALIDATE. Exits to Matched once a session completed, or to Failed.

#pragma once
#include "common.hpp"
#include "matching_actions.hpp"
#include "session.hpp"

namespace everest::lib::slac::msm::matching_sm {

struct Matching_def    : public state_machine_def<Matching_def> {
    // States
    // clang-format off
    struct Init    : public state<> { };
    struct Listen  : public state<> { };
    struct Pipe    : public state<> { };
    struct Matched : public exit_pseudo_state<update> { };
    struct Failed  : public exit_pseudo_state<update> { };
    // clang-format on

    // Member guards (used via g_row / from the functors in matching_actions.hpp)
    bool is_matched(update const&) {
        for(auto& elem : sessions){
            if(elem.is_flag_active<SessionMatched>()){
                return true;
            }
        }
        return false;
    }
    bool is_failed(update const&) {
        if(sessions.empty()){
            return false;
        }
        for(auto& elem : sessions){
            if(not elem.is_flag_active<SessionFailed>()){
                return false;
            }
        }
        return true;
    }

    // Transitions
    using Session = state_machine<session_sm::Session_def>;
    using initial_state = boost::mpl::vector<Init, Listen, Pipe>;
    using p = Matching_def;
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
        g_row < Init   , update  , Matched /* none */              , &p::is_matched         >,
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

    template <class FSM,class Event>
    void no_transition(Event const&, FSM&, int) {
    }

    // Entry / exit
    template <class Event, class Fsm>
    void on_entry(Event const&, Fsm& fsm) {
        ctx = fsm.ctx;
        to.setDuration(std::chrono::milliseconds(ctx->slac_config.slac_init_timeout_ms));
        to.reset();
        failed_matching_reset_once = false;
        validate_armed = false;
        validate_step2_pending = false;
        validate_step1_retries = 0;
        validate_owner_mac = MacAddress{};
        ctx->validation_done = false;
        ctx->log_info("Entered Matching state, waiting for CM_SLAC_PARM.REQ");
        ctx->status.match_state = SlacState::Matching;
        ctx->status.d3_state = D3State::Matching;
    }

    template <class Event, class Fsm>
    void on_exit(Event const&, Fsm&) {
        sessions.clear();
        ctx->status.session_count = 0;
    }

    // Members
    void send_validate_cnf_reply(MacAddress const& mac, std::uint8_t result, std::uint8_t toggle_num) {
        messages::cm_validate_cnf reply{};
        reply.signal_type = defs::CM_VALIDATE_REQ_SIGNAL_TYPE;
        reply.toggle_num = toggle_num;
        reply.result = result;
        if (not ctx->send_slac_message(mac, reply)) {
            ctx->log_warn("Failed to send CM_VALIDATE.CNF");
        }
    }

    std::vector<Session> sessions;
    fsm::evse::Context* ctx;
    timer to;
    bool failed_matching_reset_once{false};
    // CM_VALIDATE (ISO 15118-3 9.4) BCB-toggle validation state (see handle_validate_req / validate_tick).
    bool validate_armed{false};         // step-1 seen; awaiting step-2 or repeating the step-1 CNF
    bool validate_step2_pending{false}; // step-2 seen; waiting out the toggle-observation window
    int validate_step1_retries{0};      // autonomous step-1 CNF repetitions so far (<= C_EV_match_retry)
    int validate_baseline_bc{0};        // bc_transition_count at the start of the toggle window
    MacAddress validate_owner_mac{};    // EV that owns the in-progress validation
    timer validate_timer;               // step-1 repetition interval / step-2 toggle-observation window

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
