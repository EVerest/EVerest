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

    // Member guards (used via row / from the functors in session_actions.hpp)
    template<class MsgT>
    static bool check_message(message const& e, std::uint16_t expected, fsm::evse::MatchingSessionData const& session_data) {
        const auto mmtype = e.payload.get_mmtype();
        if(mmtype not_eq expected){
            return false;
        }
        auto const msg = e.payload.template payload_as<MsgT>();
        if (not msg.has_value()) {
            return false;
        }
        return session_data.validate_message(*msg);
    }
    bool is_start_atten_char(message const& e) {
        auto mmtype = defs::MMTYPE_CM_START_ATTEN_CHAR | defs::MMTYPE_MODE_IND;
        return check_message<slac::messages::cm_start_atten_char_ind>(e, mmtype, session_data);
    }
    bool is_atten_char_rsp(message const& e) {
        auto mmtype = slac::defs::MMTYPE_CM_ATTEN_CHAR | slac::defs::MMTYPE_MODE_RSP;
        return check_message<slac::messages::cm_atten_char_rsp>(e, mmtype, session_data);
    }
    bool is_slac_match_req(message const& e) {
        auto mmtype = slac::defs::MMTYPE_CM_SLAC_MATCH | slac::defs::MMTYPE_MODE_REQ;
        return check_message<slac::messages::cm_slac_match_req>(e, mmtype, session_data);
    }
    bool is_atten_profile_ind(message const& e) {
        auto mmtype = slac::defs::MMTYPE_CM_ATTEN_PROFILE | slac::defs::MMTYPE_MODE_IND;
        return check_message<slac::messages::cm_atten_profile_ind>(e, mmtype, session_data);
    }

    // Member actions (used via row)
    void on_atten_char_rsp(message const&) {
        ctx->log_info(session_log_prefix(session_data) +
                      "Received CM_ATTEN_CHAR.RSP, waiting for CM_SLAC_MATCH.REQ");
    }
    void match_cnf(message const& e){
        messages::cm_slac_match_cnf& reply = ctx->match_confirm_cache.message;
        auto const msg = e.payload.payload_as<slac::messages::cm_slac_match_req>();
        if (not msg.has_value()) {
            return;
        }
        ctx->log_info(session_log_prefix(session_data) +
                      "Received CM_SLAC_MATCH.REQ, sending CM_SLAC_MATCH.CNF -> session complete");
        static constexpr Nmk failed_match_session_nmk{0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                                                     0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10};

        Nmk const* session_nmk = &ctx->slac_config.session_nmk;
        if (ctx->slac_config.link_status.debug_simulate_failed_matching) {
            ctx->log_info("Sending wrong NMK to EV to simulate a failed link setup after match request");
            session_nmk = &failed_match_session_nmk;
        }

        session_data.create_cm_slac_match_cnf(reply, *msg, *session_nmk);
        if (not ctx->send_slac_message(session_data.ev_mac, reply)) {
            ctx->log_warn("Failed to send CM_SLAC_MATCH.CNF");
        }
        ctx->signal_cm_slac_match_cnf(session_data.ev_mac.data());
        ctx->cache_match_confirm_message(reply, session_data.ev_mac, session_data.evse_mac, session_data.run_id);
        std::copy(std::begin(session_data.ev_mac), std::end(session_data.ev_mac), std::begin(ctx->status.ev_mac));
    }

    // Transitions
    using initial_state = WaitStartAtten;
    using p = Session_def;
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
        row   < WaitAttenRsp     , message , WaitSlacMatch    , &p::on_atten_char_rsp , &p::is_atten_char_rsp     >,
        Row   < WaitSlacMatch    , update  , Failed           , log_no_slac_match     , timeout                   >,
        Row   < WaitSlacMatch    , update  , Failed           , log_validation_window , validation_window_expired >,
        row   < WaitSlacMatch    , message , MatchComplete    , &p::match_cnf         , &p::is_slac_match_req     >
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
