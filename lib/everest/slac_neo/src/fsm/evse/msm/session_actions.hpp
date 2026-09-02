// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 - 2026 Pionix GmbH and Contributors to EVerest

// Guards and actions of the Session sub-machine (one per CM_SLAC_PARM.REQ; see session.hpp).

#pragma once
#include "common.hpp"

namespace everest::lib::slac::msm::session_sm {

// Guards
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
