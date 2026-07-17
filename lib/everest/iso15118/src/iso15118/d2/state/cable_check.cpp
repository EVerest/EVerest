// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/d2/state/cable_check.hpp>

#include <iso15118/d2/state/pre_charge.hpp>
#include <iso15118/d2/state/session_stop.hpp>

#include <iso15118/detail/d2/state/cable_check.hpp>
#include <iso15118/detail/d2/state/sequence_error.hpp>
#include <iso15118/detail/helper.hpp>

namespace iso15118::d2::state {

namespace {
// V2G_SECC_CableCheck_Performance_Time = 38 s (Table 111 / DIN Table 77). The SECC bounds its cable-check
// (isolation-monitoring) phase 2 s below the EVCC's 40 s timeout so it gives up first.
constexpr uint32_t TIMEOUT_CABLE_CHECK_MS = 38000;
// V2G_SECC_CPState_Detection_Timeout (Table 111): maximum time to detect CP State C/D after the first
// CableCheckReq before the cable check is refused ([V2G2-916]..[V2G2-918]).
constexpr uint32_t TIMEOUT_CPSTATE_DETECTION_MS = 1500;
} // namespace

message_2::CableCheckResponse handle_request([[maybe_unused]] const message_2::CableCheckRequest& req,
                                             const dt::SessionId& session_id, bool cable_check_done,
                                             bool cable_check_fault,
                                             std::optional<dt::DC_EVSEStatusCode> error_status_code) {
    message_2::CableCheckResponse res;
    res.header.session_id = session_id;

    res.dc_evse_status.notification = dt::EVSENotification::None;
    res.dc_evse_status.notification_max_delay = 0;

    if (cable_check_fault) {
        // An isolation fault reported by the module is answered with a negative CableCheckRes (Invalid
        // isolation, EVSE_Malfunction); the session is then terminated (mirrors DIN [V2G-DC-890]).
        res.response_code = dt::ResponseCode::FAILED;
        res.dc_evse_status.isolation_status = dt::IsolationLevel::Invalid;
        res.dc_evse_status.status_code = dt::DC_EVSEStatusCode::EVSE_Malfunction;
        res.evse_processing = dt::EVSEProcessing::Finished;
        return res;
    }

    res.response_code = dt::ResponseCode::OK;
    res.dc_evse_status.isolation_status = cable_check_done ? dt::IsolationLevel::Valid : dt::IsolationLevel::Invalid;
    res.dc_evse_status.status_code =
        cable_check_done ? dt::DC_EVSEStatusCode::EVSE_Ready : dt::DC_EVSEStatusCode::EVSE_IsolationMonitoringActive;

    res.evse_processing = cable_check_done ? dt::EVSEProcessing::Finished : dt::EVSEProcessing::Ongoing;

    // A module-reported EVSE error (Malfunction / UtilityInterruptEvent) overrides the status code so the
    // EV sees the fault during isolation monitoring too (EvseV2G stamps its status code into every DC
    // response). The isolation-fault path above returns earlier and keeps its own EVSE_Malfunction.
    if (error_status_code.has_value()) {
        res.dc_evse_status.status_code = error_status_code.value();
    }
    return res;
}

void CableCheck::enter() {
    m_ctx.log.enter_state("CableCheck");
}

Result CableCheck::process_request(const message_2::CableCheckRequest& req) {
    if (not cable_check_initiated) {
        // On the initial cable check (cable_check_done still false) trigger the module isolation test and
        // arm its performance timeout. On a renegotiation loop-back the SECC re-enters CableCheck with
        // cable_check_done already true: the contactor stayed closed throughout renegotiation (ISO
        // 15118-2 §8.7.4.3 NOTE 1) so the cable was never disconnected and isolation is still valid. A
        // physical isolation re-test is neither possible (it requires ramping the supply down / opening,
        // which NOTE 1 forbids) nor required, and re-signalling START_CABLE_CHECK would drive
        // EvseManager::cable_check(), which aborts because the Charger has left PrepareCharging for
        // Charging. So skip the physical trigger and let handle_request() answer the re-negotiated
        // CableCheckReq with isolation=Valid / EVSEProcessing=Finished immediately (Figure 107 message
        // sequence is still honoured: only the isolation hardware step is skipped, not the response).
        if (not m_ctx.cable_check_done) {
            m_ctx.feedback.signal(session::feedback::Signal::START_CABLE_CHECK);
            m_ctx.start_timeout(d20::TimeoutType::ONGOING, TIMEOUT_CABLE_CHECK_MS);
        }
        cable_check_initiated = true;
    }

    const auto res = handle_request(req, m_ctx.get_session_id(), m_ctx.cable_check_done, m_ctx.cable_check_fault,
                                    m_ctx.error_status_code());
    m_ctx.respond(res);

    // An isolation fault answers CableCheckRes/FAILED and terminates the session.
    if (res.response_code >= dt::ResponseCode::FAILED) {
        m_ctx.stop_timeout(d20::TimeoutType::ONGOING);
        m_ctx.session_stopped = true;
        return {};
    }

    if (res.evse_processing == dt::EVSEProcessing::Finished) {
        m_ctx.stop_timeout(d20::TimeoutType::ONGOING);
        return m_ctx.create_state<PreCharge>();
    }

    return {};
}

Result CableCheck::feed(Event ev) {
    if (ev == Event::CONTROL_MESSAGE) {
        if (const auto* control = m_ctx.get_control_event<d20::CableCheckFinished>()) {
            // Module-reported cable-check result: success -> isolation valid; failure -> isolation fault.
            // Absence of the event means the check is still ongoing.
            if (static_cast<bool>(*control)) {
                m_ctx.cable_check_done = true;
            } else {
                m_ctx.cable_check_fault = true;
            }
        }
        // Parked while waiting for CP State C/D ([V2G2-916]..[V2G2-918]): resume as soon as it arrives.
        if (pending_req.has_value() and
            (m_ctx.current_cp_state == d20::CpState::C or m_ctx.current_cp_state == d20::CpState::D)) {
            m_ctx.stop_timeout(d20::TimeoutType::CPSTATE);
            const auto req = pending_req.value();
            pending_req.reset();
            return process_request(req);
        }
        return {};
    }

    if (ev == Event::TIMEOUT) {
        const auto* timeout = m_ctx.get_active_timeout();
        if (timeout and *timeout == d20::TimeoutType::ONGOING) {
            m_ctx.log("CableCheck ongoing timeout reached, terminating session");
            m_ctx.session_stopped = true;
        } else if (timeout and *timeout == d20::TimeoutType::CPSTATE and pending_req.has_value()) {
            // [V2G2-918] No CP State C/D within V2G_SECC_CPState_Detection_Timeout after the
            // CableCheckReq: the cable check must not run; answer FAILED and end the session (the FAILED
            // response arms the FailedTermination path: oscillator off + SECC-side TCP close).
            m_ctx.log("no CP State C/D within V2G_SECC_CPState_Detection_Timeout, CableCheck -> FAILED");
            pending_req.reset();
            auto res =
                handle_request(message_2::CableCheckRequest{}, m_ctx.get_session_id(),
                               /*cable_check_done=*/false, /*cable_check_fault=*/false, m_ctx.error_status_code());
            res.response_code = dt::ResponseCode::FAILED;
            res.dc_evse_status.isolation_status = dt::IsolationLevel::Invalid;
            res.evse_processing = dt::EVSEProcessing::Finished;
            m_ctx.respond(res);
            m_ctx.session_stopped = true;
        }
        return {};
    }

    if (ev != Event::V2GTP_MESSAGE) {
        return {};
    }

    // An EV aborting mid-handshake sends SessionStopReq; hand it to SessionStop for a clean SessionStopRes.
    if (m_ctx.peek_request_type() == message_2::Type::SessionStopReq) {
        return m_ctx.create_state<SessionStop>();
    }

    const auto variant = m_ctx.pull_request();

    const auto req = variant->get_if<message_2::CableCheckRequest>();
    if (req == nullptr) {
        m_ctx.log("expected CableCheckReq! But code type id: %d", variant->get_type());
        // [V2G2-539]: answer with the received-type response carrying FAILED_SequenceError, then close.
        respond_sequence_error(m_ctx, *variant);
        m_ctx.session_stopped = true;
        return {};
    }

    // The request must echo the assigned SessionID [V2G2-388]; a mismatch is answered with
    // CableCheckRes/FAILED_UnknownSession and terminates the session.
    if (reject_unknown_session(m_ctx, *variant)) {
        return {};
    }

    // [V2G2-916]..[V2G2-918] On the initial cable check the EV must be in CP State C/D; give it
    // V2G_SECC_CPState_Detection_Timeout from the request before answering FAILED. A renegotiation
    // loop-back (cable_check_done already true, contactor still closed) keeps CP C/D and is not gated.
    if (not m_ctx.cable_check_done and m_ctx.current_cp_state != d20::CpState::C and
        m_ctx.current_cp_state != d20::CpState::D) {
        pending_req = *req;
        m_ctx.start_timeout(d20::TimeoutType::CPSTATE, TIMEOUT_CPSTATE_DETECTION_MS);
        return {};
    }

    return process_request(*req);
}

} // namespace iso15118::d2::state
