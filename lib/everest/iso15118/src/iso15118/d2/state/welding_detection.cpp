// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/d2/state/welding_detection.hpp>

#include <iso15118/d2/state/session_stop.hpp>

#include <iso15118/detail/d2/state/sequence_error.hpp>
#include <iso15118/detail/d2/state/welding_detection.hpp>
#include <iso15118/detail/helper.hpp>

namespace iso15118::d2::state {

message_2::WeldingDetectionResponse handle_request([[maybe_unused]] const message_2::WeldingDetectionRequest& req,
                                                   const dt::SessionId& session_id, float present_voltage) {
    message_2::WeldingDetectionResponse res;
    res.header.session_id = session_id;
    res.response_code = dt::ResponseCode::OK;

    res.dc_evse_status.notification = dt::EVSENotification::None;
    res.dc_evse_status.notification_max_delay = 0;
    res.dc_evse_status.isolation_status = dt::IsolationLevel::Valid;
    res.dc_evse_status.status_code = dt::DC_EVSEStatusCode::EVSE_Ready;

    res.evse_present_voltage = dt::to_physical_value(present_voltage, dt::Unit::V);
    return res;
}

void WeldingDetection::enter() {
    m_ctx.log.enter_state("WeldingDetection");
}

Result WeldingDetection::feed(Event ev) {
    if (ev == Event::CONTROL_MESSAGE) {
        if (const auto* control = m_ctx.get_control_event<d20::PresentVoltageCurrent>()) {
            m_ctx.present_voltage = control->voltage;
            m_ctx.present_current = control->current;
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

    const auto res = handle_request(*req, m_ctx.get_session_id(), m_ctx.present_voltage);
    m_ctx.respond(res);

    return {};
}

} // namespace iso15118::d2::state
