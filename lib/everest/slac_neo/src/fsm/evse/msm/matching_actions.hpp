// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 - 2026 Pionix GmbH and Contributors to EVerest

// Guards and actions of the Matching sub-machine (see matching.hpp).

#pragma once
#include "common.hpp"

namespace everest::lib::slac::msm::matching_sm {

// Session bookkeeping shared by the guards below. A session flags SessionMatched in MatchComplete
// and SessionFailed in Failed (see session.hpp).
template <class Fsm> bool any_session_matched(Fsm& fsm) {
    for (auto& elem : fsm.sessions) {
        if (elem.template is_flag_active<SessionMatched>()) {
            return true;
        }
    }
    return false;
}
// False while there is no session at all: "all failed" needs at least one session.
template <class Fsm> bool all_sessions_failed(Fsm& fsm) {
    if (fsm.sessions.empty()) {
        return false;
    }
    for (auto& elem : fsm.sessions) {
        if (not elem.template is_flag_active<SessionFailed>()) {
            return false;
        }
    }
    return true;
}

// Guards
struct is_slac_param_req : public is_message_of_type<slac::defs::MMTYPE_CM_SLAC_PARAM | slac::defs::MMTYPE_MODE_REQ> {};
// A session in MatchComplete means CM_SLAC_MATCH.CNF is out, but Matching is only exited on the
// next update tick. In that window (and per ISO 15118-3 whenever an AVLN is up) a fresh
// CM_SLAC_PARM.REQ must get no CNF (TC_SECC_CMN_VTB_PLCLinkStatus_003) and must not restart the
// completed session.
struct has_matched_session {
    template <class Fsm, class Evt, class SrcT, class TarT> bool operator()(Evt const&, Fsm& fsm, SrcT&, TarT&) {
        return any_session_matched(fsm);
    }
};

// Actions
struct ignore_parm_req {
    template <class Fsm, class SrcT, class TarT> void operator()(message const&, Fsm& fsm, SrcT&, TarT&) {
        fsm.ctx->log_info("Ignoring CM_SLAC_PARM.REQ, match already completed");
    }
};
struct pipe_event {
    template <class Fsm, class Evt, class SrcT, class TarT> void operator()(Evt const& e, Fsm& fsm, SrcT&, TarT&) {
        for (auto& elem : fsm.sessions) {
            elem.process_event(e);
        }
    }
};

// CM_VALIDATE (ISO 15118-3 9.4) BCB-toggle validation lives in fsm::evse::ValidateHandler
// (src/fsm/evse/validate_handler.hpp); Matching only routes the REQ frames and the update tick to it.
struct handle_validate_req {
    template <class Fsm, class SrcT, class TarT> void operator()(message const& e, Fsm& fsm, SrcT&, TarT&) {
        const auto req = e.payload.payload_as<messages::cm_validate_req>();
        if (not req.has_value()) {
            return;
        }
        fsm.validate.handle_req(*req, e.payload.get_src_mac(), *fsm.ctx);
    }
};
// Serviced on every `update` tick while a validation is armed (guard validate_needs_service).
struct validate_tick {
    template <class Fsm, class Evt, class SrcT, class TarT> void operator()(Evt const&, Fsm& fsm, SrcT&, TarT&) {
        fsm.validate.tick(*fsm.ctx);
    }
};
struct validate_needs_service {
    template <class Fsm, class Evt, class SrcT, class TarT> bool operator()(Evt const&, Fsm& fsm, SrcT&, TarT&) {
        return fsm.validate.needs_service();
    }
};

struct reset_matching_subfsm {
    template <class Evt, class Fsm, class SrcT, class TarT> void operator()(Evt const&, Fsm& fsm, SrcT&, TarT&) {
        fsm.ctx->status.session_count = 0;
        fsm.sessions.clear();
        fsm.to.setDuration(std::chrono::milliseconds(fsm.ctx->slac_config.slac_init_timeout_ms));
        fsm.to.reset();
        fsm.failed_matching_reset_once = true;
    }
};

struct add_session {
    template <class Fsm, class SrcT, class TarT> void operator()(message const& e, Fsm& fsm, SrcT&, TarT&) {
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
        auto session_iter = std::find_if(fsm.sessions.begin(), fsm.sessions.end(), [&data](auto const& session) {
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
struct is_validate_req : public is_message_of_type<defs::MMTYPE_CM_VALIDATE | defs::MMTYPE_MODE_REQ> {};

struct should_reset_instead_of_fail {
    template <class Evt, class Fsm, class SrcT, class TarT> bool operator()(Evt const&, Fsm& fsm, SrcT&, TarT&) {
        auto const should_timeout = fsm.to.timeout();
        auto const is_failed = all_sessions_failed(fsm);
        auto const no_sessions = fsm.sessions.empty();
        auto const should_reset = (not fsm.failed_matching_reset_once) and
                                  fsm.ctx->slac_config.reset_instead_of_fail and
                                  (is_failed or (no_sessions && should_timeout));
        return should_reset;
    }
};
struct should_transition_to_failed_matching {
    template <class Evt, class Fsm, class SrcT, class TarT> bool operator()(Evt const&, Fsm& fsm, SrcT&, TarT&) {
        auto const should_timeout = fsm.to.timeout();
        auto const is_failed = all_sessions_failed(fsm);
        auto const no_sessions = fsm.sessions.empty();
        auto const should_fail = (is_failed or (no_sessions && should_timeout)) and
                                 ((not fsm.ctx->slac_config.reset_instead_of_fail) or fsm.failed_matching_reset_once);
        return should_fail;
    }
};

} // namespace everest::lib::slac::msm::matching_sm
