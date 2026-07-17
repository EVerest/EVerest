// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 - 2026 Pionix GmbH and Contributors to EVerest

#pragma once
#include <everest/slac/fsm/slac_msm_helpers.hpp>

#include <boost/mpl/vector.hpp>
#include <boost/msm/back/state_machine.hpp>
#include <boost/msm/front/completion_event.hpp>
#include <boost/msm/front/functor_row.hpp>
#include <boost/msm/front/internal_row.hpp>
#include <boost/msm/front/state_machine_def.hpp>
#include <boost/msm/front/states.hpp>
#include <everest/slac/slac_defs.hpp>
#include <everest/slac/slac_messages.hpp>
#include <everest/slac/slac_utils.hpp>

#include <everest/slac/HomeplugMessage.hpp>
#include <everest/slac/MatchingSessionData.hpp>
#include <everest/slac/fsm/context.hpp>
#include <everest/slac/timer.hpp>

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace everest::lib::slac::msm {
using namespace everest::lib::slac;
using namespace std::chrono_literals;
using namespace boost::msm::front;
using namespace boost::msm::back;

inline bool accepts_set_key_cnf_success_result(fsm::evse::SetKeyCnfSuccessMode mode, std::uint8_t result) {
    switch (mode) {
    case fsm::evse::SetKeyCnfSuccessMode::modem_compat_0x01:
        return result == defs::CM_SET_KEY_CNF_RESULT_MODEM_COMPAT_SUCCESS;
    case fsm::evse::SetKeyCnfSuccessMode::hpgp_standard_0x00:
        return result == defs::CM_SET_KEY_CNF_RESULT_HPGP_SUCCESS;
    case fsm::evse::SetKeyCnfSuccessMode::accept_0x00_or_0x01:
        return result == defs::CM_SET_KEY_CNF_RESULT_HPGP_SUCCESS ||
               result == defs::CM_SET_KEY_CNF_RESULT_MODEM_COMPAT_SUCCESS;
    }
    return false;
}

inline std::string format_session_nmk_for_log(Nmk const& nmk) {
    static constexpr char hex_chars[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(nmk.size() * 2U);
    for (auto const octet : nmk) {
        out.push_back(hex_chars[(octet >> 4U) & 0x0FU]);
        out.push_back(hex_chars[octet & 0x0FU]);
    }
    return out;
}

inline std::string format_mac_for_log(MacAddress const& mac) {
    static constexpr char hex_chars[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(mac.size() * 3U);
    for (auto const octet : mac) {
        if (not out.empty()) {
            out.push_back(':');
        }
        out.push_back(hex_chars[(octet >> 4U) & 0x0FU]);
        out.push_back(hex_chars[octet & 0x0FU]);
    }
    return out;
}

inline std::string format_run_id_for_log(RunId const& run_id) {
    static constexpr char hex_chars[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(run_id.size() * 2U);
    for (auto const octet : run_id) {
        out.push_back(hex_chars[(octet >> 4U) & 0x0FU]);
        out.push_back(hex_chars[octet & 0x0FU]);
    }
    return out;
}

// Log prefix identifying a matching session, same style as EvseSlac's session_log().
inline std::string session_log_prefix(fsm::evse::MatchingSessionData const& session_data) {
    return "Session (run_id=" + format_run_id_for_log(session_data.run_id) +
           ", ev_mac=" + format_mac_for_log(session_data.ev_mac) + "): ";
}

// Guards
// clang-format off
struct is_link_detection_on
{
    template <class Fsm, class Evt, class SrcT, class TarT>
    bool operator()(Evt const&, Fsm&, SrcT&, TarT& )
    {
        return true;
    }
};
struct is_lumissil
{
    template <class Fsm, class Evt, class SrcT, class TarT>
    bool operator()(Evt const&, Fsm& fsm, SrcT&, TarT& ) {
        return fsm.ctx->modem_vendor == defs::ModemVendor::Lumissil;
    }
};
struct is_qualcomm
{
    template <class Fsm, class Evt, class SrcT, class TarT>
    bool operator()(Evt const&, Fsm& fsm, SrcT&, TarT& ) {
        return fsm.ctx->modem_vendor == defs::ModemVendor::Qualcomm;
    }
};
struct link_status_cnf
{
    template <class Fsm, class Evt, class SrcT, class TarT>
    bool operator()(Evt const& e, Fsm& fsm, SrcT& src, TarT&) {
        return src.link_status_cnf(e, fsm);
    }
};

struct link_status_neg
{
    template <class Fsm, class Evt, class SrcT, class TarT>
    bool operator()(Evt const& e, Fsm& fsm, SrcT& src, TarT&) {
        return src.is_link_status_neg(e, fsm);
    }
};

struct failed
{
    template <class Fsm, class Evt, class SrcT, class TarT>
    bool operator()(Evt const&, Fsm& fsm, SrcT& src, TarT& )
    {
        return src.state_failed();
    }
};

struct link_status_req
{
    template <class Fsm, class Evt, class SrcT, class TarT>
    void operator()(Evt const&, Fsm& fsm, SrcT&, TarT& tar) {
        // Re-arm the poll timer only when we actually poll, so the link-status
        // poll cadence is not disturbed by unrelated self-transitions (see
        // CheckLink::on_entry).
        tar.to.reset();
        tar.link_status_req(fsm);
    }
};

// Flags
struct SessionFailed{};
struct SessionMatched{};
struct CheckLink     : public state<> {
    template <class Event, class Fsm>
    void on_entry(Event const&, Fsm& fsm) {
        // Only (re)arm the poll duration here -- do NOT reset the timer's
        // reference. The poll timer is reset when a LINK_STATUS.REQ is actually
        // sent (see the link_status_req action). Otherwise every self-transition
        // that re-enters this substate for another reason (notably the Neo-only
        // CM_AMP_MAP retransmit/responder in the matched state) would restart the
        // countdown and starve the link-status poll, so a connection loss would
        // not be detected within TP_match_leave (ISO 15118-5 PLCLinkStatus_005).
        to.setDuration(std::chrono::milliseconds(fsm.link_check_to_ms));
    }

    timer to;
    bool state_timeout() {
        return to.timeout();
    }
};
struct Lumissil      : public CheckLink {
    template <class Fsm, class Evt>
    bool is_link_status_message(Evt const& e, Fsm&) {
        const auto mmtype = e.payload.get_mmtype();
        return mmtype == (defs::lumissil::MMTYPE_NSCM_GET_D_LINK_STATUS | defs::MMTYPE_MODE_CNF);
    }

    template <class Fsm, class Evt>
    bool link_status_cnf(Evt const& e, Fsm& fsm) {
        if (not is_link_status_message(e, fsm)) {
            return false;
        }
        auto const link_status_msg = e.payload.template payload_as<messages::lumissil::nscm_get_d_link_status_cnf>();
        return link_status_msg.has_value() && (link_status_msg->link_status == defs::D_LINK_STATUS_LINKED);
    }

    template <class Fsm, class Evt>
    bool is_link_status_neg(Evt const& e, Fsm& fsm) {
        if (not is_link_status_message(e, fsm)) {
            return false;
        }
        auto const link_status_msg = e.payload.template payload_as<messages::lumissil::nscm_get_d_link_status_cnf>();
        return link_status_msg.has_value() && (link_status_msg->link_status != defs::D_LINK_STATUS_LINKED);
    }

    template <class Fsm>
    void link_status_req(Fsm& fsm) {
        messages::lumissil::nscm_get_d_link_status_req link_status_req{};
        if (not fsm.ctx->send_slac_message(fsm.ctx->slac_config.plc_peer_mac, link_status_req)) {
            fsm.ctx->log_warn("Failed to send CM_GET_D_LINK_STATUS.REQ to SLAC peer");
        }
    }
    template <class Event, class Fsm> void on_entry(Event const& e, Fsm& fsm) {
        CheckLink::on_entry(e, fsm);
    }

};
struct Qualcomm      : public CheckLink {
    template <class Fsm, class Evt>
    bool is_link_status_message(Evt const& e, Fsm&) {
        const auto mmtype = e.payload.get_mmtype();
        return mmtype == (defs::qualcomm::MMTYPE_LINK_STATUS | defs::MMTYPE_MODE_CNF);
    }

    template <class Fsm, class Evt>
    bool link_status_cnf(Evt const& e, Fsm& fsm) {
        if (not is_link_status_message(e, fsm)) {
            return false;
        }
        auto const link_status_msg = e.payload.template payload_as<messages::qualcomm::link_status_cnf>();
        return link_status_msg.has_value() && (link_status_msg->link_status == defs::D_LINK_STATUS_LINKED);
    }

    template <class Fsm, class Evt>
    bool is_link_status_neg(Evt const& e, Fsm& fsm) {
        if (not is_link_status_message(e, fsm)) {
            return false;
        }
        auto const link_status_msg = e.payload.template payload_as<messages::qualcomm::link_status_cnf>();
        return link_status_msg.has_value() && (link_status_msg->link_status != defs::D_LINK_STATUS_LINKED);
    }

    template <class Fsm>
    void link_status_req(Fsm& fsm) {
        messages::qualcomm::link_status_req link_status_req{};
        if (not fsm.ctx->send_slac_message(fsm.ctx->slac_config.plc_peer_mac, link_status_req)) {
            fsm.ctx->log_warn("Failed to send LINK_STATUS.REQ to SLAC peer");
        }
    }
    template <class Event, class Fsm> void on_entry(Event const& e, Fsm& fsm) {
        CheckLink::on_entry(e, fsm);
    }
};


// SubState StateMachines
struct Session_def     : public state_machine_def<Session_def> {
    // States
    static constexpr auto FINALIZE_SOUNDING_DELAY_MS = 45;
    struct WaitStartAtten   : public timeout_ms_state<defs::TT_MATCH_SEQUENCE_MS> { };
    struct Sounding         : public timeout_ms_state<defs::TT_EVSE_MATCH_MNBC_MS> { };
    struct FinalizeSounding : public timeout_ms_state<FINALIZE_SOUNDING_DELAY_MS> { };
    struct WaitAttenRsp     : public timeout_ms_state<defs::TT_MATCH_RESPONSE_MS> { };
    struct WaitSlacMatch    : public timeout_ms_state<defs::TT_EVSE_MATCH_SESSION_MS> {  };
    struct MatchComplete    : public state<> {
        typedef boost::mpl::vector<SessionMatched> flag_list;
    };
    struct Failed           : public state<> {
        typedef boost::mpl::vector<SessionFailed> flag_list;
    };

    // Guards
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

    struct sound_below_limit {
        template <class Fsm, class SrcT, class TarT>
        bool operator()(message const& e, Fsm& fsm, SrcT&, TarT&) {
            return fsm.session_data.captured_sounds + 1 < slac::defs::CM_SLAC_PARM_CNF_NUM_SOUNDS and
                   fsm.is_atten_profile_ind(e);
        }
    };
    struct sound_completes_count {
        template <class Fsm, class SrcT, class TarT>
        bool operator()(message const& e, Fsm& fsm, SrcT&, TarT&) {
            return fsm.session_data.captured_sounds + 1 >= slac::defs::CM_SLAC_PARM_CNF_NUM_SOUNDS and
                   fsm.is_atten_profile_ind(e);
        }
    };
    // After a CM_VALIDATE process, the CM_SLAC_MATCH.REQ must arrive within TT_match_sequence (much
    // shorter than the overall TT_EVSE_match_session that bounds WaitSlacMatch otherwise). When that
    // shorter window elapses the matching has FAILED (ISO 15118-5 CmSlacMatch_003/004 cmValidate
    // variant); a late CM_SLAC_MATCH.REQ must then get no CM_SLAC_MATCH.CNF.
    struct validation_window_expired {
        template <class Fsm, class Evt, class SrcT, class TarT>
        bool operator()(Evt const&, Fsm& fsm, SrcT&, TarT&) {
            return fsm.ctx->validation_done and fsm.ctx->validation_match_window.timeout();
        }
    };
    struct retry_limit {
        template <class Fsm, class Evt, class SrcT, class TarT>
        bool operator()(Evt const&, Fsm& fsm, SrcT&, TarT& ) {
            return fsm.session_data.num_retries >= slac::defs::C_EV_MATCH_RETRY;
        }
    };

    struct start_atten_in_time {
        template <class Fsm, class SrcT, class TarT>
        bool operator()(message const& e, Fsm& fsm, SrcT& src, TarT&) {
            return not src.state_timeout() and fsm.is_start_atten_char(e);
        }
    };

    // Actions
    struct sounding_started {
        template <class Fsm, class SrcT, class TarT>
        void operator()(message const&, Fsm& fsm, SrcT&, TarT&) {
            fsm.ctx->log_info(session_log_prefix(fsm.session_data) +
                              "Received CM_START_ATTEN_CHAR.IND, MNBC sounding started");
        }
    };
    // Accumulate one CM_ATTEN_PROFILE.IND (see sound_below_limit / sound_completes_count).
    struct capture_sound {
        template <class Fsm, class SrcT, class TarT>
        void operator()(message const& e, Fsm& fsm, SrcT&, TarT&) {
            auto const msg = e.payload.payload_as<slac::messages::cm_atten_profile_ind>();
            if (not msg.has_value()) {
                return;
            }
            for (int i = 0; i < slac::defs::AAG_LIST_LEN; ++i) {
                fsm.session_data.captured_aags[i] += msg->aag[i];
            }
            fsm.session_data.captured_sounds++;
            fsm.ctx->log_debug(session_log_prefix(fsm.session_data) + "Received CM_ATTEN_PROFILE.IND (" +
                               std::to_string(fsm.session_data.captured_sounds) + " of " +
                               std::to_string(slac::defs::CM_SLAC_PARM_CNF_NUM_SOUNDS) + " sounds captured)");
            if (fsm.session_data.captured_sounds >= slac::defs::CM_SLAC_PARM_CNF_NUM_SOUNDS) {
                fsm.ctx->log_info(session_log_prefix(fsm.session_data) +
                                  "Received all sounds, finalizing sounding");
            }
        }
    };

    template <class Fsm> static void send_atten_char(Fsm& fsm) {
        auto atten_char = fsm.session_data.create_cm_atten_char_ind(fsm.ctx->slac_config.sounding_atten_adjustment);
        if (not fsm.ctx->send_slac_message(fsm.session_data.ev_mac, atten_char)) {
            fsm.ctx->log_warn("Failed to send CM_ATTEN_CHAR.IND");
        }
        int aag_overall_sum = 0;
        for (size_t i = 0; i < slac::defs::AAG_LIST_LEN; ++i) {
            aag_overall_sum += atten_char.attenuation_profile.aag[i];
        }
        fsm.ctx->status.average_attenuation = aag_overall_sum / slac::defs::AAG_LIST_LEN;
        std::ostringstream ss;
        ss << "Avg atten.: " << std::fixed << std::setprecision(1)
           << (static_cast<double>(aag_overall_sum) / slac::defs::AAG_LIST_LEN) << " dB";
        if (fsm.ctx->slac_config.sounding_atten_adjustment != 0) {
            ss << " plus offset " << std::to_string(fsm.ctx->slac_config.sounding_atten_adjustment) << " dB";
        }
        ss << ", from " << std::to_string(slac::defs::AAG_LIST_LEN) << " groups, "
           << fsm.session_data.captured_sounds << " sounds";
        fsm.ctx->log_info(session_log_prefix(fsm.session_data) + ss.str());
    }
    struct finalize_snd {
        template <class Fsm, class Evt, class SrcT, class TarT>
        void operator()(Evt const&, Fsm& fsm, SrcT&, TarT& ) {
            fsm.ctx->log_info(session_log_prefix(fsm.session_data) + "Finalize sounding, sending CM_ATTEN_CHAR.IND");
            send_atten_char(fsm);
        }
    };
    struct retry_snd {
        template <class Fsm, class Evt, class SrcT, class TarT>
        void operator()(Evt const&, Fsm& fsm, SrcT&, TarT&) {
            fsm.ctx->log_info(session_log_prefix(fsm.session_data) +
                              "No CM_ATTEN_CHAR.RSP yet, retransmitting CM_ATTEN_CHAR.IND (retry " +
                              std::to_string(fsm.session_data.num_retries + 1) + " of " +
                              std::to_string(slac::defs::C_EV_MATCH_RETRY) + ")");
            send_atten_char(fsm);
            fsm.session_data.num_retries++;
        }
    };
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

    template <char const* Reason> struct log_session_failed {
        template <class Fsm, class Evt, class SrcT, class TarT>
        void operator()(Evt const&, Fsm& fsm, SrcT&, TarT&) {
            fsm.ctx->log_info(session_log_prefix(fsm.session_data) + Reason);
        }
    };
    static constexpr char const fail_no_start_atten[] =
        "Timeout waiting for CM_START_ATTEN_CHAR.IND, session failed";
    static constexpr char const fail_no_atten_rsp[] =
        "No CM_ATTEN_CHAR.RSP after all retries, session failed";
    static constexpr char const fail_no_slac_match[] =
        "Timeout waiting for CM_SLAC_MATCH.REQ, session failed";
    static constexpr char const fail_validation_window[] =
        "No CM_SLAC_MATCH.REQ within TT_match_sequence after CM_VALIDATE, session failed";

    // Transitions
    using initial_state = WaitStartAtten;
    using p = Session_def;
    using retry_timeout         = And_<timeout, retry_limit>;
    using log_no_start_atten    = log_session_failed<fail_no_start_atten>;
    using log_no_atten_rsp      = log_session_failed<fail_no_atten_rsp>;
    using log_no_slac_match     = log_session_failed<fail_no_slac_match>;
    using log_validation_window = log_session_failed<fail_validation_window>;
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
    template <class FSM,class Event>
    void no_transition(Event const&, FSM&, int) { }

    // Members
    template <class Event, class Fsm>
    void on_entry(Event const&, Fsm&) {
        //ctx = fsm.ctx; <- does not work here, since there is no parent FSM
        session_data.num_retries = 0;
    }

    fsm::evse::MatchingSessionData session_data;
    fsm::evse::Context* ctx;
};
struct Matching_def    : public state_machine_def<Matching_def> {
    // States
    struct Init    : public state<> { };
    struct Listen  : public state<> { };
    struct Pipe    : public state<> { };
    struct Matched : public exit_pseudo_state<update> { };
    struct Failed  : public exit_pseudo_state<update> { };

    // Guards
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
    struct is_slac_param_req : public is_message_of_type<slac::defs::MMTYPE_CM_SLAC_PARAM | slac::defs::MMTYPE_MODE_REQ> { };

    //Actions
    struct pipe_event {
        template <class Fsm, class Evt, class SrcT, class TarT>
        void operator()(Evt const& e, Fsm& fsm, SrcT&, TarT& ) {
            for(auto& elem : fsm.sessions){
                elem.process_event(e);
            }
        }
    };

    // ISO 15118-3 9.4 CM_VALIDATE BCB-toggle validation. The EV proves it sits on the same Control-Pilot
    // wire by switching its CP B->C->B (a "toggle") a number of times. The exchange has two steps,
    // distinguished by the REQ's pilotTimer field:
    //   step 1 (pilotTimer == 0): the EVSE answers CM_VALIDATE.CNF result=READY(0x01), toggle_num=0, and
    //     arms the observation. If the EV sends no step 2, the EVSE autonomously repeats the step-1 CNF
    //     every TT_match_sequence, limited to C_EV_match_retry repetitions, then FAILS silently.
    //   step 2 (pilotTimer != 0): the EV sends the REQ carrying TP_EV_vald_toggle (pilotTimer, 100 ms
    //     units) and then performs its BCB toggles during that window. Per ISO 15118-3 the EVSE waits out
    //     the window and only THEN answers result=SUCCESS(0x02) with toggle_num = number of toggles seen.
    // EvseManager counts one B->C edge per BCB toggle into ctx->bc_transition_count via the slac
    // count_bc command, so the delta over the window is the toggle count. A step-2 REQ from the owner carrying a non-READY result is
    // the EV's decision to terminate/skip -> no CNF. A step-1 REQ from a different EV while a validation is
    // in progress is answered NOT_READY(0x00) (processing blocked, [V2G3-M09-13] / CmValidate_009).
    struct handle_validate_req {
        template <class Fsm, class SrcT, class TarT>
        void operator()(message const& e, Fsm& fsm, SrcT&, TarT&) {
            const auto* src_mac = e.payload.get_src_mac();
            const auto req = e.payload.payload_as<messages::cm_validate_req>();

            // Invalid/unsupported signalType: ignore it and leave any in-progress step-1 repetition
            // running (ISO 15118-5 CmValidate_004 sends signalType 0xFF between step-1 CNFs).
            if (not req.has_value() or req->signal_type != defs::CM_VALIDATE_REQ_SIGNAL_TYPE) {
                return;
            }

            if (req->timer == 0x00) {
                // Step 1. A different EV mid-validation is blocked (0x00).
                if (fsm.validate_armed and src_mac != nullptr and
                    not wire_pointer_equal(src_mac, fsm.validate_owner_mac)) {
                    if (src_mac != nullptr) {
                        fsm.send_validate_cnf_reply(byte_array_from_wire<MacAddress>(src_mac),
                                                    defs::CM_VALIDATE_REQ_RESULT_NOT_READY, 0);
                    }
                    return;
                }
                // (Re-)arm: answer READY and (re)start the step-1 repetition interval. A repeated step-1
                // REQ resets the retry counter (CmValidate_002).
                if (src_mac != nullptr) {
                    fsm.validate_owner_mac = byte_array_from_wire<MacAddress>(src_mac);
                }
                fsm.validate_armed = true;
                fsm.validate_step2_pending = false;
                fsm.validate_step1_retries = 0;
                // Snapshot the edge counter now, at step 1: no BCB toggle has happened yet (the EV
                // toggles only during the step-2 window), so any edges counted from here to the end of
                // that window are exactly the toggles. Snapshotting at step 2 instead would race the
                // CP path (MQTT) against the slower SLAC-frame path and miss the first toggle's edges.
                fsm.validate_baseline_bc = fsm.ctx->bc_transition_count.load();
                fsm.validate_timer.setDurationMilliSeconds(defs::TT_MATCH_SEQUENCE_MS);
                fsm.validate_timer.reset();
                fsm.send_validate_cnf_reply(fsm.validate_owner_mac, defs::CM_VALIDATE_REQ_RESULT_READY, 0);
                return;
            }

            // Step 2 (pilotTimer != 0): only the owner of the armed validation may proceed.
            if (not fsm.validate_armed or src_mac == nullptr or
                not wire_pointer_equal(src_mac, fsm.validate_owner_mac)) {
                return;
            }
            // A non-READY result is the EV's decision to terminate/skip: no CNF, end the validation
            // (CmValidate_005/006/007/008).
            if (req->result != defs::CM_VALIDATE_REQ_RESULT_READY) {
                fsm.validate_armed = false;
                fsm.validate_step2_pending = false;
                return;
            }
            // Start the toggle-observation window: wait out TP_EV_vald_toggle (pilotTimer in 100 ms
            // units) before counting. The baseline was snapshotted at step 1 (before any toggle); the
            // CNF is sent by validate_tick when the window elapses.
            fsm.validate_step2_pending = true;
            fsm.validate_timer.setDurationMilliSeconds((static_cast<long long>(req->timer) + 1) * 100);
            fsm.validate_timer.reset();
        }
    };

    // Serviced on every `update` tick while a validation is armed (guard validate_needs_service): either
    // close the step-2 toggle window with a SUCCESS CNF, or repeat/expire the step-1 CNF.
    struct validate_tick {
        template <class Fsm, class Evt, class SrcT, class TarT>
        void operator()(Evt const&, Fsm& fsm, SrcT&, TarT&) {
            if (fsm.validate_step2_pending) {
                // EvseManager counts one B->C edge per BCB toggle, so the delta since the baseline IS
                // the number of toggles the EV performed during the observation window.
                const int toggles_seen = fsm.ctx->bc_transition_count.load() - fsm.validate_baseline_bc;
                const int toggles = std::max(0, std::min(toggles_seen, 255));
                fsm.send_validate_cnf_reply(fsm.validate_owner_mac,
                                            defs::CM_VALIDATE_REQ_RESULT_SUCCESS,
                                            static_cast<std::uint8_t>(toggles));
                fsm.validate_armed = false;
                fsm.validate_step2_pending = false;
                fsm.ctx->log_info("CM_VALIDATE: detected " + std::to_string(toggles) +
                                  " BCB toggle(s) during the toggle window");
                // Validation done: the CM_SLAC_MATCH.REQ must now arrive within TT_match_sequence
                // (ISO 15118-5 CmSlacMatch_003/004 cmValidate variant), not the full match session.
                fsm.ctx->validation_done = true;
                fsm.ctx->validation_match_window.setDurationMilliSeconds(defs::TT_MATCH_SEQUENCE_MS);
                fsm.ctx->validation_match_window.reset();
            } else if (fsm.validate_armed) {
                if (fsm.validate_step1_retries < slac::defs::C_EV_MATCH_RETRY) {
                    fsm.validate_step1_retries++;
                    fsm.send_validate_cnf_reply(fsm.validate_owner_mac,
                                                defs::CM_VALIDATE_REQ_RESULT_READY, 0);
                    fsm.validate_timer.reset();
                } else {
                    // Retry limit reached with no step 2: the validation (matching) has FAILED; stop
                    // answering (CmValidate_003/004).
                    fsm.validate_armed = false;
                }
            }
        }
    };
    struct validate_needs_service {
        template <class Fsm, class Evt, class SrcT, class TarT>
        bool operator()(Evt const&, Fsm& fsm, SrcT&, TarT&) {
            return (fsm.validate_armed or fsm.validate_step2_pending) and fsm.validate_timer.timeout();
        }
    };

    struct reset_matching_subfsm {
        template <class Evt, class Fsm, class SrcT, class TarT>
        void operator()(Evt const&, Fsm& fsm, SrcT&, TarT&) {
            fsm.ctx->status.session_count = 0;
            fsm.sessions.clear();
            fsm.to.setDuration(std::chrono::milliseconds(fsm.ctx->slac_config.slac_init_timeout_ms));
            fsm.to.reset();
            fsm.failed_matching_reset_once = true;
        }
    };

    struct add_session {
        template <class Fsm, class SrcT, class TarT>
        void operator()(message const& e, Fsm& fsm, SrcT&, TarT& ) {
            // Add session
            auto& ctx = *fsm.ctx;
            auto const msg = e.payload.payload_as<slac::messages::cm_slac_parm_req>();
            if (not msg.has_value()) {
                return;
            }
            if (not fsm::evse::MatchingSessionData::validate_message(*msg)) {
                return;
            }
            auto const ev_mac = byte_array_from_wire<MacAddress>(e.payload.get_src_mac());
            auto const run_id = byte_array_from_wire<RunId>(msg->run_id);
            fsm::evse::MatchingSessionData data(ev_mac, run_id, ctx.evse_mac);
            auto session_iter = std::find_if(fsm.sessions.begin(), fsm.sessions.end(),
                                            [&data](Session const& session) {
                                                return session.session_data.matches_identity(data.ev_mac, data.run_id);
                                            });
            if (session_iter == fsm.sessions.end()) {
                auto const max_matching_sessions = fsm.max_matching_sessions();
                if (static_cast<int>(fsm.sessions.size()) >= max_matching_sessions) {
                    ctx.log_warn("Ignoring CM_SLAC_PARM.REQ because max_matching_sessions was reached (" +
                                 std::to_string(max_matching_sessions) + ")");
                    return;
                }
                session_iter = fsm.sessions.emplace(fsm.sessions.end());
            }
            auto& session = *session_iter;
            session.session_data = data;
            session.ctx = fsm.ctx;
            session.start();
            // send reply
            ctx.log_info(session_log_prefix(data) + "Received CM_SLAC_PARM.REQ, sending CM_SLAC_PARM.CNF");
            auto param_confirm = data.create_cm_slac_parm_cnf();
            if (not ctx.send_slac_message(data.ev_mac, param_confirm)) {
                ctx.log_warn("Failed to send CM_SLAC_PARM.CNF");
            }
            ctx.signal_cm_slac_parm_req(data.ev_mac.data());
            ctx.status.session_count = fsm.sessions.size();
        }
    };
    struct is_validate_req : public is_message_of_type<defs::MMTYPE_CM_VALIDATE | defs::MMTYPE_MODE_REQ> { };

    struct should_reset_instead_of_fail {
        template <class Evt, class Fsm, class SrcT, class TarT>
        bool operator()(Evt const&, Fsm& fsm, SrcT&, TarT& ) {
            auto const should_timeout = fsm.to.timeout();
            auto const is_failed = fsm.is_failed(update{});
            auto const no_sessions = fsm.sessions.empty();
            auto const should_reset = (not fsm.failed_matching_reset_once) and
                                      fsm.ctx->slac_config.reset_instead_of_fail and
                                      (is_failed or (no_sessions && should_timeout));
            return should_reset;
        }
    };
    struct should_transition_to_failed_matching {
        template <class Evt, class Fsm, class SrcT, class TarT>
        bool operator()(Evt const&, Fsm& fsm, SrcT&, TarT& ) {
            auto const should_timeout = fsm.to.timeout();
            auto const is_failed = fsm.is_failed(update{});
            auto const no_sessions = fsm.sessions.empty();
            auto const should_fail = (is_failed or (no_sessions && should_timeout)) and
                                     ((not fsm.ctx->slac_config.reset_instead_of_fail) or fsm.failed_matching_reset_once);
            return should_fail;
        }
    };

    // Transitions
    using Session = state_machine<Session_def>;
    using initial_state = boost::mpl::vector<Init, Listen, Pipe>;
    using p = Matching_def;
    using fail_matching = should_transition_to_failed_matching;
    using reset_matching = should_reset_instead_of_fail;
    using not_validate_req = Not_<is_validate_req>;
    struct transition_table : boost::mpl::vector<
        //    +--------+---------+---------+-----------------------+------------------------+
        //    | Source | Event   | Target  | Action                | Guard                  |
        //    +--------+---------+---------+-----------------------+------------------------+
        g_row < Init   , update  , Matched /* none */              , &p::is_matched         >,
        Row   < Init   , update  , Failed  , none                  , fail_matching          >,
        Row   < Init   , update  , Init    , reset_matching_subfsm , reset_matching         >,
        //    +--------+---------+---------+-----------------------+------------------------+
        Row   < Listen , message , Listen  , add_session           , is_slac_param_req      >,
        Row   < Listen , message , Listen  , handle_validate_req   , is_validate_req        >,
        Row   < Listen , update  , Listen  , validate_tick         , validate_needs_service >,
        //    +--------+---------+---------+-----------------------+------------------------+
        Row   < Pipe   , message , none    , pipe_event            , not_validate_req       >,
        Row   < Pipe   , update  , none    , pipe_event            , none                   >
        //    +--------+---------+---------+-----------------------+------------------------+
        >{};

    template <class FSM,class Event>
    void no_transition(Event const&, FSM&, int) {
    }

    // Members
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
struct Reset_def       : public state_machine_def<Reset_def> {
    // States
    struct Init      : public state<>{ };
    struct MsgSent   : public state<>{ };
    struct MsgValid  : public state<>{ };
    struct ResetChip : public exit_pseudo_state<update>{ };
    struct Idle      : public exit_pseudo_state<update>{ };

    // Guards
    struct msg_expected : public is_message_of_type<defs::MMTYPE_CM_SET_KEY | defs::MMTYPE_MODE_CNF>{ };
    struct is_retry_confirmed_set_key {
        template <class Fsm, class Evt, class SrcT, class TarT>
        bool operator()(Evt const&, Fsm& fsm, SrcT&, TarT& ) {
            return fsm.ctx->slac_config.set_key_handling_mode == fsm::evse::SetKeyHandlingMode::retry_confirmed;
        }
    };
    struct is_legacy_single_attempt_set_key {
        template <class Fsm, class Evt, class SrcT, class TarT>
        bool operator()(Evt const&, Fsm& fsm, SrcT&, TarT& ) {
            return fsm.ctx->slac_config.set_key_handling_mode == fsm::evse::SetKeyHandlingMode::legacy_single_attempt;
        }
    };
    struct set_key_timeout {
        template <class Evt, class Fsm, class SrcT, class TarT>
        bool operator()(Evt const&, Fsm& fsm, SrcT&, TarT& ) {
            return fsm.set_key_timeout();
        }
    };
    struct has_set_key_cnf_payload {
        template <class Evt, class Fsm, class SrcT, class TarT>
        bool operator()(Evt const& e, Fsm&, SrcT&, TarT& ) {
            const auto msg = e.payload.template payload_as<messages::cm_set_key_cnf>();
            return msg.has_value();
        }
    };
    struct has_set_key_attempts_left {
        template <class Fsm, class Evt, class SrcT, class TarT>
        bool operator()(Evt const&, Fsm& fsm, SrcT&, TarT& ) {
            return fsm.set_key_attempts < fsm.ctx->slac_config.set_key_max_attempts;
        }
    };
    struct has_no_set_key_attempts_left {
        template <class Fsm, class Evt, class SrcT, class TarT>
        bool operator()(Evt const&, Fsm& fsm, SrcT&, TarT& ) {
            return fsm.set_key_attempts >= fsm.ctx->slac_config.set_key_max_attempts;
        }
    };
    struct is_set_key_cnf_success {
        template <class Fsm, class Evt, class SrcT, class TarT>
        bool operator()(Evt const& e, Fsm& fsm, SrcT& src, TarT& tar) {
            if (not has_set_key_cnf_payload{}(e, fsm, src, tar)) {
                return false;
            }

            const auto msg = e.payload.template payload_as<messages::cm_set_key_cnf>();
            return accepts_set_key_cnf_success_result(fsm.ctx->slac_config.set_key_cnf_success_mode, msg->result);
        }
    };
    struct is_set_key_cnf_failed {
        template <class Fsm, class Evt, class SrcT, class TarT>
        bool operator()(Evt const& e, Fsm& fsm, SrcT& src, TarT& tar) {
            if (not has_set_key_cnf_payload{}(e, fsm, src, tar)) {
                return false;
            }

            const auto msg = e.payload.template payload_as<messages::cm_set_key_cnf>();
            return not accepts_set_key_cnf_success_result(fsm.ctx->slac_config.set_key_cnf_success_mode, msg->result);
        }
    };
    struct is_reset_chip_on {
        template <class Fsm, class Evt, class SrcT, class TarT>
        bool operator()(Evt const&, Fsm& fsm, SrcT&, TarT& ) {
            return fsm.ctx->slac_config.chip_reset.enabled;
        }
    };
    //Actions
    struct send_set_key_req {
        template <class Fsm>
        static void send(Fsm& fsm) {
            Nmk const& nmk = (fsm.ctx->slac_config.set_key_handling_mode == fsm::evse::SetKeyHandlingMode::retry_confirmed)
                                 ? fsm.pending_nmk
                                 : fsm.ctx->slac_config.session_nmk;
            fsm.ctx->log_info("Using SLAC session NMK " + format_session_nmk_for_log(nmk));
            auto msg = everest::lib::slac::fsm::evse::MatchingSessionData::create_cm_set_key_req(nmk);
            fsm.set_key_timer.setDuration(std::chrono::milliseconds(fsm.ctx->slac_config.set_key_timeout_ms));
            fsm.set_key_timer.reset();
            if (not fsm.ctx->send_slac_message(fsm.ctx->slac_config.plc_peer_mac, msg)) {
                fsm.ctx->log_warn("Failed to send CM_SET_KEY.REQ");
            }
        }

        template <class Evt, class Fsm, class SrcT, class TarT>
        void operator()(Evt const&, Fsm& fsm, SrcT&, TarT&) {
            send(fsm);
        }
    };
    struct retry_send_set_key_req {
        template <class Fsm, class SrcT, class TarT>
        void operator()(none const&, Fsm& fsm, SrcT&, TarT&) {
            fsm.set_key_attempts++;
            fsm.ctx->log_warn("Retrying CM_SET_KEY.REQ due to timeout. Attempt " +
                              std::to_string(fsm.set_key_attempts) + " of " +
                              std::to_string(fsm.ctx->slac_config.set_key_max_attempts));
            send_set_key_req::send(fsm);
        }
    };
    struct fail_send_set_key_req {
        template <class Fsm, class SrcT, class TarT>
        void operator()(none const&, Fsm& fsm, SrcT&, TarT&) {
            fsm.ctx->log_error("CM_SET_KEY timeout without valid CM_SET_KEY.CNF after " +
                               std::to_string(fsm.ctx->slac_config.set_key_max_attempts) +
                               " attempts; continuing to reset/idle path");
        }
    };
    struct note_set_key_failed {
        template <class Evt, class Fsm, class SrcT, class TarT>
        void operator()(Evt const& e, Fsm& fsm, SrcT&, TarT&) {
            const auto reply = e.payload.template payload_as<messages::cm_set_key_cnf>();
            if (not reply.has_value()) {
                return;
            }
            fsm.ctx->log_warn("CM_SET_KEY.CNF indicates failure with result=" +
                              std::to_string(reply->result) +
                              " on attempt " + std::to_string(fsm.set_key_attempts) +
                              "; retrying after timeout (max " +
                              std::to_string(fsm.ctx->slac_config.set_key_max_attempts) + ")");
        }
    };
    struct give_up_set_key_failed {
        template <class Evt, class Fsm, class SrcT, class TarT>
        void operator()(Evt const& e, Fsm& fsm, SrcT&, TarT&) {
            const auto reply = e.payload.template payload_as<messages::cm_set_key_cnf>();
            if (not reply.has_value()) {
                return;
            }
            fsm.ctx->log_error("CM_SET_KEY.CNF indicates failure with result=" +
                               std::to_string(reply->result) +
                               " after maximum attempts; continuing to reset/idle path");
        }
    };
    struct apply_set_key_cnf {
        template <class Evt, class Fsm, class SrcT, class TarT>
        void operator()(Evt const&, Fsm& fsm, SrcT&, TarT&) {
            fsm.ctx->slac_config.session_nmk = fsm.pending_nmk;
            fsm.ctx->log_info("CM_SET_KEY.CNF success, NMK set on modem");
            fsm.ctx->status.modem_NMK = true;
        }
    };

    // Transition guards
    using timeout_retry            = And_<set_key_timeout, And_<is_retry_confirmed_set_key, has_set_key_attempts_left>>;
    using timeout_give_up          = And_<set_key_timeout, And_<is_retry_confirmed_set_key, has_no_set_key_attempts_left>>;
    using set_key_ok               = And_<msg_expected, is_set_key_cnf_success>;
    using set_key_failed           = And_<msg_expected, is_set_key_cnf_failed>;
    using set_key_failed_retry     = And_<set_key_failed, And_<is_retry_confirmed_set_key, has_set_key_attempts_left>>;
    using set_key_failed_give_up   = And_<set_key_failed, And_<is_retry_confirmed_set_key, has_no_set_key_attempts_left>>;
    using no_reset_chip            = Not_<is_reset_chip_on>;

    // Transitions
    using initial_state = Init;
    struct transition_table : boost::mpl::vector<
        //  +----------+---------+-----------+------------------------+--------------------------+
        //  | Source   | Event   | Target    | Action                 | Guard                    |
        //  +----------+---------+-----------+------------------------+--------------------------+
        Row < Init     , none    , MsgSent   , send_set_key_req       , none                     >,
        Row < MsgSent  , update  , MsgSent   , retry_send_set_key_req , timeout_retry            >,
        Row < MsgSent  , update  , MsgValid  , fail_send_set_key_req  , timeout_give_up          >,
        Row < MsgSent  , message , MsgValid  , apply_set_key_cnf      , set_key_ok               >,
        Row < MsgSent  , message , MsgSent   , note_set_key_failed    , set_key_failed_retry     >,
        Row < MsgSent  , message , MsgValid  , give_up_set_key_failed , set_key_failed_give_up   >,
        Row < MsgValid , update  , ResetChip , none                   , is_reset_chip_on         >,
        Row < MsgValid , update  , Idle      , none                   , no_reset_chip            >
        //  +----------+---------+-----------+------------------------+--------------------------+
        > {};
    template <class FSM,class Event>
    void no_transition(Event const&, FSM&, int) { }

    // Members
    template <class Event, class Fsm>
    void on_entry(Event const&, Fsm& fsm) {
        ctx = fsm.ctx;
        if (fsm.ctx->slac_config.regenerate_key_on_reset){
            if (fsm.ctx->slac_config.set_key_handling_mode == fsm::evse::SetKeyHandlingMode::retry_confirmed) {
                fsm.ctx->slac_config.generate_nmk(this->pending_nmk);
            } else {
                fsm.ctx->slac_config.generate_nmk(fsm.ctx->slac_config.session_nmk);
            }
        } else {
            this->pending_nmk = fsm.ctx->slac_config.session_nmk;
        }
        if (fsm.ctx->slac_config.set_key_handling_mode == fsm::evse::SetKeyHandlingMode::legacy_single_attempt) {
            this->pending_nmk = fsm.ctx->slac_config.session_nmk;
        }
        this->set_key_attempts = 1;
        ctx->log_info("Entered Reset state");
        ctx->clear_match_confirm_cache();
        ctx->status.match_state = SlacState::Reset;
        ctx->status.d3_state = D3State::Unmatched;
        ctx->status.modem_NMK = false;
    }

    fsm::evse::Context* ctx;
    Nmk pending_nmk{};
    int set_key_attempts{0};
    timer set_key_timer;

    bool set_key_timeout() {
        return set_key_timer.timeout();
    }
    bool state_timeout() {
        return set_key_timeout();
    }
};
struct ResetChip_def   : public state_machine_def<ResetChip_def> {
    // States
    struct Delay     : public state<> {
        template <class Event, class Fsm>
        void on_entry(Event const&, Fsm& fsm) {
            to.setDuration(std::chrono::milliseconds(fsm.ctx->slac_config.chip_reset.delay_ms));
            to.reset();
        }

        timer to;
        bool state_timeout() {
            return to.timeout();
        }
    };
    struct Sent      : public state<> { };
    struct Received  : public state<> { };
    struct Done      : public exit_pseudo_state<update> { };

    // Guards
    bool is_done(update const&){
        // Qualcomm sends a reply
        // CG5317 does not reply to the reset packet
        // Chip reset not supported for other chips
        return ctx->modem_vendor == defs::ModemVendor::Lumissil;
    }
    struct is_reset_message {
        template <class Fsm, class SrcT, class TarT>
        bool operator()(message const& e, Fsm&, SrcT&, TarT& ) {
            const auto mmtype = e.payload.get_mmtype();
            auto expected = defs::qualcomm::MMTYPE_CM_RESET_DEVICE | defs::MMTYPE_MODE_CNF;
            return mmtype == expected;
        }
    };

    // Actions
    struct send_message {
        template <class Fsm, class Evt, class SrcT, class TarT>
        void operator()(Evt const&, Fsm& fsm, SrcT&, TarT& ) {
            auto& ctx = *fsm.ctx;
            if (ctx.modem_vendor == defs::ModemVendor::Qualcomm) {
                messages::qualcomm::cm_reset_device_req reset_req{};
                if (not ctx.send_slac_message(ctx.slac_config.plc_peer_mac, reset_req)) {
                    ctx.log_warn("Failed to send CM_RESET_DEVICE.REQ");
                }
            } else if (ctx.modem_vendor == defs::ModemVendor::Lumissil) {
                messages::lumissil::nscm_reset_device_req reset_req{};
                if (not ctx.send_slac_message(ctx.slac_config.plc_peer_mac, reset_req)) {
                    ctx.log_warn("Failed to send NSCM_RESET_DEVICE.REQ");
                }
            }
        }
    };

    // Transitions
    using initial_state = Delay;
    using p = ResetChip_def;
    struct transition_table : boost::mpl::vector<
        //    +----------+---------+----------+----------------+------------------+
        //    | Source   | Event   | Target   | Action         | Guard            |
        //    +----------+---------+----------+----------------+------------------+
        Row   < Delay    , update  , Sent     , send_message   , timeout          >,
        Row   < Sent     , message , Received , trigger_update , is_reset_message >,
        g_row < Sent     , update  , Done     /* none */       , &p::is_done      >,
        _row  < Received , update  , Done     /* none */         /* none */       >
        //    +----------+---------+----------+----------------+------------------+
        >{};
    template <class FSM,class Event>
    void no_transition(Event const&, FSM&, int) { }

    //Members
    template <class Event, class Fsm>
    void on_entry(Event const&, Fsm& fsm) {
        ctx = fsm.ctx;
        ctx->status.match_state = SlacState::ResetChip;
        ctx->status.d3_state = D3State::Unmatched;
    }

    fsm::evse::Context* ctx;
};
struct Matched_def     : public state_machine_def<Matched_def> {
    // States
    struct Init          : public state<> { };
    struct NoDetect      : public state<> { };
    struct Other         : public state<> { };
    struct Failed        : public exit_pseudo_state<message> { };

    // Guards
    struct detect_link {
        template <class Fsm, class Evt, class SrcT, class TarT>
        bool operator()(Evt const&, Fsm& fsm, SrcT&, TarT& ) {
            return fsm.ctx->slac_config.link_status.do_detect;
        }
    };

    // A valid CM_AMP_MAP.REQ (am_len != 0) received while matched. am_len == 0 is
    // invalid and must be left unanswered (ISO 15118-5 CmAmpMap_005).
    struct is_amp_map_req {
        template <class Fsm, class SrcT, class TarT>
        bool operator()(message const& e, Fsm&, SrcT&, TarT&) {
            if (e.payload.get_mmtype() != (defs::MMTYPE_CM_AMP_MAP | defs::MMTYPE_MODE_REQ)) {
                return false;
            }
            auto const req = e.payload.payload_as<messages::cm_amp_map_req>();
            return req.has_value() and req->am_len != 0;
        }
    };

    // Answer a valid CM_AMP_MAP.REQ with CM_AMP_MAP.CNF(result=0x00). On real HW
    // the requested transmit-power reduction is applied to the modem; that RF
    // effect is out of scope for the SLAC stack.
    struct send_amp_map_cnf {
        template <class Fsm, class SrcT, class TarT>
        void operator()(message const& e, Fsm& fsm, SrcT&, TarT&) {
            messages::cm_amp_map_cnf reply{};
            reply.result = defs::CM_AMP_MAP_CNF_RESULT_SUCCESS;
            if (not fsm.ctx->send_slac_message(e.payload.get_src_mac(), reply)) {
                fsm.ctx->log_warn("Failed to send CM_AMP_MAP.CNF");
            }
        }
    };

    // SECC-initiated CM_AMP_MAP.REQ (ISO 15118-3 A.9.6, PICS InitiateCmAmpMap): after sending the
    // first REQ (on_entry) the SECC retransmits it every TT_match_response until a CM_AMP_MAP.CNF with
    // result=0x00 arrives, limited to C_EV_match_retry retransmissions (CmAmpMap_003/004).
    struct amp_map_retransmit_due {
        template <class Fsm, class Evt, class SrcT, class TarT>
        bool operator()(Evt const&, Fsm& fsm, SrcT&, TarT&) {
            return fsm.amp_map_awaiting_cnf and fsm.amp_map_timer.timeout();
        }
    };
    struct retransmit_amp_map {
        template <class Fsm, class Evt, class SrcT, class TarT>
        void operator()(Evt const&, Fsm& fsm, SrcT&, TarT&) {
            if (fsm.amp_map_retries < slac::defs::C_EV_MATCH_RETRY) {
                fsm.amp_map_retries++;
                fsm.ctx->send_amp_map_req(fsm.ctx->status.ev_mac, fsm.ctx->slac_config.amp_map_len,
                                          fsm.ctx->slac_config.amp_map_data);
                fsm.amp_map_timer.reset();
            } else {
                fsm.amp_map_awaiting_cnf = false; // retry limit reached, stop
            }
        }
    };
    // A CM_AMP_MAP.CNF with result=0x00 confirms the exchange and stops retransmission; any other
    // result is ignored ([V2G3-A09-114], CmAmpMap_004 keeps retransmitting on an invalid CNF).
    struct is_amp_map_cnf_ok {
        template <class Fsm, class SrcT, class TarT>
        bool operator()(message const& e, Fsm& fsm, SrcT&, TarT&) {
            if (not fsm.amp_map_awaiting_cnf or
                e.payload.get_mmtype() != (defs::MMTYPE_CM_AMP_MAP | defs::MMTYPE_MODE_CNF)) {
                return false;
            }
            auto const cnf = e.payload.payload_as<messages::cm_amp_map_cnf>();
            return cnf.has_value() and cnf->result == defs::CM_AMP_MAP_CNF_RESULT_SUCCESS;
        }
    };
    struct amp_map_cnf_ack {
        template <class Fsm, class SrcT, class TarT>
        void operator()(message const&, Fsm& fsm, SrcT&, TarT&) {
            fsm.amp_map_awaiting_cnf = false;
        }
    };

    // Transitions
    using initial_state = Init;
    using p = ResetChip_def;
    using link_detection_off = Not_<detect_link>;
    struct transition_table : boost::mpl::vector<
        //    +----------+---------+----------+--------------------+--------------------------------+
        //    | Source   | Event   | Target   | Action             | Guard                          |
        //    +----------+---------+----------+--------------------+--------------------------------+
        Row   < Init     , none    , Lumissil , link_status_req    , And_<detect_link, is_lumissil> >,
        Row   < Init     , none    , Qualcomm , link_status_req    , And_<detect_link, is_qualcomm> >,
        Row   < Init     , none    , NoDetect , none               , link_detection_off             >,
        //    +----------+---------+----------+--------------------+--------------------------------+
        Row   < Lumissil , update  , Lumissil , link_status_req    , timeout                        >,
        Row   < Lumissil , update  , Lumissil , retransmit_amp_map , amp_map_retransmit_due         >,
        Row   < Lumissil , message , Failed   , none               , link_status_neg                >,
        Row   < Lumissil , message , Lumissil , send_amp_map_cnf   , is_amp_map_req                 >,
        Row   < Lumissil , message , Lumissil , amp_map_cnf_ack    , is_amp_map_cnf_ok              >,
        //    +----------+---------+----------+--------------------+--------------------------------+
        Row   < Qualcomm , update  , Qualcomm , link_status_req    , timeout                        >,
        Row   < Qualcomm , update  , Qualcomm , retransmit_amp_map , amp_map_retransmit_due         >,
        Row   < Qualcomm , message , Failed   , none               , link_status_neg                >,
        Row   < Qualcomm , message , Qualcomm , send_amp_map_cnf   , is_amp_map_req                 >,
        Row   < Qualcomm , message , Qualcomm , amp_map_cnf_ack    , is_amp_map_cnf_ok              >,
        //    +----------+---------+----------+--------------------+--------------------------------+
        Row   < NoDetect , message , NoDetect , send_amp_map_cnf   , is_amp_map_req                 >,
        Row   < NoDetect , update  , NoDetect , retransmit_amp_map , amp_map_retransmit_due         >,
        Row   < NoDetect , message , NoDetect , amp_map_cnf_ack    , is_amp_map_cnf_ok              >,
        Row   < Other    , message , Other    , send_amp_map_cnf   , is_amp_map_req                 >,
        Row   < Init     , message , Init     , send_amp_map_cnf   , is_amp_map_req                 >
        //    +----------+---------+----------+--------------------+--------------------------------+
        >{};

    template <class FSM,class Event>
    void no_transition(Event const&, FSM&, int) { }

    // members;
    template <class Event, class Fsm>
    void on_entry(Event const&, Fsm& fsm) {
        ctx = fsm.ctx;
        {
            std::ostringstream ss;
            ss << "Entered Matched state (EV " << format_mac_for_log(ctx->status.ev_mac)
               << ", avg. attenuation " << std::fixed << std::setprecision(1)
               << ctx->status.average_attenuation << " dB)";
            ctx->log_info(ss.str());
        }
        ctx->clear_match_confirm_cache();
        ctx->signal_dlink_ready(true);
        link_check_to_ms = ctx->slac_config.link_status.poll_in_matched_state_ms;
        ctx->status.match_state = SlacState::Matched;
        ctx->status.d3_state = D3State::Matched;
        ctx->status.modem_link_ready = true;

        // ISO 15118-3 A.9.6 transmit-power limitation: once the AVLN is up, send
        // the operator-configured amplitude map to the peer (CmAmpMap_002..004).
        // Disabled by default; the map is provided via the amp_map_file config.
        amp_map_awaiting_cnf = false;
        amp_map_retries = 0;
        if (ctx->slac_config.initiate_amp_map and ctx->slac_config.amp_map_len > 0) {
            if (not ctx->send_amp_map_req(ctx->status.ev_mac, ctx->slac_config.amp_map_len,
                                          ctx->slac_config.amp_map_data)) {
                ctx->log_warn("Failed to send CM_AMP_MAP.REQ");
            }
            // Await the CM_AMP_MAP.CNF; retransmit every TT_match_response until it arrives, limited
            // to C_EV_match_retry retransmissions (serviced by retransmit_amp_map on the update tick).
            amp_map_awaiting_cnf = true;
            amp_map_timer.setDurationMilliSeconds(defs::TT_MATCH_RESPONSE_MS);
            amp_map_timer.reset();
        }
    }

    template <class Event, class Fsm>
    void on_exit(Event const&, Fsm&) {
        ctx->signal_dlink_ready(false);
        ctx->status.ev_mac.fill(0);
        ctx->status.average_attenuation = 0.f;
        ctx->status.modem_link_ready = false;
        ctx->clear_match_confirm_cache();
    }

    fsm::evse::Context* ctx;
    int link_check_to_ms{0};
    // SECC-initiated CM_AMP_MAP retransmission state (see the amp_map_* guards/actions above).
    bool amp_map_awaiting_cnf{false};
    int amp_map_retries{0};
    timer amp_map_timer;
};
struct WaitForLink_def : public state_machine_def<WaitForLink_def> {
    // States
    struct Init          : public state<> { };
    struct NoDetect      : public state<> { };
    struct Failed        : public exit_pseudo_state<none> { };
    struct Matched       : public exit_pseudo_state<message> { };

    // Guards
    struct is_match_req {
        template <class Fsm, class Evt, class SrcT, class TarT>
        bool operator()(Evt const& e, Fsm& fsm, SrcT&, TarT&) {
            if (e.payload.get_mmtype() != (defs::MMTYPE_CM_SLAC_MATCH | defs::MMTYPE_MODE_REQ)) {
                return false;
            }
            auto const msg = e.payload.template payload_as<slac::messages::cm_slac_match_req>();
            if (not msg.has_value()) {
                return false;
            }
            if (not fsm.ctx->match_confirm_cache.valid) {
                return false;
            }
            auto const source_mac = e.payload.get_src_mac();
            if (source_mac == nullptr) {
                return false;
            }
            if (not wire_pointer_equal(source_mac, fsm.ctx->match_confirm_cache.ev_mac)) {
                return false;
            }
            fsm::evse::MatchingSessionData data(fsm.ctx->match_confirm_cache.ev_mac,
                                                fsm.ctx->match_confirm_cache.run_id,
                                                fsm.ctx->match_confirm_cache.evse_mac);
            return data.validate_message(*msg);
        }
    };

    //Actions
    struct send_match_cnf {
        template <class Fsm, class Evt, class SrcT, class TarT>
        void operator()(Evt const&, Fsm& fsm, SrcT&, TarT& ) {
            auto& ctx = *fsm.ctx;
            if (not ctx.match_confirm_cache.valid) {
                return;
            }
            if (not ctx.send_slac_message(ctx.match_confirm_cache.ev_mac, ctx.match_confirm_cache.message)) {
                ctx.log_warn("Failed to send cached CM_SLAC_MATCH.CNF");
            }
        }
    };

    // Transitions
    using initial_state = Init;
    using p = ResetChip_def;
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

    template <class FSM,class Event>
    void no_transition(Event const&, FSM&, int) { }

    // Members
    template <class Event, class Fsm>
    void on_entry(Event const&, Fsm& fsm) {
        ctx = fsm.ctx;
        ctx->log_info("Waiting for Link to be ready...");
        link_check_to_ms = ctx->slac_config.link_status.retry_ms;
        to.setDuration(std::chrono::milliseconds(ctx->slac_config.link_status.timeout_ms));
        to.reset();
        ctx->status.match_state = SlacState::WaitForLink;
        // Still in the matching phase (post CM_SLAC_MATCH.CNF, awaiting link) -> published as MATCHING.
        ctx->status.d3_state = D3State::Matching;
    }


    fsm::evse::Context* ctx;
    int link_check_to_ms{0};
    timer to;
    bool state_timeout(){
        return to.timeout();
    }
};
struct Init_def        : public state_machine_def<Init_def> {
    // States
    struct Init       : timeout_state {
        template <class Event, class Fsm> void on_entry(Event const& e, Fsm& fsm) {
            timeout_state::state_timeout_ms = fsm.ctx->slac_config.request_info_delay_ms;
            timeout_state::on_entry(e, fsm);
        }
    };
    struct OpAttr     : timeout_state {
        template <class Event, class Fsm> void on_entry(Event const& e, Fsm& fsm) {
            timeout_state::state_timeout_ms = fsm.ctx->slac_config.request_info_delay_ms;
            timeout_state::on_entry(e, fsm);
        }
    };
    struct GetVersion : timeout_state {
        template <class Event, class Fsm> void on_entry(Event const& e, Fsm& fsm) {
            timeout_state::state_timeout_ms = fsm.ctx->slac_config.request_info_delay_ms;
            timeout_state::on_entry(e, fsm);
        }
    };
    struct Done       : exit_pseudo_state<update> { };
    struct Other      : state<> { };
    struct Lumissil   : public state<> {
        static std::string device_info(message const& e) {
            auto msg = e.payload.payload_as<messages::lumissil::nscm_get_version_cnf>();
            return msg ? utils::device_info(*msg) : std::string{};
        }
        static auto constexpr modem_vendor = defs::ModemVendor::Lumissil;
        static auto constexpr msg_type = defs::lumissil::MMTYPE_NSCM_GET_VERSION | defs::MMTYPE_MODE_CNF;
    };
    struct Qualcomm   : public state<> {
        static std::string device_info(message const& e) {
            auto msg = e.payload.payload_as<messages::qualcomm::op_attr_cnf>();
            return msg ? utils::device_info(*msg) : std::string{};
        }
        static auto constexpr modem_vendor = defs::ModemVendor::Qualcomm;
        static auto constexpr msg_type = defs::qualcomm::MMTYPE_OP_ATTR | defs::MMTYPE_MODE_CNF;
    };

    // Guards
    struct is_qualcomm_msg : public is_message_of_type<Qualcomm::msg_type>{ };
    struct is_lumissil_msg : public is_message_of_type<Lumissil::msg_type>{ };

    // Actions
    struct op_attr_req : public send_default_msg<messages::qualcomm::op_attr_req> { };
    struct get_version_req : public send_default_msg<messages::lumissil::nscm_get_version_req> { };
    struct set_modem_vendor {
        template <class Fsm, class SrcT, class TarT>
        void operator()(message const& e, Fsm& fsm, SrcT&, TarT&) {
            fsm.ctx->modem_vendor = TarT::modem_vendor;
            fsm.ctx->log_info(TarT::device_info(e));
        }

    };

    // Transitions
    using initial_state = boost::mpl::vector<Init, Other>;
    struct transition_table : boost::mpl::vector<
        //    +------------+---------+------------+------------------+-----------------+
        //    | Source     | Event   | Target     | Action           | Guard           |
        //    +------------+---------+------------+------------------+-----------------+
        Row   < Init       , update  , OpAttr     , op_attr_req      , timeout         >,
        Row   < OpAttr     , update  , GetVersion , get_version_req  , timeout         >,
        Row   < GetVersion , update  , Done       , none             , timeout         >,
        //    +------------+---------+------------+------------------+-----------------+
        Row   < Other      , message , Lumissil   , set_modem_vendor , is_lumissil_msg >,
        Row   < Other      , message , Qualcomm   , set_modem_vendor , is_qualcomm_msg >
        //    +------------+---------+------------+------------------+-----------------+
        >{};
    template <class FSM,class Event>
    void no_transition(Event const&, FSM&, int) { }

    // Members
    template <class Event, class Fsm>
    void on_entry(Event const&, Fsm& fsm) {
        ctx = fsm.ctx;
        ctx->status.match_state = SlacState::Init;
        ctx->status.d3_state = D3State::Unmatched;
    }

    fsm::evse::Context* ctx;
};



// Main StateMachine
struct SlacFSM_def : state_machine_def<SlacFSM_def> {
    // States
    using Init = state_machine<Init_def>;
    using Init_Done = Init::exit_pt<Init_def::Done>;

    using Reset = state_machine<Reset_def>;
    using Reset_ResetChip = Reset::exit_pt<Reset_def::ResetChip>;
    using Reset_Idle = Reset::exit_pt<Reset_def::Idle>;

    using ResetChip = state_machine<ResetChip_def>;
    using ResetChip_Done = ResetChip::exit_pt<ResetChip_def::Done>;

    using Matching = state_machine<Matching_def>;
    using Matching_Fail = Matching::exit_pt<Matching_def::Failed>;
    using Matching_Match = Matching::exit_pt<Matching_def::Matched>;

    using Matched = state_machine<Matched_def>;
    using Matched_Fail = Matched::exit_pt<Matched_def::Failed>;

    using WaitForLink = state_machine<WaitForLink_def>;
    using WaitForLink_Fail = WaitForLink::exit_pt<WaitForLink_def::Failed>;
    using WaitForLink_Match = WaitForLink::exit_pt<WaitForLink_def::Matched>;

    struct Idle   : public state<> {
        template <class Event, class Fsm>
        void on_entry(Event const&, Fsm& fsm) {
            fsm.ctx->log_info("Entered Idle state");
            fsm.ctx->clear_match_confirm_cache();
            fsm.ctx->status.match_state = SlacState::Idle;
            fsm.ctx->status.d3_state = D3State::Unmatched;
            fsm.ctx->status.modem_PIB = true;
        }
    };
    struct Failed : public state<> {
        template <class Event, class Fsm>
        void on_entry(Event const&, Fsm& fsm) {
            auto& ctx = *fsm.ctx;
            ctx.log_info("Entered Failed state");
            if (ctx.slac_config.ac_mode_five_percent) {
                ctx.signal_error_routine_request();
            }
            ctx.clear_match_confirm_cache();
            ctx.status.match_state = SlacState::Failed;
            ctx.status.d3_state = D3State::Unmatched;
        }
    };

    // Guards
    struct cfg_wait_for_link {
        template <class Fsm, class Evt, class SrcT, class TarT>
        bool operator()(Evt const&, Fsm& fsm, SrcT&, TarT& ) {
            return fsm.ctx->slac_config.link_status.do_detect;
        }
    };
    struct is_legacy_set_key_handling_mode {
        template <class Fsm, class Evt, class SrcT, class TarT>
        bool operator()(Evt const&, Fsm& fsm, SrcT&, TarT& ) {
            return fsm.ctx->slac_config.set_key_handling_mode == fsm::evse::SetKeyHandlingMode::legacy_single_attempt;
        }
    };

    // Actions
    struct on_matched_fail {
        template <class Fsm, class Evt, class SrcT, class TarT>
        void operator()(Evt const&, Fsm& fsm, SrcT&, TarT& ) {
            auto& ctx = *fsm.ctx;
            ctx.log_error("Connection lost in matched state");
            ctx.signal_error_routine_request();
        }
    };

    // Transitions
    using initial_state = Init;
    using reset_timeout = And_<timeout, is_legacy_set_key_handling_mode>;
    using no_link_wait = Not_<cfg_wait_for_link>;
    struct transition_table : boost::mpl::vector<
        //  +-------------------+-----------+-------------+-----------------+-------------------+
        //  | Source            | Event     | Target      | Action          | Guard             |
        //  +-------------------+-----------+-------------+-----------------+-------------------+
        Row < Init_Done         , update    , Reset       , none            , none              >,
        //  +-------------------+-----------+-------------+-----------------+-------------------+
        Row < Reset             , reset     , Reset       , none            , none              >,
        Row < Reset             , update    , Failed      , none            , reset_timeout     >,
        Row < Reset_ResetChip   , update    , ResetChip   , none            , none              >,
        Row < Reset_Idle        , update    , Idle        , none            , none              >,
        //  +-------------------+-----------+-------------+-----------------+-------------------+
        Row < ResetChip_Done    , update    , Idle        , none            , none              >,
        //  +-------------------+-----------+-------------+-----------------+-------------------+
        Row < Idle              , enter_bcd , Matching    , none            , none              >,
        Row < Idle              , reset     , Reset       , none            , none              >,
        //  +-------------------+-----------+-------------+-----------------+-------------------+
        Row < Matching          , reset     , Reset       , none            , none              >,
        Row < Matching          , leave_bcd , Idle        , none            , none              >,
        Row < Matching_Fail     , none      , Failed      , none            , none              >,
        Row < Matching_Match    , none      , WaitForLink , none            , cfg_wait_for_link >,
        Row < Matching_Match    , none      , Matched     , none            , no_link_wait      >,
        //  +-------------------+-----------+-------------+-----------------+-------------------+
        Row < WaitForLink       , update    , Failed      , none            , timeout           >,
        Row < WaitForLink       , reset     , Reset       , none            , none              >,
        Row < WaitForLink       , leave_bcd , Reset       , none            , none              >,
        Row < WaitForLink_Fail  , none      , Failed      , none            , none              >,
        Row < WaitForLink_Match , message   , Matched     , none            , none              >,
        //  +-------------------+-----------+-------------+-----------------+-------------------+
        Row < Matched           , reset     , Reset       , none            , none              >,
        Row < Matched           , leave_bcd , Reset       , none            , none              >,
        Row < Matched_Fail      , message   , Reset       , on_matched_fail , none              >,
        //  +-------------------+-----------+-------------+-----------------+-------------------+
        Row < Failed            , reset     , Reset       , none            , none              >,
        Row < Failed            , leave_bcd , Reset       , none            , none              >
        //  +-------------------+-----------+-------------+-----------------+-------------------+
        > {};
    template <class FSM,class Event>
    void no_transition(Event const&, FSM&, int) {
    }

    fsm::evse::Context* ctx;

    SlacFSM_def(fsm::evse::Context& ctx_) : ctx(&ctx_) {
    }
};

using SlacFSM = state_machine<SlacFSM_def>;

// clang-format on

} // namespace everest::lib::slac::msm
