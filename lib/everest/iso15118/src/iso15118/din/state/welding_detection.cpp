// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/din/state/welding_detection.hpp>

#include <iso15118/din/state/session_stop.hpp>

#include <iso15118/detail/din/state/sequence_error.hpp>
#include <iso15118/detail/din/state/session_stop.hpp>
#include <iso15118/detail/din/state/state_helper.hpp>
#include <iso15118/detail/din/state/welding_detection.hpp>
#include <iso15118/detail/helper.hpp>

namespace iso15118::din::state {

namespace {
// [V2G-DC-925/926]: bound the welding-detection loop with V2G_SECC_WeldingDetection_Timeout (20 s,
// Table 77); on expiry terminate the session.
constexpr uint32_t TIMEOUT_WELDING_DETECTION_MS = 20000;
// V2G_SECC_CPState_Detection_Timeout (Table 77): maximum time to wait for CP State B after receiving
// the request following PowerDelivery(off) [V2G-DC-988]/[V2G-DC-556].
constexpr uint32_t TIMEOUT_CPSTATE_DETECTION_MS = 1500;
} // namespace

message_din::WeldingDetectionResponse handle_request(const message_din::WeldingDetectionRequest& req,
                                                     float present_voltage, const dt::SessionId& session_id,
                                                     std::optional<dt::DcEvseStatusCode> error_status_code) {
    message_din::WeldingDetectionResponse res;
    setup_header(res.header, session_id);

    // DC_EVSEStatus and EVSEPresentVoltage are mandatory in WeldingDetectionRes and must be present even on
    // a FAILED_UnknownSession response, so populate them before the SessionID check below.
    // A module-reported EVSE error (Malfunction / UtilityInterruptEvent) overrides the status code so the
    // EV sees the fault during welding detection (EvseV2G parity); EVSE_Ready otherwise.
    res.dc_evse_status.evse_status_code = error_status_code.value_or(dt::DcEvseStatusCode::EVSE_Ready);
    res.dc_evse_status.evse_isolation_status = dt::IsolationLevel::Valid;
    res.evse_present_voltage = present_voltage;

    // [V2G-DC-391] the SessionID must match the one assigned in SessionSetup; a mismatch is answered with
    // FAILED_UnknownSession (carrying the mandatory parameters filled above) and terminates the session.
    if (session_id != req.header.session_id) {
        return response_with_code(res, dt::ResponseCode::FAILED_UnknownSession);
    }

    return response_with_code(res, dt::ResponseCode::OK);
}

void WeldingDetection::enter() {
    m_ctx.log.enter_state("WeldingDetection");
}

void WeldingDetection::process_request(const message_din::WeldingDetectionRequest& req) {
    const auto res = handle_request(req, m_ctx.present_voltage, m_ctx.get_session_id(), m_ctx.error_status_code());
    m_ctx.respond(res);

    if (res.response_code >= dt::ResponseCode::FAILED) {
        m_ctx.session_stopped = true;
    }
}

Result WeldingDetection::feed(Event ev) {
    if (ev == Event::CONTROL_MESSAGE) {
        if (const auto* control_data = m_ctx.get_control_event<d20::PresentVoltageCurrent>()) {
            m_ctx.present_voltage = control_data->voltage;
        }
        // Parked while waiting for CP State B ([V2G-DC-988]): resume as soon as it is reported.
        if (pending_req.has_value() and m_ctx.current_cp_state == d20::CpState::B) {
            m_ctx.stop_timeout(d20::TimeoutType::CPSTATE);
            const auto req = pending_req.value();
            pending_req.reset();
            process_request(req);
        }
        return {};
    }

    if (ev == Event::TIMEOUT) {
        const auto* timeout = m_ctx.get_active_timeout();
        if (timeout and *timeout == d20::TimeoutType::ONGOING) {
            m_ctx.log("WeldingDetection timeout reached, terminating session");
            m_ctx.session_stopped = true;
        } else if (timeout and *timeout == d20::TimeoutType::CPSTATE and pending_req.has_value()) {
            // [V2G-DC-556] No CP State B within V2G_SECC_CPState_Detection_Timeout after the
            // WeldingDetectionReq: respond FAILED and end the session (the FAILED response arms the
            // FailedTermination path: oscillator off without delay + SECC-side TCP close).
            m_ctx.log("no CP State B within V2G_SECC_CPState_Detection_Timeout, WeldingDetection -> FAILED");
            pending_req.reset();
            message_din::WeldingDetectionResponse res;
            setup_header(res.header, m_ctx.get_session_id());
            res.dc_evse_status.evse_status_code = m_ctx.error_status_code().value_or(dt::DcEvseStatusCode::EVSE_Ready);
            res.dc_evse_status.evse_isolation_status = dt::IsolationLevel::Valid;
            res.evse_present_voltage = m_ctx.present_voltage;
            m_ctx.respond(response_with_code(res, dt::ResponseCode::FAILED));
            m_ctx.session_stopped = true;
        }
        return {};
    }

    if (ev != Event::V2GTP_MESSAGE) {
        return {};
    }

    // The EVCC ends welding detection with a SessionStopReq; defer it to the SessionStop state without
    // consuming the request (WAIT_FOR_WELDINGDETECTION_SESSIONSTOP).
    if (m_ctx.peek_request_type() == message_din::Type::SessionStopReq) {
        m_ctx.stop_timeout(d20::TimeoutType::ONGOING);
        return m_ctx.create_state<SessionStop>();
    }

    const auto variant = m_ctx.pull_request();

    if (const auto req = variant->get_if<message_din::WeldingDetectionRequest>()) {
        if (not welding_started) {
            m_ctx.start_timeout(d20::TimeoutType::ONGOING, TIMEOUT_WELDING_DETECTION_MS);
            welding_started = true;
        }

        // [V2G-DC-988] After PowerDelivery(off) the EV must signal CP State B before/around the next
        // request; give it V2G_SECC_CPState_Detection_Timeout from the request before failing.
        if (m_ctx.power_delivery_stopped and m_ctx.current_cp_state != d20::CpState::B) {
            pending_req = *req;
            m_ctx.start_timeout(d20::TimeoutType::CPSTATE, TIMEOUT_CPSTATE_DETECTION_MS);
            return {};
        }

        process_request(*req);
        return {};
    }

    m_ctx.log("expected WeldingDetectionReq! But code type id: %d", variant->get_type());
    // [V2G-DC-539]: answer with the received-type response carrying FAILED_SequenceError, then close.
    respond_sequence_error(m_ctx, *variant);
    m_ctx.session_stopped = true;
    return {};
}

} // namespace iso15118::din::state
