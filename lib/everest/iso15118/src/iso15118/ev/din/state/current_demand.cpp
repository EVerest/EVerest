// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <iso15118/ev/din/state/current_demand.hpp>

#include <optional>

#include <iso15118/detail/helper.hpp>
#include <iso15118/ev/detail/din/context_helper.hpp>
#include <iso15118/ev/detail/din/state/current_demand.hpp>
#include <iso15118/ev/detail/din/state/state_helper.hpp>
#include <iso15118/ev/din/state/power_delivery.hpp>

namespace iso15118::ev::din::state {

namespace current_demand {

message_din::CurrentDemandRequest create_request(const RequestParams& params) {
    message_din::CurrentDemandRequest req;
    req.dc_ev_status = params.dc_ev_status;
    req.ev_target_voltage = params.target_voltage;
    req.ev_target_current = params.target_current;
    req.ev_maximum_voltage_limit = params.max_voltage_limit;
    req.ev_maximum_current_limit = params.max_current_limit;
    req.ev_maximum_power_limit = params.max_power_limit;
    req.charging_complete = params.charging_complete;
    return req;
}

bool charger_requests_stop(const message_din::CurrentDemandResponse& res) {
    if (res.dc_evse_status.evse_notification == dt::EvseNotification::StopCharging) {
        return true;
    }
    const auto status = res.dc_evse_status.evse_status_code;
    return status == dt::DcEvseStatusCode::EVSE_Shutdown or status == dt::DcEvseStatusCode::EVSE_EmergencyShutdown;
}

} // namespace current_demand

namespace {

void send(Context& ctx) {
    const auto params = ctx.get_dc_params();

    current_demand::RequestParams request_params;
    request_params.dc_ev_status = make_dc_ev_status(params, true);
    request_params.target_voltage = params.target_voltage;
    request_params.target_current = params.target_current;
    request_params.max_voltage_limit = params.max_voltage;
    request_params.max_current_limit = params.max_charge_current;
    // Optional in DIN; an unset module value is left out rather than advertised as 0.
    if (params.max_charge_power > 0.0f) {
        request_params.max_power_limit = params.max_charge_power;
    }
    request_params.charging_complete = charging_complete(params);

    ctx.send_request(current_demand::create_request(request_params));
}

// The SECC limits carried by the response, merged onto the ones discovered so far. All three are
// optional in DIN.
std::optional<feedback::DcMaximumLimits> present_limits(const message_din::CurrentDemandResponse& res,
                                                        const std::optional<feedback::DcMaximumLimits>& known) {
    if (not res.evse_maximum_voltage_limit.has_value() and not res.evse_maximum_current_limit.has_value() and
        not res.evse_maximum_power_limit.has_value()) {
        return std::nullopt;
    }

    auto limits = known.value_or(feedback::DcMaximumLimits{});
    if (res.evse_maximum_voltage_limit.has_value()) {
        limits.voltage = static_cast<float>(res.evse_maximum_voltage_limit.value());
    }
    if (res.evse_maximum_current_limit.has_value()) {
        limits.current = static_cast<float>(res.evse_maximum_current_limit.value());
    }
    if (res.evse_maximum_power_limit.has_value()) {
        limits.power = static_cast<float>(res.evse_maximum_power_limit.value());
    }
    return limits;
}

} // namespace

void CurrentDemand::enter() {
    logf_debug("Enter state: CurrentDemand (DIN 70121)");
    send(m_ctx);
}

Result CurrentDemand::feed(Event ev) {
    if (ev != Event::V2GTP_MESSAGE) {
        return Result::ignored();
    }

    const auto variant = m_ctx.pull_response();

    const auto* res = expect_response<message_din::CurrentDemandResponse>(m_ctx, *variant);
    if (res == nullptr) {
        return Result::stopping();
    }

    if (const auto limits = present_limits(*res, m_ctx.evse_info.dc_present_limits)) {
        m_ctx.evse_info.dc_present_limits = limits;
        m_ctx.feedback.dc_evse_present_limits(limits.value());
    }

    if (current_demand::charger_requests_stop(*res)) {
        m_ctx.feedback.stop_from_charger();
        // A charger stop terminates the session; it never pauses it.
        m_ctx.set_stop_charging_requested(true);
        return m_ctx.create_state<PowerDelivery>(PowerDelivery::Phase::Stop);
    }

    // An EV stop or pause leaves the loop; SessionStop decides which of the two it is.
    if (m_ctx.is_stop_charging_requested() or m_ctx.is_pause_charging_requested()) {
        return m_ctx.create_state<PowerDelivery>(PowerDelivery::Phase::Stop);
    }

    send(m_ctx);
    return Result::awaiting();
}

} // namespace iso15118::ev::din::state
