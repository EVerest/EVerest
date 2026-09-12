// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <iso15118/ev/din/state/charge_parameter_discovery.hpp>

#include <iso15118/detail/helper.hpp>
#include <iso15118/ev/detail/din/context_helper.hpp>
#include <iso15118/ev/detail/din/state/charge_parameter_discovery.hpp>
#include <iso15118/ev/detail/din/state/state_helper.hpp>
#include <iso15118/ev/din/state/cable_check.hpp>

namespace iso15118::ev::din::state {

namespace charge_parameter_discovery {

namespace {

session::feedback::DcMaximumLimits extract_limits(const dt::DcEvseChargeParameter& param) {
    session::feedback::DcMaximumLimits limits;
    limits.current = static_cast<float>(param.evse_maximum_current_limit);
    limits.voltage = static_cast<float>(param.evse_maximum_voltage_limit);
    // DIN makes EVSEMaximumPowerLimit optional; derive it from the current and voltage limits.
    limits.power = static_cast<float>(
        param.evse_maximum_power_limit.value_or(param.evse_maximum_current_limit * param.evse_maximum_voltage_limit));
    return limits;
}

bool evse_requests_stop(const dt::DcEvseStatus& status) {
    if (status.evse_notification == dt::EvseNotification::StopCharging) {
        return true;
    }
    switch (status.evse_status_code) {
    case dt::DcEvseStatusCode::EVSE_Shutdown:
    case dt::DcEvseStatusCode::EVSE_EmergencyShutdown:
        return true;
    default:
        return false;
    }
}

} // namespace

message_din::ChargeParameterDiscoveryRequest create_request(dt::EnergyTransferMode requested_energy_transfer_type,
                                                            const dt::DcEvChargeParameter& dc_ev_charge_parameter) {
    message_din::ChargeParameterDiscoveryRequest req;
    req.ev_requested_energy_transfer_type = requested_energy_transfer_type;
    req.dc_ev_charge_parameter = dc_ev_charge_parameter;
    return req;
}

Result handle_response(const message_din::ChargeParameterDiscoveryResponse& res) {
    Result result;
    result.finished = (res.evse_processing == dt::EvseProcessing::Finished);
    if (not result.finished) {
        return result;
    }

    if (res.dc_evse_charge_parameter.has_value()) {
        const auto& param = res.dc_evse_charge_parameter.value();
        result.limits = extract_limits(param);
        result.evse_stopping = evse_requests_stop(param.dc_evse_status);
    }
    return result;
}

} // namespace charge_parameter_discovery

namespace {

void send(Context& ctx) {
    const auto params = ctx.get_dc_params();

    dt::DcEvChargeParameter dc_param;
    dc_param.dc_ev_status = make_dc_ev_status(params, false);
    dc_param.ev_maximum_current_limit = params.max_charge_current;
    dc_param.ev_maximum_voltage_limit = params.max_voltage;
    // Both fields are optional in DIN; an unset module value is left out rather than advertised as 0.
    if (params.max_charge_power > 0.0f) {
        dc_param.ev_maximum_power_limit = params.max_charge_power;
    }
    if (params.energy_capacity > 0.0f) {
        dc_param.ev_energy_capacity = params.energy_capacity;
    }

    ctx.send_request(charge_parameter_discovery::create_request(
        din_energy_transfer_mode(ctx.params().energy_transfer_mode), dc_param));
}

} // namespace

void ChargeParameterDiscovery::enter() {
    logf_debug("Enter state: ChargeParameterDiscovery (DIN 70121)");
    send(m_ctx);
}

Result ChargeParameterDiscovery::feed(Event ev) {
    if (ev != Event::V2GTP_MESSAGE) {
        return Result::ignored();
    }

    const auto variant = m_ctx.pull_response();

    const auto* res = expect_response<message_din::ChargeParameterDiscoveryResponse>(m_ctx, *variant);
    if (res == nullptr) {
        return Result::stopping();
    }

    const auto result = charge_parameter_discovery::handle_response(*res);
    if (result.finished) {
        if (result.limits.has_value()) {
            m_ctx.evse_info.dc_present_limits = result.limits;
            m_ctx.feedback.dc_evse_present_limits(result.limits.value());
        }
    }

    if (auto stop = stop_before_start(m_ctx)) {
        return std::move(*stop);
    }

    if (not result.finished) {
        // EVSEProcessing::Ongoing: re-poll. The session owns the ongoing guard.
        send(m_ctx);
        return Result::awaiting();
    }

    if (result.evse_stopping) {
        logf_warning("ChargeParameterDiscovery finished with a SECC stop request, transitioning to SessionStop");
        return m_ctx.create_state<SessionStop>();
    }

    m_ctx.feedback.ev_power_ready();
    return m_ctx.create_state<CableCheck>();
}

} // namespace iso15118::ev::din::state
