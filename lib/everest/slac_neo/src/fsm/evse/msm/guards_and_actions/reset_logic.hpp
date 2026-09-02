// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 - 2026 Pionix GmbH and Contributors to EVerest

// Guards and actions of the Reset sub-machine (CM_SET_KEY handling; see reset.hpp).

#pragma once
#include "../common.hpp"

namespace everest::lib::slac::msm::reset_sm {

// Entry helpers
// Decide which NMK the upcoming CM_SET_KEY.REQ carries. With regenerate_key_on_reset a fresh key is
// generated: into pending_nmk in retry_confirmed mode (it becomes the session key only once the
// modem confirmed it, see apply_set_key_cnf) or straight into the session key otherwise. Without
// regeneration the current session key is re-sent. legacy_single_attempt always sends the session
// key. Also arms the first attempt.
template <class ResetSm> void select_nmk_for_reset(ResetSm& sm) {
    auto& config = sm.ctx->slac_config;
    if (config.regenerate_key_on_reset) {
        if (config.set_key_handling_mode == fsm::evse::SetKeyHandlingMode::retry_confirmed) {
            config.generate_nmk(sm.pending_nmk);
        } else {
            config.generate_nmk(config.session_nmk);
        }
    } else {
        sm.pending_nmk = config.session_nmk;
    }
    if (config.set_key_handling_mode == fsm::evse::SetKeyHandlingMode::legacy_single_attempt) {
        sm.pending_nmk = config.session_nmk;
    }
    sm.set_key_attempts = 1;
}

// Guards
struct msg_expected : public is_message_of_type<defs::MMTYPE_CM_SET_KEY | defs::MMTYPE_MODE_CNF> {};
struct is_retry_confirmed_set_key {
    template <class Fsm, class Evt, class SrcT, class TarT> bool operator()(Evt const&, Fsm& fsm, SrcT&, TarT&) {
        return fsm.ctx->slac_config.set_key_handling_mode == fsm::evse::SetKeyHandlingMode::retry_confirmed;
    }
};
struct set_key_timeout {
    template <class Evt, class Fsm, class SrcT, class TarT> bool operator()(Evt const&, Fsm& fsm, SrcT&, TarT&) {
        return fsm.set_key_timer_expired();
    }
};
struct has_set_key_cnf_payload {
    template <class Evt, class Fsm, class SrcT, class TarT> bool operator()(Evt const& e, Fsm&, SrcT&, TarT&) {
        const auto msg = e.payload.template payload_as<messages::cm_set_key_cnf>();
        return msg.has_value();
    }
};
struct has_set_key_attempts_left {
    template <class Fsm, class Evt, class SrcT, class TarT> bool operator()(Evt const&, Fsm& fsm, SrcT&, TarT&) {
        return fsm.set_key_attempts < fsm.ctx->slac_config.set_key_max_attempts;
    }
};
struct has_no_set_key_attempts_left {
    template <class Fsm, class Evt, class SrcT, class TarT> bool operator()(Evt const&, Fsm& fsm, SrcT&, TarT&) {
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
    template <class Fsm, class Evt, class SrcT, class TarT> bool operator()(Evt const&, Fsm& fsm, SrcT&, TarT&) {
        return fsm.ctx->slac_config.chip_reset.enabled;
    }
};

// Actions
struct send_set_key_req {
    template <class Fsm> static void send(Fsm& fsm) {
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

    template <class Evt, class Fsm, class SrcT, class TarT> void operator()(Evt const&, Fsm& fsm, SrcT&, TarT&) {
        send(fsm);
    }
};
struct retry_send_set_key_req {
    template <class Fsm, class SrcT, class TarT> void operator()(none const&, Fsm& fsm, SrcT&, TarT&) {
        fsm.set_key_attempts++;
        fsm.ctx->log_warn("Retrying CM_SET_KEY.REQ due to timeout. Attempt " + std::to_string(fsm.set_key_attempts) +
                          " of " + std::to_string(fsm.ctx->slac_config.set_key_max_attempts));
        send_set_key_req::send(fsm);
    }
};
struct fail_send_set_key_req {
    template <class Fsm, class SrcT, class TarT> void operator()(none const&, Fsm& fsm, SrcT&, TarT&) {
        fsm.ctx->log_error("CM_SET_KEY timeout without valid CM_SET_KEY.CNF after " +
                           std::to_string(fsm.ctx->slac_config.set_key_max_attempts) +
                           " attempts; continuing to reset/idle path");
    }
};
struct note_set_key_failed {
    template <class Evt, class Fsm, class SrcT, class TarT> void operator()(Evt const& e, Fsm& fsm, SrcT&, TarT&) {
        const auto reply = e.payload.template payload_as<messages::cm_set_key_cnf>();
        if (not reply.has_value()) {
            return;
        }
        fsm.ctx->log_warn("CM_SET_KEY.CNF indicates failure with result=" + std::to_string(reply->result) +
                          " on attempt " + std::to_string(fsm.set_key_attempts) + "; retrying after timeout (max " +
                          std::to_string(fsm.ctx->slac_config.set_key_max_attempts) + ")");
    }
};
struct give_up_set_key_failed {
    template <class Evt, class Fsm, class SrcT, class TarT> void operator()(Evt const& e, Fsm& fsm, SrcT&, TarT&) {
        const auto reply = e.payload.template payload_as<messages::cm_set_key_cnf>();
        if (not reply.has_value()) {
            return;
        }
        fsm.ctx->log_error("CM_SET_KEY.CNF indicates failure with result=" + std::to_string(reply->result) +
                           " after maximum attempts; continuing to reset/idle path");
    }
};
struct apply_set_key_cnf {
    template <class Evt, class Fsm, class SrcT, class TarT> void operator()(Evt const&, Fsm& fsm, SrcT&, TarT&) {
        fsm.ctx->slac_config.session_nmk = fsm.pending_nmk;
        fsm.ctx->log_info("CM_SET_KEY.CNF success, NMK set on modem");
        fsm.ctx->status.modem_NMK = true;
    }
};

} // namespace everest::lib::slac::msm::reset_sm
