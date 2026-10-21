// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <iso15118/d20/state/session_stop.hpp>

#include <iso15118/detail/d20/context_helper.hpp>
#include <iso15118/detail/d20/state/session_stop.hpp>
#include <iso15118/detail/helper.hpp>

namespace iso15118::d20::state {

namespace dt = message_20::datatypes;

message_20::SessionStopResponse handle_request(const message_20::SessionStopRequest& req, const d20::Session& session) {

    message_20::SessionStopResponse res;

    if (validate_and_setup_header(res.header, session, req.header.session_id) == false) {
        set_response_code(res, dt::ResponseCode::FAILED_UnknownSession);
        return res;
    }

    if (req.charging_session == dt::ChargingSession::ServiceRenegotiation &&
        session.service_renegotiation_supported == false) {
        set_response_code(res, dt::ResponseCode::FAILED_NoServiceRenegotiationSupported);
        return res;
    }

    // [V2G20-1195]: in dynamic control mode only the SECC may initiate a pause ([V2G20-1850]).
    if (req.charging_session == dt::ChargingSession::Pause and
        session.get_selected_services().selected_control_mode == dt::ControlMode::Dynamic and
        not session.secc_pause_notified) {
        set_response_code(res, dt::ResponseCode::FAILED_PauseNotAllowed);
        return res;
    }

    set_response_code(res, dt::ResponseCode::OK);
    return res;
}

void SessionStop::enter() {
    logf_debug("Enter state: SessionStop");
}

Result SessionStop::feed(Event ev) {

    if (ev != Event::V2GTP_MESSAGE) {
        return {};
    }

    const auto variant = m_ctx.pull_request();

    if (const auto req = variant->get_if<message_20::SessionStopRequest>()) {
        const auto res = handle_request(*req, m_ctx.session);

        m_ctx.respond(res);
        apply_session_stop_response(m_ctx, *req, res);

        return {};
    } else {
        logf_warning("Expected SessionStop! But code type id: %d", variant->get_type());

        // Sequence Error
        const message_20::Type req_type = variant->get_type();
        send_sequence_error(req_type, m_ctx);

        m_ctx.session_stopped = true;
        return {};
    }
}

void apply_session_stop_response(d20::Context& ctx, const message_20::SessionStopRequest& req,
                                 const message_20::SessionStopResponse& res) {
    mark_session_stop_response(ctx, req, res);

    // Todo(sl): Tell the reason why the charger is stopping. Shutdown, Error, etc.
    if (res.response_code >= dt::ResponseCode::FAILED) {
        ctx.session_stopped = true;
        ctx.pause_ctx.reset();
    } else if (req.charging_session == dt::ChargingSession::Pause) {
        ctx.session_paused = true;
        if (not ctx.pause_ctx.has_value()) {
            logf_error("Pause the session but pause_ctx has no value");
            return;
        }
        ctx.pause_ctx->selected_service_parameters = ctx.session.get_selected_services();
        ctx.pause_ctx->authorization = ctx.session.authorization;
    } else if (req.charging_session == dt::ChargingSession::Terminate) {
        ctx.session_stopped = true;
        ctx.pause_ctx.reset();
    }
}

void mark_session_stop_response(d20::Context& ctx, const message_20::SessionStopRequest& req,
                                const message_20::SessionStopResponse& res) {
    if (req.ev_termination_code.has_value()) {
        logf_info("EV termination code: %s", req.ev_termination_code->c_str());
    }
    if (req.ev_termination_explanation.has_value()) {
        logf_info("EV termination explanation: %s", req.ev_termination_explanation->c_str());
    }
    if (req.ev_termination_code.has_value() or req.ev_termination_explanation.has_value()) {
        ctx.feedback.ev_termination(req.ev_termination_code.value_or(""), req.ev_termination_explanation.value_or(""));
    }

    // Only a positive Res that ends the session anchors the CP-oscillator retain time (a
    // ServiceRenegotiation keeps the session running); a FAILED Res ends the session with
    // immediate oscillator-off + SECC-side TCP close instead. Reported once the response
    // actually hit the wire (Session::send_response).
    if (res.response_code == dt::ResponseCode::OK) {
        if (req.charging_session != dt::ChargingSession::ServiceRenegotiation) {
            ctx.session_stop_res_pending = (req.charging_session == dt::ChargingSession::Pause)
                                               ? session::feedback::SessionStopAction::Pause
                                               : session::feedback::SessionStopAction::Terminate;
        }
    } else {
        ctx.session_stop_res_pending = session::feedback::SessionStopAction::FailedTermination;
    }
}

} // namespace iso15118::d20::state
