// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <iso15118/ev/din/state/cable_check.hpp>

#include <iso15118/detail/helper.hpp>
#include <iso15118/ev/detail/din/context_helper.hpp>
#include <iso15118/ev/detail/din/state/cable_check.hpp>
#include <iso15118/ev/detail/din/state/state_helper.hpp>
#include <iso15118/ev/din/state/pre_charge.hpp>

namespace iso15118::ev::din::state {

namespace cable_check {

message_din::CableCheckRequest create_request(const dt::DcEvStatus& dc_ev_status) {
    message_din::CableCheckRequest req;
    req.dc_ev_status = dc_ev_status;
    return req;
}

Result handle_response(const message_din::CableCheckResponse& res) {
    Result result;
    result.finished = (res.evse_processing == dt::EvseProcessing::Finished);

    const auto status = res.dc_evse_status.evse_status_code;
    result.evse_shutdown =
        (status == dt::DcEvseStatusCode::EVSE_Shutdown or status == dt::DcEvseStatusCode::EVSE_EmergencyShutdown);
    return result;
}

} // namespace cable_check

namespace {

void send(Context& ctx) {
    ctx.send_request(cable_check::create_request(make_dc_ev_status(ctx.get_dc_params(), true)));
}

} // namespace

Result CableCheck::begin(Context& ctx) {
    // Without CP-state feedback there is nothing to wait for.
    if (ctx.has_cp_state_feedback() and not ctx.cp_state_c_or_d()) {
        return Result::waiting(ctx.create_state<CableCheck>(Entry{false}));
    }
    send(ctx);
    return ctx.create_state<CableCheck>(Entry{true});
}

void CableCheck::enter() {
    logf_debug("Enter state: CableCheck (DIN 70121)");
    // The Session's ongoing guard bounds the wait for CP state C or D.
    if (not request_sent) {
        logf_debug("CableCheck holds the first request until CP state C or D");
    }
}

Result CableCheck::feed(Event ev) {
    if (ev == Event::CONTROL_MESSAGE) {
        if (request_sent) {
            return Result::ignored();
        }
        // Still waiting for CP state C/D: a stop or pause ends the session without a CableCheckReq.
        if (auto stop = stop_before_start(m_ctx)) {
            return std::move(*stop);
        }
        if (not m_ctx.cp_state_c_or_d()) {
            return Result::ignored();
        }
        send(m_ctx);
        request_sent = true;
        return Result::awaiting();
    }

    if (ev != Event::V2GTP_MESSAGE) {
        return Result::ignored();
    }

    const auto variant = m_ctx.pull_response();

    if (not request_sent) {
        logf_error("CableCheck received a response before sending its request; stopping the session");
        m_ctx.stop_session();
        return Result::stopping();
    }

    const auto* res = expect_response<message_din::CableCheckResponse>(m_ctx, *variant);
    if (res == nullptr) {
        return Result::stopping();
    }

    const auto result = cable_check::handle_response(*res);
    if (result.evse_shutdown) {
        logf_error("CableCheck finished with an EVSE (emergency) shutdown, stopping the session");
        return to_session_stop(m_ctx, session::StopReason::EvseTerminate);
    }

    if (auto stop = stop_before_start(m_ctx)) {
        return std::move(*stop);
    }

    if (result.finished) {
        return to_pre_charge(m_ctx);
    }

    // EVSEProcessing::Ongoing: re-poll. The session owns the ongoing guard.
    send(m_ctx);
    return Result::awaiting();
}

} // namespace iso15118::ev::din::state
