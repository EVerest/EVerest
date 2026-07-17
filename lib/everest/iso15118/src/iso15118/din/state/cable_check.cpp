// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/din/state/cable_check.hpp>

#include <iso15118/din/state/pre_charge.hpp>
#include <iso15118/din/state/session_stop.hpp>

#include <iso15118/detail/din/state/cable_check.hpp>
#include <iso15118/detail/din/state/sequence_error.hpp>
#include <iso15118/detail/din/state/state_helper.hpp>
#include <iso15118/detail/helper.hpp>

namespace iso15118::din::state {

namespace {
// V2G_SECC_CPState_Detection_Timeout (Table 77): maximum time the SECC waits to detect CP State C/D
// after receiving a CableCheckReq before it stops the session [V2G-DC-967].
constexpr uint32_t TIMEOUT_CPSTATE_DETECTION_MS = 1500;
} // namespace

message_din::CableCheckResponse handle_request(const message_din::CableCheckRequest& req, bool cable_check_done,
                                               bool cable_check_fault, const dt::SessionId& session_id,
                                               std::optional<dt::DcEvseStatusCode> error_status_code) {
    message_din::CableCheckResponse res;
    setup_header(res.header, session_id);

    // DC_EVSEStatus is mandatory in CableCheckRes and must be present even on a FAILED_UnknownSession
    // response, so give it a schema-valid default before the SessionID check (the branches below overwrite
    // it for the OK / fault paths).
    res.dc_evse_status.evse_status_code = dt::DcEvseStatusCode::EVSE_Ready;
    res.dc_evse_status.evse_isolation_status = dt::IsolationLevel::Valid;

    // [V2G-DC-391] the SessionID must match the one assigned in SessionSetup; a mismatch is answered with
    // FAILED_UnknownSession (carrying the mandatory DC_EVSEStatus filled above) and terminates the session.
    if (session_id != req.header.session_id) {
        return response_with_code(res, dt::ResponseCode::FAILED_UnknownSession);
    }

    if (cable_check_fault) {
        // [V2G-DC-890] An isolation fault detected during the cable check is answered with a negative
        // CableCheckRes (Invalid isolation, EVSE_Malfunction); the session is then terminated.
        res.evse_processing = dt::EvseProcessing::Finished;
        res.dc_evse_status.evse_status_code = dt::DcEvseStatusCode::EVSE_Malfunction;
        res.dc_evse_status.evse_isolation_status = dt::IsolationLevel::Invalid;
        return response_with_code(res, dt::ResponseCode::FAILED);
    }

    if (cable_check_done) {
        res.evse_processing = dt::EvseProcessing::Finished;
        // [V2G-DC-499] The EVCC proceeds to PreCharge only on EVSE_Ready with a Valid/Warning isolation.
        res.dc_evse_status.evse_status_code = dt::DcEvseStatusCode::EVSE_Ready;
        res.dc_evse_status.evse_isolation_status = dt::IsolationLevel::Valid;
    } else {
        res.evse_processing = dt::EvseProcessing::Ongoing;
        res.dc_evse_status.evse_status_code = dt::DcEvseStatusCode::EVSE_IsolationMonitoringActive;
    }

    // A module-reported EVSE error (Malfunction / UtilityInterruptEvent) overrides the status code so the
    // EV sees the fault during isolation monitoring too (EvseV2G parity); the isolation-fault path above
    // returns earlier and keeps its own EVSE_Malfunction.
    if (error_status_code.has_value()) {
        res.dc_evse_status.evse_status_code = error_status_code.value();
    }

    return response_with_code(res, dt::ResponseCode::OK);
}

void CableCheck::enter() {
    m_ctx.log.enter_state("CableCheck");
}

Result CableCheck::process_request(const message_din::CableCheckRequest& req) {
    if (not cable_check_initiated) {
        m_ctx.feedback.signal(session::feedback::Signal::START_CABLE_CHECK);
        cable_check_initiated = true;
        // The CP State C/D gate above passed, so the EV is now in the DC charging phase and must stay
        // in C/D; an unexpected CP State B from here on is a fault ([V2G-DC-668], handled in the
        // engine's CpStateChanged path).
        m_ctx.expect_cp_state_cd = true;
    }

    const auto res = handle_request(req, m_ctx.cable_check_done, m_ctx.cable_check_fault, m_ctx.get_session_id(),
                                    m_ctx.error_status_code());
    m_ctx.respond(res);

    if (res.response_code >= dt::ResponseCode::FAILED) {
        m_ctx.session_stopped = true;
        return {};
    }

    if (m_ctx.cable_check_done) {
        return m_ctx.create_state<PreCharge>();
    }
    return {};
}

Result CableCheck::feed(Event ev) {
    if (ev == Event::CONTROL_MESSAGE) {
        if (const auto* control_data = m_ctx.get_control_event<d20::CableCheckFinished>()) {
            // The module reports the finished cable-check result: success -> isolation valid; failure ->
            // isolation fault [V2G-DC-890]. Absence of the event means the check is still ongoing.
            if (static_cast<bool>(*control_data)) {
                m_ctx.cable_check_done = true;
            } else {
                m_ctx.cable_check_fault = true;
            }
        }
        // Parked while waiting for CP State C/D ([V2G-DC-967]): resume as soon as it is reported.
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
        if (timeout and *timeout == d20::TimeoutType::CPSTATE and pending_req.has_value()) {
            // [V2G-DC-967] No CP State C/D within V2G_SECC_CPState_Detection_Timeout after the
            // CableCheckReq: the cable check must not run; respond FAILED and end the session (the
            // FAILED response arms the FailedTermination path: oscillator off + SECC-side TCP close).
            m_ctx.log("no CP State C/D within V2G_SECC_CPState_Detection_Timeout, CableCheck -> FAILED");
            pending_req.reset();
            message_din::CableCheckResponse res;
            setup_header(res.header, m_ctx.get_session_id());
            res.evse_processing = dt::EvseProcessing::Finished;
            res.dc_evse_status.evse_status_code = dt::DcEvseStatusCode::EVSE_Shutdown;
            res.dc_evse_status.evse_isolation_status = dt::IsolationLevel::Valid;
            m_ctx.respond(response_with_code(res, dt::ResponseCode::FAILED));
            m_ctx.session_stopped = true;
        }
        return {};
    }

    if (ev != Event::V2GTP_MESSAGE) {
        return {};
    }

    // An EV aborting sends SessionStopReq; hand it to SessionStop for a clean SessionStopRes.
    if (m_ctx.peek_request_type() == message_din::Type::SessionStopReq) {
        return m_ctx.create_state<SessionStop>();
    }

    const auto variant = m_ctx.pull_request();

    if (const auto req = variant->get_if<message_din::CableCheckRequest>()) {
        // [V2G-DC-967]: the SECC must detect CP State C (or D, [V2G-DC-493]) within
        // V2G_SECC_CPState_Detection_Timeout of the CableCheckReq before running the cable check; park
        // the request until the CP state is reported or the timeout fails it. A request that misses the
        // SessionID check bypasses the gate (FAILED_UnknownSession is due immediately).
        const bool cp_charging_state =
            m_ctx.current_cp_state == d20::CpState::C or m_ctx.current_cp_state == d20::CpState::D;
        if (not cp_charging_state and m_ctx.get_session_id() == req->header.session_id) {
            pending_req = *req;
            m_ctx.start_timeout(d20::TimeoutType::CPSTATE, TIMEOUT_CPSTATE_DETECTION_MS);
            return {};
        }

        return process_request(*req);
    }

    m_ctx.log("expected CableCheckReq! But code type id: %d", variant->get_type());
    // [V2G-DC-539]: answer with the received-type response carrying FAILED_SequenceError, then close.
    respond_sequence_error(m_ctx, *variant);
    m_ctx.session_stopped = true;
    return {};
}

} // namespace iso15118::din::state
