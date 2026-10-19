// SPDX-License-Identifier: Apache-2.0
// Copyright 2023 Pionix GmbH and Contributors to EVerest
#include <iso15118/d20/state/dc_welding_detection.hpp>
#include <iso15118/d20/state/session_stop.hpp>

#include <iso15118/detail/d20/context_helper.hpp>
#include <iso15118/detail/d20/state/dc_welding_detection.hpp>
#include <iso15118/detail/d20/state/session_stop.hpp>
#include <iso15118/detail/helper.hpp>

namespace iso15118::d20::state {

namespace dt = message_20::datatypes;

message_20::DC_WeldingDetectionResponse handle_request(const message_20::DC_WeldingDetectionRequest& req,
                                                       const d20::Session& session, const float present_voltage) {
    message_20::DC_WeldingDetectionResponse res;

    if (validate_and_setup_header(res.header, session, req.header.session_id) == false) {
        return response_with_code(res, dt::ResponseCode::FAILED_UnknownSession);
    }

    res.present_voltage = dt::from_float(present_voltage);

    return response_with_code(res, dt::ResponseCode::OK);
}

void DC_WeldingDetection::enter() {
    m_ctx.log.enter_state("DC_WeldingDetection");
}

Result DC_WeldingDetection::answer(const message_20::DC_WeldingDetectionRequest& req) {
    const auto present_voltage = m_ctx.cache_present_values.has_value() ? m_ctx.cache_present_values->voltage : 0.0f;
    const auto res = handle_request(req, m_ctx.session, present_voltage);

    m_ctx.respond(res);

    if (res.response_code >= dt::ResponseCode::FAILED) {
        m_ctx.welding_detection.finish(m_ctx.feedback);
        m_ctx.session_stopped = true;
        return {};
    }

    if (req.processing == dt::Processing::Ongoing) {
        return {};
    }

    m_ctx.welding_detection.finish(m_ctx.feedback);
    return m_ctx.create_state<SessionStop>();
}

// The first response waits for a value of the welding detection voltage source, or until waiting for it
// expired; otherwise it would carry a stale power supply measurement.
Result DC_WeldingDetection::answer_pending_req() {
    if (not pending_req.has_value()) {
        return {};
    }
    if (not first_answered and not m_ctx.welding_detection.has_voltage() and not welding_voltage_wait_expired) {
        return {};
    }

    m_ctx.clear_timeout(d20::TimeoutType::WELDING_VOLTAGE);
    const auto req = pending_req.value();
    pending_req.reset();
    first_answered = true;
    return answer(req);
}

Result DC_WeldingDetection::feed(Event ev) {

    if (ev == Event::CONTROL_MESSAGE) {
        // Present values are latched on the context before the state sees the event.
        return answer_pending_req();
    }

    if (ev == Event::TIMEOUT) {
        const auto* timeout = m_ctx.get_active_timeout();
        if (timeout and *timeout == d20::TimeoutType::WELDING_VOLTAGE) {
            logf_warning("No value from the welding detection voltage source within %d ms, answering "
                         "DC_WeldingDetection with the last present voltage",
                         d20::TIMEOUT_WELDING_VOLTAGE);
            welding_voltage_wait_expired = true;
            return answer_pending_req();
        }
        return {};
    }

    if (ev != Event::V2GTP_MESSAGE) {
        return {};
    }

    const auto variant = m_ctx.pull_request();

    if (const auto req = variant->get_if<message_20::DC_WeldingDetectionRequest>()) {
        if (first_answered) {
            return answer(*req);
        }
        pending_req = *req;
        if (not m_ctx.welding_detection.has_voltage()) {
            m_ctx.restart_timeout(d20::TimeoutType::WELDING_VOLTAGE, d20::TIMEOUT_WELDING_VOLTAGE);
        }
        return answer_pending_req();

    } else if (const auto req = variant->get_if<message_20::SessionStopRequest>()) {
        m_ctx.welding_detection.finish(m_ctx.feedback);
        const auto res = handle_request(*req, m_ctx.session);

        if (req->ev_termination_code.has_value()) {
            logf_info("EV termination code: %s", req->ev_termination_code.value().c_str());
        }
        if (req->ev_termination_explanation.has_value()) {
            logf_info("EV Termination explanation: %s", req->ev_termination_explanation.value().c_str());
        }

        m_ctx.respond(res);

        // Todo(sl): Tell the reason why the charger is stopping. Shutdown, Error, etc.
        if (req->charging_session == message_20::datatypes::ChargingSession::Pause) {
            m_ctx.session_paused = true;
            if (not m_ctx.pause_ctx.has_value()) {
                logf_error("Pause the session but pause_ctx has no value");
                return {};
            }
            m_ctx.pause_ctx->selected_service_parameters = m_ctx.session.get_selected_services();
        } else if (req->charging_session == message_20::datatypes::ChargingSession::Terminate) {
            m_ctx.session_stopped = true;
            m_ctx.pause_ctx.reset();
        }

        return {};
    } else {
        m_ctx.log("expected DC_WeldingDetection! But code type id: %d", variant->get_type());

        // Sequence Error
        const message_20::Type req_type = variant->get_type();
        send_sequence_error(req_type, m_ctx);

        m_ctx.welding_detection.finish(m_ctx.feedback);
        m_ctx.session_stopped = true;
        return {};
    }
}

} // namespace iso15118::d20::state
