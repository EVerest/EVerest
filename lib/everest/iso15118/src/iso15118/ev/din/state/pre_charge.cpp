// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <iso15118/ev/din/state/pre_charge.hpp>

#include <cmath>

#include <iso15118/detail/helper.hpp>
#include <iso15118/ev/detail/din/context_helper.hpp>
#include <iso15118/ev/detail/din/state/pre_charge.hpp>
#include <iso15118/ev/detail/din/state/state_helper.hpp>
#include <iso15118/ev/din/state/power_delivery.hpp>

namespace iso15118::ev::din::state {

namespace pre_charge {

namespace {

// Absolute cap on top of the +/- 10 % band so high target voltages still require a tight match.
constexpr double ABSOLUTE_TOLERANCE_V = 20.0;

} // namespace

message_din::PreChargeRequest create_request(const dt::DcEvStatus& dc_ev_status, double target_voltage,
                                             double target_current) {
    message_din::PreChargeRequest req;
    req.dc_ev_status = dc_ev_status;
    req.ev_target_voltage = target_voltage;
    req.ev_target_current = target_current;
    return req;
}

bool converged(const message_din::PreChargeResponse& res, double target_voltage) {
    if (target_voltage <= 0.0) {
        return false;
    }
    const auto delta = std::fabs(res.evse_present_voltage - target_voltage);
    return (delta <= target_voltage * 0.10) and (delta <= ABSOLUTE_TOLERANCE_V);
}

} // namespace pre_charge

namespace {

void send(Context& ctx) {
    const auto params = ctx.get_dc_params();
    // Josev parity: pre-charge ramps voltage with a target current of 0 A.
    ctx.send_request(pre_charge::create_request(make_dc_ev_status(params, true), params.target_voltage, 0.0));
}

} // namespace

void PreCharge::enter() {
    logf_debug("Enter state: PreCharge (DIN 70121)");
    send(m_ctx);
}

Result PreCharge::feed(Event ev) {
    if (ev != Event::V2GTP_MESSAGE) {
        return Result::ignored();
    }

    const auto variant = m_ctx.pull_response();

    const auto* res = expect_response<message_din::PreChargeResponse>(m_ctx, *variant);
    if (res == nullptr) {
        return Result::stopping();
    }

    if (auto stop = stop_before_start(m_ctx)) {
        return std::move(*stop);
    }

    const auto target_voltage = m_ctx.get_dc_params().target_voltage;
    if (target_voltage <= 0.0 and not nonpositive_target_warned) {
        nonpositive_target_warned = true;
        logf_warning("PreCharge target voltage is %.1f V; the loop cannot converge until the module raises it",
                     target_voltage);
    }

    if (pre_charge::converged(*res, target_voltage)) {
        // Published immediately before PowerDeliveryReq(ready=true).
        m_ctx.feedback.dc_power_on();
        return m_ctx.create_state<PowerDelivery>(PowerDelivery::Phase::Start);
    }

    // Not converged: resend and stay. The SECC keeps answering pre-charge requests while its
    // converter ramps; the session owns the ongoing guard.
    send(m_ctx);
    return Result::awaiting();
}

} // namespace iso15118::ev::din::state
