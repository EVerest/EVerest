// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <iso15118/ev/d2/state/pre_charge.hpp>

#include <cmath>

#include <iso15118/detail/helper.hpp>
#include <iso15118/ev/d2/state/power_delivery.hpp>
#include <iso15118/ev/detail/d2/context_helper.hpp>
#include <iso15118/ev/detail/d2/state/pre_charge.hpp>
#include <iso15118/ev/detail/d2/state/state_helper.hpp>

namespace iso15118::ev::d2::state {

namespace pre_charge {

message_2::PreChargeRequest create_request(const dt::DC_EVStatus& dc_ev_status, float target_voltage) {
    message_2::PreChargeRequest req;
    req.dc_ev_status = dc_ev_status;
    req.ev_target_voltage = dt::to_physical_value(target_voltage, dt::Unit::V);
    req.ev_target_current = dt::to_physical_value(0.0f, dt::Unit::A);
    return req;
}

bool converged(const message_2::PreChargeResponse& res, float target_voltage) {
    if (target_voltage <= 0.0f) {
        return false;
    }
    const auto present_voltage = static_cast<float>(dt::from_physical_value(res.evse_present_voltage));
    const bool within_band = (present_voltage > target_voltage * 0.9f) and (present_voltage < target_voltage * 1.1f);
    const bool within_abs = std::fabs(present_voltage - target_voltage) <= ABS_VOLTAGE_TOLERANCE_V;
    return within_band and within_abs;
}

} // namespace pre_charge

void PreCharge::enter() {
    logf_debug("Enter state: PreCharge (ISO 15118-2)");
    const auto params = m_ctx.get_dc_params();
    m_ctx.send_request(pre_charge::create_request(make_dc_ev_status(params, true), params.target_voltage));
}

Result PreCharge::feed(Event ev) {
    if (ev != Event::V2GTP_MESSAGE) {
        return Result::ignored();
    }

    const auto variant = m_ctx.pull_response();
    const auto* res = expect_response<message_2::PreChargeResponse>(m_ctx, *variant);
    if (res == nullptr) {
        return Result::stopping();
    }

    const auto params = m_ctx.get_dc_params();
    if (params.target_voltage <= 0.0f and not nonpositive_target_warned) {
        nonpositive_target_warned = true;
        logf_warning("PreCharge target voltage is %.1f V; the loop cannot converge until the module raises it",
                     static_cast<double>(params.target_voltage));
    }

    if (not pre_charge::converged(*res, params.target_voltage)) {
        // The SECC keeps answering pre-charge requests while its converter ramps; the Session's ongoing
        // guard bounds the loop.
        m_ctx.send_request(pre_charge::create_request(make_dc_ev_status(params, true), params.target_voltage));
        return Result::awaiting();
    }

    if (auto stop = stop_before_start(m_ctx)) {
        return std::move(*stop);
    }

    // Published immediately before PowerDeliveryReq(Start).
    m_ctx.feedback.dc_power_on();
    return m_ctx.create_state<PowerDelivery>(PowerDelivery::Phase::Start);
}

} // namespace iso15118::ev::d2::state
