// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <iso15118/ev/din/state/welding_detection.hpp>

#include <iso15118/detail/helper.hpp>
#include <iso15118/ev/detail/din/context_helper.hpp>
#include <iso15118/ev/detail/din/state/state_helper.hpp>
#include <iso15118/ev/detail/din/state/welding_detection.hpp>

namespace iso15118::ev::din::state {

namespace welding_detection {

message_din::WeldingDetectionRequest create_request(const dt::DcEvStatus& dc_ev_status) {
    message_din::WeldingDetectionRequest req;
    req.dc_ev_status = dc_ev_status;
    return req;
}

} // namespace welding_detection

namespace {

void send(Context& ctx) {
    ctx.send_request(welding_detection::create_request(make_dc_ev_status(ctx.get_dc_params(), false)));
}

} // namespace

void WeldingDetection::enter() {
    logf_debug("Enter state: WeldingDetection (DIN 70121)");
    send(m_ctx);
}

Result WeldingDetection::feed(Event ev) {
    if (ev != Event::V2GTP_MESSAGE) {
        return Result::ignored();
    }

    const auto variant = m_ctx.pull_response();

    const auto* res = expect_response<message_din::WeldingDetectionResponse>(m_ctx, *variant);
    if (res == nullptr) {
        return Result::stopping();
    }

    ++cycles;
    const bool voltage_safe = res->evse_present_voltage < welding_detection::WELDING_DETECTION_SAFE_VOLTAGE_V;
    if (voltage_safe or cycles >= welding_detection::WELDING_DETECTION_CYCLES) {
        return m_ctx.create_state<SessionStop>();
    }

    send(m_ctx);
    return Result::awaiting();
}

} // namespace iso15118::ev::din::state
