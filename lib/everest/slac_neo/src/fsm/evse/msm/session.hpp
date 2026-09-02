// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 - 2026 Pionix GmbH and Contributors to EVerest

// Session sub-machine: one instance per CM_SLAC_PARM.REQ, driven by Matching (see matching.hpp).
// Runs the sounding exchange for a single EV from CM_START_ATTEN_CHAR.IND to CM_SLAC_MATCH.CNF.

#pragma once
#include "common.hpp"
#include "session_actions.hpp"

namespace everest::lib::slac::msm::session_sm {

struct Session_def     : public state_machine_def<Session_def> {
    // States
    // clang-format off
    static constexpr auto FINALIZE_SOUNDING_DELAY_MS = 45;
    struct WaitStartAtten   : public timeout_ms_state<defs::TT_MATCH_SEQUENCE_MS> { };
    struct Sounding         : public timeout_ms_state<defs::TT_EVSE_MATCH_MNBC_MS> { };
    struct FinalizeSounding : public timeout_ms_state<FINALIZE_SOUNDING_DELAY_MS> { };
    struct WaitAttenRsp     : public timeout_ms_state<defs::TT_MATCH_RESPONSE_MS> { };
    struct WaitSlacMatch    : public timeout_ms_state<defs::TT_EVSE_MATCH_SESSION_MS> {  };
    // clang-format on
    struct MatchComplete    : public state<> {
        typedef boost::mpl::vector<SessionMatched> flag_list;
    };
    struct Failed           : public state<> {
        typedef boost::mpl::vector<SessionFailed> flag_list;
    };

    // Transitions
    using initial_state = WaitStartAtten;
    using retry_timeout         = And_<timeout, retry_limit>;
    using log_no_start_atten    = log_session_failed<fail_no_start_atten>;
    using log_no_atten_rsp      = log_session_failed<fail_no_atten_rsp>;
    using log_no_slac_match     = log_session_failed<fail_no_slac_match>;
    using log_validation_window = log_session_failed<fail_validation_window>;
    // clang-format off
    struct transition_table : boost::mpl::vector<
        //    +------------------+---------+------------------+-----------------------+---------------------------+
        //    | Source           | Event   | Target           | Action                | Guard                     |
        //    +------------------+---------+------------------+-----------------------+---------------------------+
        Row   < WaitStartAtten   , update  , Failed           , log_no_start_atten    , timeout                   >,
        Row   < WaitStartAtten   , message , Sounding         , sounding_started      , start_atten_in_time       >,
        Row   < Sounding         , update  , FinalizeSounding , none                  , timeout                   >,
        Row   < Sounding         , message , none             , capture_sound         , sound_below_limit         >,
        Row   < Sounding         , message , FinalizeSounding , capture_sound         , sound_completes_count     >,
        Row   < FinalizeSounding , update  , WaitAttenRsp     , finalize_snd          , timeout                   >,
        Row   < WaitAttenRsp     , update  , WaitAttenRsp     , retry_snd             , timeout                   >,
        Row   < WaitAttenRsp     , update  , Failed           , log_no_atten_rsp      , retry_timeout             >,
        Row   < WaitAttenRsp     , message , WaitSlacMatch    , on_atten_char_rsp     , is_atten_char_rsp_guard   >,
        Row   < WaitSlacMatch    , update  , Failed           , log_no_slac_match     , timeout                   >,
        Row   < WaitSlacMatch    , update  , Failed           , log_validation_window , validation_window_expired >,
        Row   < WaitSlacMatch    , message , MatchComplete    , match_cnf             , is_slac_match_req_guard   >
        //    +------------------+---------+------------------+-----------------------+---------------------------+
        >{};
    // clang-format on
    template <class FSM,class Event>
    void no_transition(Event const&, FSM&, int) { }

    // Entry / exit
    template <class Event, class Fsm>
    void on_entry(Event const&, Fsm&) {
        //ctx = fsm.ctx; <- does not work here, since there is no parent FSM
        session_data.num_retries = 0;
    }

    // Members
    fsm::evse::MatchingSessionData session_data;
    fsm::evse::Context* ctx;
};

} // namespace everest::lib::slac::msm::session_sm
