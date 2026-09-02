// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 - 2026 Pionix GmbH and Contributors to EVerest

// Guards and actions of the Matching sub-machine (see matching.hpp).

#pragma once
#include "common.hpp"

namespace everest::lib::slac::msm::matching_sm {

// Guards
struct is_slac_param_req : public is_message_of_type<slac::defs::MMTYPE_CM_SLAC_PARAM | slac::defs::MMTYPE_MODE_REQ> { };
// A session in MatchComplete means CM_SLAC_MATCH.CNF is out, but Matching is only exited on the
// next update tick. In that window (and per ISO 15118-3 whenever an AVLN is up) a fresh
// CM_SLAC_PARM.REQ must get no CNF (TC_SECC_CMN_VTB_PLCLinkStatus_003) and must not restart the
// completed session.
struct has_matched_session {
    template <class Fsm, class Evt, class SrcT, class TarT>
    bool operator()(Evt const&, Fsm& fsm, SrcT&, TarT&) {
        return fsm.is_matched(update{});
    }
};

//Actions
struct ignore_parm_req {
    template <class Fsm, class SrcT, class TarT>
    void operator()(message const&, Fsm& fsm, SrcT&, TarT&) {
        fsm.ctx->log_info("Ignoring CM_SLAC_PARM.REQ, match already completed");
    }
};
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
                                        [&data](auto const& session) {
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

} // namespace everest::lib::slac::msm::matching_sm
