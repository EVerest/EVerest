// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/d20/ev/state/ac_charge_parameter_discovery.hpp>

#include <iso15118/d20/ev/state/schedule_exchange.hpp>
#include <iso15118/d20/ev/timeouts.hpp>

#include <iso15118/detail/d20/ev/state/ac_charge_parameter_discovery.hpp>
#include <iso15118/detail/d20/ev/state/state_helper.hpp>
#include <iso15118/detail/helper.hpp>

namespace iso15118::d20::ev::state {

namespace ac_charge_parameter_discovery {

namespace {
void fill_common(dt::AC_CPDReqEnergyTransferMode& mode, const AcEvChargeParameters& params) {
    mode.max_charge_power = params.max_charge_power;
    mode.max_charge_power_L2 = params.max_charge_power_L2;
    mode.max_charge_power_L3 = params.max_charge_power_L3;
    mode.min_charge_power = params.min_charge_power;
    mode.min_charge_power_L2 = params.min_charge_power_L2;
    mode.min_charge_power_L3 = params.min_charge_power_L3;
}

AcMaximumLimits extract_limits(const dt::AC_CPDResEnergyTransferMode& mode) {
    AcMaximumLimits limits;
    limits.charge_power = dt::from_RationalNumber(mode.max_charge_power);
    if (mode.max_charge_power_L2.has_value()) {
        limits.charge_power_L2 = dt::from_RationalNumber(mode.max_charge_power_L2.value());
    }
    if (mode.max_charge_power_L3.has_value()) {
        limits.charge_power_L3 = dt::from_RationalNumber(mode.max_charge_power_L3.value());
    }
    return limits;
}

AcMaximumLimits extract_limits(const dt::BPT_AC_CPDResEnergyTransferMode& mode) {
    auto limits = extract_limits(static_cast<const dt::AC_CPDResEnergyTransferMode&>(mode));
    limits.discharge_power = dt::from_RationalNumber(mode.max_discharge_power);
    return limits;
}
} // namespace

message_20::AC_ChargeParameterDiscoveryRequest create_request(const AcEvChargeParameters& params) {
    message_20::AC_ChargeParameterDiscoveryRequest req;
    auto& mode = req.transfer_mode.emplace<dt::AC_CPDReqEnergyTransferMode>();
    fill_common(mode, params);
    return req;
}

message_20::AC_ChargeParameterDiscoveryRequest create_bpt_request(const AcEvChargeParameters& params) {
    message_20::AC_ChargeParameterDiscoveryRequest req;
    auto& mode = req.transfer_mode.emplace<dt::BPT_AC_CPDReqEnergyTransferMode>();
    fill_common(mode, params);
    // BPT discharge limits (SIL defaults applied when unset) [flow spec §3 AC branch].
    mode.max_discharge_power = params.max_discharge_power.value_or(dt::from_float(11000.0f));
    mode.min_discharge_power = params.min_discharge_power.value_or(dt::from_float(100.0f));
    return req;
}

Result handle_response(const message_20::AC_ChargeParameterDiscoveryResponse& res) {
    Result result;
    result.valid = (res.response_code < dt::ResponseCode::FAILED);
    if (not result.valid) {
        return result;
    }

    if (const auto* mode = std::get_if<dt::BPT_AC_CPDResEnergyTransferMode>(&res.transfer_mode)) {
        result.limits = extract_limits(*mode);
    } else if (const auto* mode = std::get_if<dt::AC_CPDResEnergyTransferMode>(&res.transfer_mode)) {
        result.limits = extract_limits(*mode);
    }

    return result;
}

} // namespace ac_charge_parameter_discovery

using namespace ac_charge_parameter_discovery;

void AC_ChargeParameterDiscovery::enter() {
    m_ctx.log.enter_state("AC_ChargeParameterDiscovery");
}

d20::ev::Result AC_ChargeParameterDiscovery::feed(Event ev) {
    if (ev == Event::SEND_REQUEST) {
        const bool bpt = is_bpt_service(m_ctx.evse_info.selected_energy_service);
        message_20::AC_ChargeParameterDiscoveryRequest req;
        if (bpt) {
            req = create_bpt_request(m_ctx.session_config.ac_charge_parameters);
        } else {
            req = create_request(m_ctx.session_config.ac_charge_parameters);
        }
        m_ctx.setup_header(req.header);
        m_ctx.send_request(req);
        m_ctx.start_timeout(d20::TimeoutType::SEQUENCE, MESSAGE_TIMEOUT_MS);
        return {};
    }

    if (ev == Event::CONTROL_MESSAGE) {
        handle_stop_control_event(m_ctx);
        return {};
    }

    if (ev == Event::TIMEOUT) {
        m_ctx.log("AC_ChargeParameterDiscovery message timeout reached, terminating session");
        m_ctx.session_stopped = true;
        return {};
    }

    if (ev != Event::V2GTP_MESSAGE) {
        return {};
    }

    m_ctx.stop_timeout(d20::TimeoutType::SEQUENCE);

    const auto variant = m_ctx.pull_response();

    if (const auto res = variant->get_if<message_20::AC_ChargeParameterDiscoveryResponse>()) {
        const auto result = handle_response(*res);

        if (not result.valid) {
            m_ctx.log("AC_ChargeParameterDiscovery failed, terminating session");
            m_ctx.session_stopped = true;
            return {};
        }

        m_ctx.evse_info.ac_present_limits = result.limits;

        if (auto stop = stop_if_pending(m_ctx)) {
            return stop;
        }
        return m_ctx.create_state<ScheduleExchange>();
    }

    m_ctx.log("expected AC_ChargeParameterDiscoveryRes! But code type id: %d", variant->get_type());
    m_ctx.session_stopped = true;
    return {};
}

} // namespace iso15118::d20::ev::state
