// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
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
        set_response_code(res, dt::ResponseCode::FAILED_UnknownSession);
        return res;
    }

    res.present_voltage = dt::from_float(present_voltage);

    set_response_code(res, dt::ResponseCode::OK);
    return res;
}

void DC_WeldingDetection::enter() {
    logf_debug("Enter state: DC_WeldingDetection");
}

Result DC_WeldingDetection::answer(const message_20::DC_WeldingDetectionRequest& req) {
    const auto res = handle_request(req, m_ctx.session, present_voltage);

    m_ctx.respond(res);

    if (res.response_code >= dt::ResponseCode::FAILED) {
        m_ctx.session_stopped = true;
        return {};
    }

    if (req.processing == dt::Processing::Ongoing) {
        return {};
    }

    return m_ctx.create_state<SessionStop>();
}

void DC_WeldingDetection::leave() {
    // No parked request's timer survives into the next state.
    pending_req.reset();
    m_ctx.stop_timeout(d20::TimeoutType::PRESENT_VALUES);
}

Result DC_WeldingDetection::feed(Event ev) {

    // A session ended meanwhile (CP State A, error) gets no answer to a parked request.
    if (pending_req.has_value() and m_ctx.session_stopped) {
        pending_req.reset();
        m_ctx.stop_timeout(d20::TimeoutType::PRESENT_VALUES);
    }

    if (ev == Event::CONTROL_MESSAGE) {
        const auto control_data = m_ctx.get_control_event<PresentVoltageCurrent>();
        if (not control_data) {
            // Ignore control message
            return {};
        }

        present_voltage = control_data->voltage;

        if (pending_req.has_value() and --present_values_outstanding == 0) {
            m_ctx.stop_timeout(d20::TimeoutType::PRESENT_VALUES);
            const auto req = pending_req.value();
            pending_req.reset();
            return answer(req);
        }

        return {};
    }

    if (ev == Event::TIMEOUT) {
        const auto* const timeout = m_ctx.get_active_timeout();
        if (pending_req.has_value() and timeout != nullptr and *timeout == d20::TimeoutType::PRESENT_VALUES) {
            // With every update outstanding the source stalled; with the first one in, the value is at worst
            // one poll interval older than the request.
            if (present_values_outstanding >= d20::PRESENT_VALUES_NEEDED) {
                logf_warning("No present values within %d ms after the DC_WeldingDetectionReq, answering with the "
                             "last ones",
                             d20::TIMEOUT_PRESENT_VALUES);
            } else {
                logf_info("Only one present values update within %d ms after the DC_WeldingDetectionReq, "
                          "answering with it",
                          d20::TIMEOUT_PRESENT_VALUES);
            }
            present_values_outstanding = 0;
            const auto req = pending_req.value();
            pending_req.reset();
            return answer(req);
        }
        return {};
    }

    if (ev != Event::V2GTP_MESSAGE) {
        return {};
    }

    const auto variant = m_ctx.pull_request();

    if (const auto req = variant->get_if<message_20::DC_WeldingDetectionRequest>()) {
        message_20::Header header;
        if (validate_and_setup_header(header, m_ctx.session, req->header.session_id) == false) {
            return answer(*req);
        }
        // The EVSEPresentVoltage has to be measured after the request, not before the PowerDeliveryRes or the
        // EV's last switching of its disconnection device [IEC 61851-23:2023 Table CC.9 t105 to t106].
        pending_req = *req;
        present_values_outstanding = d20::PRESENT_VALUES_NEEDED;
        m_ctx.restart_timeout(d20::TimeoutType::PRESENT_VALUES, d20::TIMEOUT_PRESENT_VALUES);
        return {};

    } else if (const auto req = variant->get_if<message_20::SessionStopRequest>()) {
        if (pending_req.has_value()) {
            // The EV gave up on its DC_WeldingDetectionReq; its stop takes over.
            pending_req.reset();
            m_ctx.stop_timeout(d20::TimeoutType::PRESENT_VALUES);
        }
        const auto res = handle_request(*req, m_ctx.session);

        if (req->ev_termination_code.has_value()) {
            logf_info("EV termination code: %s", req->ev_termination_code.value().c_str());
        }
        if (req->ev_termination_explanation.has_value()) {
            logf_info("EV Termination explanation: %s", req->ev_termination_explanation.value().c_str());
        }

        m_ctx.respond(res);
        mark_session_stop_response(m_ctx, *req, res);

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
        logf_warning("Expected DC_WeldingDetection! But code type id: %d", variant->get_type());
        pending_req.reset();
        m_ctx.stop_timeout(d20::TimeoutType::PRESENT_VALUES);

        // Sequence Error
        const message_20::Type req_type = variant->get_type();
        send_sequence_error(req_type, m_ctx);

        m_ctx.session_stopped = true;
        return {};
    }
}

} // namespace iso15118::d20::state
