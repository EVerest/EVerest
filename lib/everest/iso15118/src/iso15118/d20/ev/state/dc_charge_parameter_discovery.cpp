// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/d20/ev/state/dc_charge_parameter_discovery.hpp>

#include <iso15118/d20/ev/state/schedule_exchange.hpp>
#include <iso15118/d20/ev/timeouts.hpp>

#include <iso15118/detail/d20/ev/state/dc_charge_parameter_discovery.hpp>
#include <iso15118/detail/d20/ev/state/state_helper.hpp>
#include <iso15118/detail/helper.hpp>

namespace iso15118::d20::ev::state {

namespace dc_charge_parameter_discovery {

namespace {
void fill_common(dt::DC_CPDReqEnergyTransferMode& mode, const DcEvChargeParameters& params) {
    mode.max_charge_power = params.max_charge_power;
    mode.min_charge_power = params.min_charge_power;
    mode.max_charge_current = params.max_charge_current;
    mode.min_charge_current = params.min_charge_current;
    mode.max_voltage = params.max_voltage;
    mode.min_voltage = params.min_voltage;
}

session::ev::feedback::DcMaximumLimits extract_limits(const dt::DC_CPDResEnergyTransferMode& mode) {
    session::ev::feedback::DcMaximumLimits limits;
    limits.power = dt::from_RationalNumber(mode.max_charge_power);
    limits.current = dt::from_RationalNumber(mode.max_charge_current);
    limits.voltage = dt::from_RationalNumber(mode.max_voltage);
    return limits;
}
} // namespace

message_20::DC_ChargeParameterDiscoveryRequest create_request(const DcEvChargeParameters& params) {
    message_20::DC_ChargeParameterDiscoveryRequest req;
    auto& mode = req.transfer_mode.emplace<dt::DC_CPDReqEnergyTransferMode>();
    fill_common(mode, params);
    return req;
}

message_20::DC_ChargeParameterDiscoveryRequest create_bpt_request(const DcEvBptChargeParameters& params) {
    message_20::DC_ChargeParameterDiscoveryRequest req;
    auto& mode = req.transfer_mode.emplace<dt::BPT_DC_CPDReqEnergyTransferMode>();
    fill_common(mode, params);
    mode.max_discharge_power = params.max_discharge_power;
    mode.min_discharge_power = params.min_discharge_power;
    mode.max_discharge_current = params.max_discharge_current;
    mode.min_discharge_current = params.min_discharge_current;
    return req;
}

Result handle_response(const message_20::DC_ChargeParameterDiscoveryResponse& res) {
    Result result;
    result.valid = (res.response_code < dt::ResponseCode::FAILED);
    if (not result.valid) {
        return result;
    }

    if (const auto* mode = std::get_if<dt::BPT_DC_CPDResEnergyTransferMode>(&res.transfer_mode)) {
        result.limits = extract_limits(*mode);
    } else if (const auto* mode = std::get_if<dt::DC_CPDResEnergyTransferMode>(&res.transfer_mode)) {
        result.limits = extract_limits(*mode);
    }

    return result;
}

} // namespace dc_charge_parameter_discovery

using namespace dc_charge_parameter_discovery;

void DC_ChargeParameterDiscovery::enter() {
    m_ctx.log.enter_state("DC_ChargeParameterDiscovery");
}

d20::ev::Result DC_ChargeParameterDiscovery::feed(Event ev) {
    if (ev == Event::SEND_REQUEST) {
        const bool bpt = is_bpt_service(m_ctx.evse_info.selected_energy_service);
        message_20::DC_ChargeParameterDiscoveryRequest req;
        if (bpt and m_ctx.session_config.dc_bpt_charge_parameters.has_value()) {
            req = create_bpt_request(m_ctx.session_config.dc_bpt_charge_parameters.value());
        } else {
            req = create_request(m_ctx.session_config.dc_charge_parameters);
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
        m_ctx.log("DC_ChargeParameterDiscovery message timeout reached, terminating session");
        m_ctx.session_stopped = true;
        return {};
    }

    if (ev != Event::V2GTP_MESSAGE) {
        return {};
    }

    m_ctx.stop_timeout(d20::TimeoutType::SEQUENCE);

    const auto variant = m_ctx.pull_response();

    if (const auto res = variant->get_if<message_20::DC_ChargeParameterDiscoveryResponse>()) {
        const auto result = handle_response(*res);

        if (not result.valid) {
            m_ctx.log("DC_ChargeParameterDiscovery failed, terminating session");
            m_ctx.session_stopped = true;
            return {};
        }

        m_ctx.evse_info.dc_present_limits = result.limits;
        m_ctx.feedback.dc_evse_present_limits(result.limits);

        if (auto stop = stop_if_pending(m_ctx)) {
            return stop;
        }
        return m_ctx.create_state<ScheduleExchange>();
    }

    m_ctx.log("expected DC_ChargeParameterDiscoveryRes! But code type id: %d", variant->get_type());
    m_ctx.session_stopped = true;
    return {};
}

} // namespace iso15118::d20::ev::state
