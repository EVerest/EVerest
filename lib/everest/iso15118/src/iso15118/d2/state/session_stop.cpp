// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/d2/state/session_stop.hpp>

#include <iso15118/detail/d2/state/sequence_error.hpp>
#include <iso15118/detail/d2/state/session_stop.hpp>
#include <iso15118/detail/d2/state/state_helper.hpp>
#include <iso15118/detail/helper.hpp>

namespace iso15118::d2::state {

namespace {
// V2G_SECC_Msg_Performance_Time(SessionStopRes) = 1,5 s (Table 109): the window
// [V2G2-920]..[V2G2-922] give the SECC to measure CP State B after a SessionStopReq.
constexpr uint32_t CP_STATE_PERFORMANCE_TIME_MS = 1500;
} // namespace

message_2::SessionStopResponse handle_request([[maybe_unused]] const message_2::SessionStopRequest& req,
                                              const dt::SessionId& session_id) {
    message_2::SessionStopResponse res;
    res.header.session_id = session_id;
    res.response_code = dt::ResponseCode::OK;
    return res;
}

void SessionStop::enter() {
    logf_debug("Enter state: SessionStop");
}

// An action the receiving state calls once it has satisfied the CP State B gate -- it never waits
// itself, because both Figure 103's SessionStop and Figure 104's welding bubble process the request.
Result process_session_stop(Context& m_ctx, const message_2::SessionStopRequest& req) {
    const auto res = handle_request(req, m_ctx.get_session_id());
    m_ctx.respond(res);

    if (req.charging_session == dt::ChargingSession::Pause) {
        // Retain the session id so the returning EV can re-join with OK_OldSessionJoined, and the payment
        // option so the resumed ServiceDiscoveryRes can offer only that one [V2G2-741].
        m_ctx.session_paused = true;
        const auto selected_option =
            m_ctx.session().contract_selected ? dt::PaymentOption::Contract : dt::PaymentOption::ExternalPayment;
        m_ctx.pause_ctx = PauseContext{m_ctx.get_session_id(), selected_option};
    } else {
        m_ctx.session_stopped = true;
        m_ctx.pause_ctx.reset();
    }

    // Reported once the response actually hit the wire (Session::send_response).
    m_ctx.session_stop_res_pending = (req.charging_session == dt::ChargingSession::Pause)
                                         ? session::feedback::SessionStopAction::Pause
                                         : session::feedback::SessionStopAction::Terminate;

    return {};
}

Result SessionStop::accept_request(const message_2::SessionStopRequest& req) {
    // The [V2G2-920] gate. A Pause ends the charge loop into sleep mode and is exempt.
    if (req.charging_session != dt::ChargingSession::Pause and cp_state_b_outstanding(m_ctx)) {
        pending_req = req;
        m_ctx.arm_cp_state_timeout(CP_STATE_PERFORMANCE_TIME_MS);
        return {};
    }

    return process_session_stop(m_ctx, req);
}

Result SessionStop::on_event(Event ev) {
    if (ev == Event::CONTROL_MESSAGE) {
        // Parked while waiting for CP State B ([V2G2-920]..[V2G2-922]): resume as soon as it arrives.
        if (pending_req.has_value() and m_ctx.evse().current_cp_state == d20::CpState::B) {
            m_ctx.stop_timeout(d20::TimeoutType::CPSTATE);
            const auto req = pending_req.value();
            pending_req.reset();
            return process_session_stop(m_ctx, req);
        }
        return {};
    }

    if (ev == Event::TIMEOUT) {
        const auto* timeout = m_ctx.get_active_timeout();
        if (timeout and *timeout == d20::TimeoutType::CPSTATE and pending_req.has_value()) {
            // [V2G2-922]: no CP State B in time, so respond FAILED and end the session. The FAILED response
            // arms the FailedTermination path (oscillator off + SECC-side TCP close).
            logf_warning("no CP State B within V2G_SECC_Msg_Performance_Time, SessionStop -> FAILED");
            pending_req.reset();
            auto res = handle_request(message_2::SessionStopRequest{}, m_ctx.get_session_id());
            res.response_code = dt::ResponseCode::FAILED;
            m_ctx.respond(res);
            m_ctx.session_stopped = true;
        }
        return {};
    }

    return {};
}

Result SessionStop::on_request(const message_2::Variant& received) {
    // [V2G2-568]: only a SessionStopReq is in sequence here.
    const auto type = received.get_type();
    if (type == message_2::Type::SessionStopReq) {
        return accept_request(received.get<message_2::SessionStopRequest>());
    } else {
        logf_warning("Expected SessionStopReq! But got type id: %d", received.get_type());
        respond_sequence_error(m_ctx, received.get_type());
        return {};
    }
}

} // namespace iso15118::d2::state
