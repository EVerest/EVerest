// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/din/state/welding_detection.hpp>

#include <iso15118/din/state/session_stop.hpp>

#include <iso15118/detail/din/state/session_stop.hpp>
#include <iso15118/detail/din/state/state_helper.hpp>
#include <iso15118/detail/din/state/welding_detection.hpp>
#include <iso15118/detail/helper.hpp>

namespace iso15118::din::state {

message_din::WeldingDetectionResponse handle_request(const message_din::WeldingDetectionRequest& req,
                                                     float present_voltage, const dt::SessionId& session_id) {
    message_din::WeldingDetectionResponse res;

    if (not validate_and_setup_header(res.header, session_id, req.header.session_id)) {
        return response_with_code(res, dt::ResponseCode::FAILED_UnknownSession);
    }

    res.dc_evse_status.evse_status_code = dt::DcEvseStatusCode::EVSE_Ready;
    res.dc_evse_status.evse_isolation_status = dt::IsolationLevel::Valid;
    res.evse_present_voltage = present_voltage;

    return response_with_code(res, dt::ResponseCode::OK);
}

void WeldingDetection::enter() {
    m_ctx.log.enter_state("WeldingDetection");
}

Result WeldingDetection::feed(Event ev) {
    if (ev == Event::CONTROL_MESSAGE) {
        if (const auto* control_data = m_ctx.get_control_event<d20::PresentVoltageCurrent>()) {
            m_ctx.present_voltage = control_data->voltage;
        }
        return {};
    }

    if (ev != Event::V2GTP_MESSAGE) {
        return {};
    }

    // The EVCC ends welding detection with a SessionStopReq; defer it to the SessionStop state without
    // consuming the request (WAIT_FOR_WELDINGDETECTION_SESSIONSTOP).
    if (m_ctx.peek_request_type() == message_din::Type::SessionStopReq) {
        return m_ctx.create_state<SessionStop>();
    }

    const auto variant = m_ctx.pull_request();

    if (const auto req = variant->get_if<message_din::WeldingDetectionRequest>()) {
        const auto res = handle_request(*req, m_ctx.present_voltage, m_ctx.get_session_id());
        m_ctx.respond(res);

        if (res.response_code >= dt::ResponseCode::FAILED) {
            m_ctx.session_stopped = true;
        }
        return {};
    }

    m_ctx.log("expected WeldingDetectionReq! But code type id: %d", variant->get_type());
    message_din::WeldingDetectionResponse res;
    setup_header(res.header, m_ctx.get_session_id());
    m_ctx.respond(response_with_code(res, dt::ResponseCode::FAILED_SequenceError));
    m_ctx.session_stopped = true;
    return {};
}

} // namespace iso15118::din::state
