// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 - 2026 Pionix GmbH and Contributors to EVerest

// Guards and actions of the Session sub-machine (one per CM_SLAC_PARM.REQ; see session.hpp).

#pragma once
#include "common.hpp"

namespace everest::lib::slac::msm::session_sm {

// Message checks: the frame has the expected MMTYPE, decodes as MsgT and belongs to this session
// (run id / EV MAC, see MatchingSessionData::validate_message).
template<class MsgT>
bool check_message(message const& e, std::uint16_t expected, fsm::evse::MatchingSessionData const& session_data) {
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
inline bool matches_start_atten_char(message const& e, fsm::evse::MatchingSessionData const& session_data) {
    auto mmtype = defs::MMTYPE_CM_START_ATTEN_CHAR | defs::MMTYPE_MODE_IND;
    return check_message<slac::messages::cm_start_atten_char_ind>(e, mmtype, session_data);
}
inline bool matches_atten_char_rsp(message const& e, fsm::evse::MatchingSessionData const& session_data) {
    auto mmtype = slac::defs::MMTYPE_CM_ATTEN_CHAR | slac::defs::MMTYPE_MODE_RSP;
    return check_message<slac::messages::cm_atten_char_rsp>(e, mmtype, session_data);
}
inline bool matches_slac_match_req(message const& e, fsm::evse::MatchingSessionData const& session_data) {
    auto mmtype = slac::defs::MMTYPE_CM_SLAC_MATCH | slac::defs::MMTYPE_MODE_REQ;
    return check_message<slac::messages::cm_slac_match_req>(e, mmtype, session_data);
}
inline bool matches_atten_profile_ind(message const& e, fsm::evse::MatchingSessionData const& session_data) {
    auto mmtype = slac::defs::MMTYPE_CM_ATTEN_PROFILE | slac::defs::MMTYPE_MODE_IND;
    return check_message<slac::messages::cm_atten_profile_ind>(e, mmtype, session_data);
}

// Guards
struct is_atten_char_rsp {
    template <class Fsm, class SrcT, class TarT>
    bool operator()(message const& e, Fsm& fsm, SrcT&, TarT&) {
        return matches_atten_char_rsp(e, fsm.session_data);
    }
};
struct is_slac_match_req {
    template <class Fsm, class SrcT, class TarT>
    bool operator()(message const& e, Fsm& fsm, SrcT&, TarT&) {
        return matches_slac_match_req(e, fsm.session_data);
    }
};
struct sound_below_limit {
    template <class Fsm, class SrcT, class TarT>
    bool operator()(message const& e, Fsm& fsm, SrcT&, TarT&) {
        return fsm.session_data.captured_sounds + 1 < slac::defs::CM_SLAC_PARM_CNF_NUM_SOUNDS and
               matches_atten_profile_ind(e, fsm.session_data);
    }
};
struct sound_completes_count {
    template <class Fsm, class SrcT, class TarT>
    bool operator()(message const& e, Fsm& fsm, SrcT&, TarT&) {
        return fsm.session_data.captured_sounds + 1 >= slac::defs::CM_SLAC_PARM_CNF_NUM_SOUNDS and
               matches_atten_profile_ind(e, fsm.session_data);
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
        return not src.state_timeout() and matches_start_atten_char(e, fsm.session_data);
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

struct on_atten_char_rsp {
    template <class Fsm, class SrcT, class TarT>
    void operator()(message const&, Fsm& fsm, SrcT&, TarT&) {
        fsm.ctx->log_info(session_log_prefix(fsm.session_data) +
                          "Received CM_ATTEN_CHAR.RSP, waiting for CM_SLAC_MATCH.REQ");
    }
};
struct match_cnf {
    template <class Fsm, class SrcT, class TarT>
    void operator()(message const& e, Fsm& fsm, SrcT&, TarT&) {
        auto& ctx = *fsm.ctx;
        auto& session_data = fsm.session_data;
        messages::cm_slac_match_cnf& reply = ctx.match_confirm_cache.message;
        auto const msg = e.payload.payload_as<slac::messages::cm_slac_match_req>();
        if (not msg.has_value()) {
            return;
        }
        ctx.log_info(session_log_prefix(session_data) +
                     "Received CM_SLAC_MATCH.REQ, sending CM_SLAC_MATCH.CNF -> session complete");
        static constexpr Nmk failed_match_session_nmk{0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                                                     0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10};

        Nmk const* session_nmk = &ctx.slac_config.session_nmk;
        if (ctx.slac_config.link_status.debug_simulate_failed_matching) {
            ctx.log_info("Sending wrong NMK to EV to simulate a failed link setup after match request");
            session_nmk = &failed_match_session_nmk;
        }

        session_data.create_cm_slac_match_cnf(reply, *msg, *session_nmk);
        if (not ctx.send_slac_message(session_data.ev_mac, reply)) {
            ctx.log_warn("Failed to send CM_SLAC_MATCH.CNF");
        }
        ctx.signal_cm_slac_match_cnf(session_data.ev_mac.data());
        ctx.cache_match_confirm_message(reply, session_data.ev_mac, session_data.evse_mac, session_data.run_id);
        std::copy(std::begin(session_data.ev_mac), std::end(session_data.ev_mac), std::begin(ctx.status.ev_mac));
    }
};

template <class Fsm> void send_atten_char(Fsm& fsm) {
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

template <char const* Reason> struct log_session_failed {
    template <class Fsm, class Evt, class SrcT, class TarT>
    void operator()(Evt const&, Fsm& fsm, SrcT&, TarT&) {
        fsm.ctx->log_info(session_log_prefix(fsm.session_data) + Reason);
    }
};
inline constexpr char const fail_no_start_atten[] =
    "Timeout waiting for CM_START_ATTEN_CHAR.IND, session failed";
inline constexpr char const fail_no_atten_rsp[] =
    "No CM_ATTEN_CHAR.RSP after all retries, session failed";
inline constexpr char const fail_no_slac_match[] =
    "Timeout waiting for CM_SLAC_MATCH.REQ, session failed";
inline constexpr char const fail_validation_window[] =
    "No CM_SLAC_MATCH.REQ within TT_match_sequence after CM_VALIDATE, session failed";

} // namespace everest::lib::slac::msm::session_sm
