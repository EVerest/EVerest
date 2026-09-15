// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <iso15118/ev/d2/state/welding_detection.hpp>

#include <iso15118/detail/helper.hpp>
#include <iso15118/ev/d2/state/session_stop.hpp>
#include <iso15118/ev/detail/d2/context_helper.hpp>
#include <iso15118/ev/detail/d2/state/state_helper.hpp>
#include <iso15118/ev/detail/d2/state/welding_detection.hpp>

namespace iso15118::ev::d2::state {

namespace welding_detection {

message_2::WeldingDetectionRequest create_request(const dt::DC_EVStatus& dc_ev_status) {
    message_2::WeldingDetectionRequest req;
    req.dc_ev_status = dc_ev_status;
    return req;
}

bool should_finish(int cycles, float present_voltage) {
    return (present_voltage < SAFE_VOLTAGE_V) or (cycles >= CYCLES);
}

} // namespace welding_detection

void WeldingDetection::enter() {
    logf_debug("Enter state: WeldingDetection (ISO 15118-2)");
    // Charging has ended; the EV is no longer ready to take power (DIN mirror).
    m_ctx.send_request(welding_detection::create_request(make_dc_ev_status(m_ctx.get_dc_params(), false)));
}

Result WeldingDetection::feed(Event ev) {
    if (ev != Event::V2GTP_MESSAGE) {
        return Result::ignored();
    }

    const auto variant = m_ctx.pull_response();
    const auto* res = expect_response<message_2::WeldingDetectionResponse>(m_ctx, *variant);
    if (res == nullptr) {
        return Result::stopping();
    }

    ++cycles;
    const auto present_voltage = static_cast<float>(dt::from_physical_value(res->evse_present_voltage));
    if (welding_detection::should_finish(cycles, present_voltage)) {
        return m_ctx.create_state<SessionStop>();
    }

    m_ctx.send_request(welding_detection::create_request(make_dc_ev_status(m_ctx.get_dc_params(), false)));
    return Result::awaiting();
}

} // namespace iso15118::ev::d2::state
