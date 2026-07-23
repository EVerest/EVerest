// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/d2/state/welding_detection.hpp>

#include <iso15118/d2/state/charge_parameter_discovery.hpp>
#include <iso15118/d2/state/session_stop.hpp>

#include <iso15118/detail/d2/state/sequence_error.hpp>
#include <iso15118/detail/d2/state/welding_detection.hpp>
#include <iso15118/detail/helper.hpp>

namespace iso15118::d2::state {

namespace {
// V2G_SECC_CPState_Detection_Timeout (Table 77): maximum time to wait for CP State B after receiving
// the request following PowerDelivery(Stop) ([V2G2-920]..[V2G2-922]).
constexpr uint32_t TIMEOUT_CPSTATE_DETECTION_MS = 1500;
} // namespace

// ISO 15118-2 defines no SECC-side WeldingDetection supervision timer (unlike DIN's
// V2G_SECC_WeldingDetection_Timeout, Table 77); the wait for the next request is bounded solely by
// V2G_SECC_Sequence_Timeout (60 s), armed at the session layer after every response. The -4 ATS
// (TC_SECC_DC_VTB_SessionStop_009) asserts no close happens before that timeout expires.
message_2::WeldingDetectionResponse handle_request([[maybe_unused]] const message_2::WeldingDetectionRequest& req,
                                                   const dt::SessionId& session_id, float present_voltage,
                                                   std::optional<dt::DC_EVSEStatusCode> error_status_code) {
    message_2::WeldingDetectionResponse res;
    res.header.session_id = session_id;
    res.response_code = dt::ResponseCode::OK;

    res.dc_evse_status.notification = dt::EVSENotification::None;
    res.dc_evse_status.notification_max_delay = 0;
    res.dc_evse_status.isolation_status = dt::IsolationLevel::Valid;
    // A module-reported EVSE error (Malfunction / UtilityInterruptEvent) overrides the status code so the
    // EV sees the fault during welding detection (EvseV2G parity); EVSE_Ready otherwise.
    res.dc_evse_status.status_code = error_status_code.value_or(dt::DC_EVSEStatusCode::EVSE_Ready);

    res.evse_present_voltage = dt::to_physical_value(present_voltage, dt::Unit::V);
    return res;
}

void WeldingDetection::enter() {
    m_ctx.log.enter_state("WeldingDetection");
}

void WeldingDetection::process_request(const message_2::WeldingDetectionRequest& req) {
    const auto res = handle_request(req, m_ctx.get_session_id(), m_ctx.present_voltage, m_ctx.error_status_code());
    m_ctx.respond(res);
}

Result WeldingDetection::feed(Event ev) {
    if (ev == Event::CONTROL_MESSAGE) {
        if (const auto* control = m_ctx.get_control_event<d20::PresentVoltageCurrent>()) {
            m_ctx.present_voltage = control->voltage;
            m_ctx.present_current = control->current;
        }
        // Parked while waiting for CP State B ([V2G2-920]..[V2G2-922]): resume as soon as it arrives.
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
        if (timeout and *timeout == d20::TimeoutType::CPSTATE and pending_req.has_value()) {
            // [V2G2-922] No CP State B within V2G_SECC_CPState_Detection_Timeout after the
            // WeldingDetectionReq: respond FAILED and end the session (the FAILED response arms the
            // FailedTermination path: oscillator off + SECC-side TCP close).
            m_ctx.log("no CP State B within V2G_SECC_CPState_Detection_Timeout, WeldingDetection -> FAILED");
            pending_req.reset();
            auto res = handle_request(message_2::WeldingDetectionRequest{}, m_ctx.get_session_id(),
                                      m_ctx.present_voltage, m_ctx.error_status_code());
            res.response_code = dt::ResponseCode::FAILED;
            m_ctx.respond(res);
            m_ctx.session_stopped = true;
        }
        return {};
    }

    if (ev != Event::V2GTP_MESSAGE) {
        return {};
    }

    // The welding-detection loop ends when the EV sends SessionStopReq; hand it to SessionStop.
    if (m_ctx.peek_request_type() == message_2::Type::SessionStopReq) {
        return m_ctx.create_state<SessionStop>();
    }

    // [V2G2-601]: after a DC PowerDeliveryReq(Stop) the EV may also restart the parameter exchange
    // with a ChargeParameterDiscoveryReq (e.g. to charge again with new parameters); hand it to
    // ChargeParameterDiscovery exactly like the PowerDelivery renegotiation path [V2G2-813].
    if (m_ctx.peek_request_type() == message_2::Type::ChargeParameterDiscoveryReq) {
        return m_ctx.create_state<ChargeParameterDiscovery>();
    }

    const auto variant = m_ctx.pull_request();

    const auto req = variant->get_if<message_2::WeldingDetectionRequest>();
    if (req == nullptr) {
        m_ctx.log("expected WeldingDetectionReq! But code type id: %d", variant->get_type());
        // [V2G2-539]: answer with the received-type response carrying FAILED_SequenceError, then close.
        respond_sequence_error(m_ctx, *variant);
        m_ctx.session_stopped = true;
        return {};
    }

    // The request must echo the assigned SessionID [V2G2-388]; a mismatch is answered with
    // WeldingDetectionRes/FAILED_UnknownSession and terminates the session.
    if (reject_unknown_session(m_ctx, *variant)) {
        return {};
    }

    // [V2G2-920]..[V2G2-922] After PowerDelivery(Stop) the EV must be in CP State B; give it
    // V2G_SECC_CPState_Detection_Timeout from the request before answering FAILED.
    if (m_ctx.power_delivery_stopped and m_ctx.current_cp_state != d20::CpState::B) {
        pending_req = *req;
        m_ctx.start_timeout(d20::TimeoutType::CPSTATE, TIMEOUT_CPSTATE_DETECTION_MS);
        return {};
    }

    process_request(*req);
    return {};
}

} // namespace iso15118::d2::state
