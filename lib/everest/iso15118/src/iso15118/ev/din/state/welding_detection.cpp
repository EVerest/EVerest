// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <iso15118/ev/din/state/welding_detection.hpp>

#include <iso15118/detail/helper.hpp>
#include <iso15118/ev/detail/din/context_helper.hpp>
#include <iso15118/ev/detail/din/state/state_helper.hpp>
#include <iso15118/ev/detail/din/state/welding_detection.hpp>
#include <iso15118/ev/din/state/session_stop.hpp>

namespace iso15118::ev::din::state {

namespace welding_detection {

message_din::WeldingDetectionRequest create_request(const dt::DcEvStatus& dc_ev_status) {
    message_din::WeldingDetectionRequest req;
    req.dc_ev_status = dc_ev_status;
    return req;
}

} // namespace welding_detection

namespace {

// Charging has ended; the EV is no longer ready to take power.
void send(Context& ctx) {
    ctx.send_request(welding_detection::create_request(make_dc_ev_status(ctx.get_dc_params(), false)));
}

} // namespace

BasePointerType to_welding_detection(Context& ctx, session::StopReason stop_reason) {
    send(ctx);
    return ctx.create_state<WeldingDetection>(stop_reason);
}

void WeldingDetection::enter() {
    logf_debug("Enter state: WeldingDetection (DIN 70121)");
}

Result WeldingDetection::feed(Event ev) {
    if (ev == Event::RESPONSE_TIMEOUT) {
        return progress.response_timeout<Result>(m_ctx);
    }
    // A poll still queued is dropped and fails the check. One already sent is judged when its answer
    // arrives, against the bound this deadline shares.
    if (ev == Event::WELDING_TIMEOUT and m_ctx.has_request()) {
        m_ctx.discard_request();
        return to_session_stop(m_ctx, progress.report_failure(m_ctx));
    }
    if (ev != Event::V2GTP_MESSAGE) {
        return Result::ignored();
    }

    const auto variant = m_ctx.pull_response();

    const auto* res = expect_response<message_din::WeldingDetectionResponse>(m_ctx, *variant);
    if (res == nullptr) {
        return Result::stopping();
    }

    switch (progress.record(res->evse_present_voltage, m_ctx.welding_detection)) {
    case welding_detection::Outcome::Safe:
        return to_session_stop(m_ctx, m_stop_reason);
    case welding_detection::Outcome::Failed:
        return to_session_stop(m_ctx, progress.report_failure(m_ctx));
    case welding_detection::Outcome::Continue:
        break;
    }

    send(m_ctx);
    return Result::awaiting();
}

} // namespace iso15118::ev::din::state
