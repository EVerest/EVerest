// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/d2/ev/state/charge_parameter_discovery.hpp>

#include <iso15118/d2/ev/state/cable_check.hpp>
#include <iso15118/d2/ev/state/power_delivery.hpp>
#include <iso15118/d2/ev/timeouts.hpp>

#include <iso15118/detail/d2/ev/state/charge_parameter_discovery.hpp>
#include <iso15118/detail/d2/ev/state/state_helper.hpp>
#include <iso15118/detail/helper.hpp>

namespace iso15118::d2::ev::state {

namespace charge_parameter_discovery {

using dt::from_physical_value;
using dt::to_physical_value;
using dt::Unit;

message_2::ChargeParameterDiscoveryRequest create_dc_request(const EvSessionConfig& config,
                                                             const dt::DC_EVStatus& dc_ev_status) {
    message_2::ChargeParameterDiscoveryRequest req;
    req.requested_energy_transfer_mode = config.requested_energy_transfer_mode;

    auto& dc = req.dc_ev_charge_parameter.emplace();
    dc.dc_ev_status = dc_ev_status;
    dc.ev_maximum_current_limit = to_physical_value(config.dc_ev_max_current, Unit::A);
    dc.ev_maximum_voltage_limit = to_physical_value(config.dc_ev_max_voltage, Unit::V);
    if (config.dc_ev_max_power.has_value()) {
        dc.ev_maximum_power_limit = to_physical_value(config.dc_ev_max_power.value(), Unit::W);
    }
    dc.ev_energy_capacity = to_physical_value(config.dc_energy_capacity, Unit::Wh);
    if (config.dc_energy_request.has_value()) {
        dc.ev_energy_request = to_physical_value(config.dc_energy_request.value(), Unit::Wh);
    }
    dc.full_soc = config.dc_full_soc;
    dc.bulk_soc = config.dc_bulk_soc;

    return req;
}

message_2::ChargeParameterDiscoveryRequest create_ac_request(const EvSessionConfig& config) {
    message_2::ChargeParameterDiscoveryRequest req;
    req.requested_energy_transfer_mode = config.requested_energy_transfer_mode;

    auto& ac = req.ac_ev_charge_parameter.emplace();
    ac.e_amount = to_physical_value(config.ac_e_amount, Unit::Wh);
    ac.ev_max_voltage = to_physical_value(config.ac_ev_max_voltage, Unit::V);
    ac.ev_max_current = to_physical_value(config.ac_ev_max_current, Unit::A);
    ac.ev_min_current = to_physical_value(config.ac_ev_min_current, Unit::A);

    return req;
}

Result handle_response(const message_2::ChargeParameterDiscoveryResponse& res) {
    Result result;
    result.valid = (res.response_code < dt::ResponseCode::FAILED);
    if (not result.valid) {
        return result;
    }

    result.finished = (res.evse_processing == dt::EVSEProcessing::Finished);

    if (res.sa_schedule_list.has_value() and not res.sa_schedule_list->empty()) {
        const auto& tuple = res.sa_schedule_list->front();
        result.sa_schedule_tuple_id = tuple.sa_schedule_tuple_id;
        result.selected_pmax_schedule = tuple.pmax_schedule;
        if (not tuple.pmax_schedule.empty()) {
            result.selected_pmax_entry = tuple.pmax_schedule.front();
        }
    }

    if (res.dc_evse_charge_parameter.has_value()) {
        const auto& dc = res.dc_evse_charge_parameter.value();
        session::ev::feedback::DcMaximumLimits limits;
        limits.voltage = static_cast<float>(from_physical_value(dc.evse_maximum_voltage_limit));
        limits.current = static_cast<float>(from_physical_value(dc.evse_maximum_current_limit));
        limits.power = static_cast<float>(from_physical_value(dc.evse_maximum_power_limit));
        result.dc_limits = limits;
    }

    if (res.ac_evse_charge_parameter.has_value()) {
        const auto& ac = res.ac_evse_charge_parameter.value();
        result.ac_nominal_voltage = ac.evse_nominal_voltage;
        result.ac_max_current = ac.evse_max_current;
    }

    return result;
}

} // namespace charge_parameter_discovery

using namespace charge_parameter_discovery;

void ChargeParameterDiscovery::enter() {
    m_ctx.log.enter_state("ChargeParameterDiscovery");
}

void ChargeParameterDiscovery::send(Event) {
    if (first_request) {
        m_ctx.start_timeout(d20::TimeoutType::ONGOING, TIMEOUT_ONGOING_MS);
        first_request = false;
    }

    const auto mode = m_ctx.session_config.requested_energy_transfer_mode;
    auto req = is_ac_mode(mode) ? create_ac_request(m_ctx.session_config)
                                : create_dc_request(m_ctx.session_config, make_dc_ev_status(m_ctx, true));
    m_ctx.setup_header(req.header);
    m_ctx.send_request(req);
    m_ctx.start_timeout(d20::TimeoutType::SEQUENCE, MESSAGE_TIMEOUT_MS);
}

d2::ev::Result ChargeParameterDiscovery::feed(Event ev) {
    if (ev == Event::SEND_REQUEST) {
        send(ev);
        return {};
    }

    if (ev == Event::CONTROL_MESSAGE) {
        handle_stop_control_event(m_ctx);
        return {};
    }

    if (ev == Event::TIMEOUT) {
        const auto* timeout = m_ctx.get_active_timeout();
        if (timeout and *timeout == d20::TimeoutType::ONGOING) {
            m_ctx.log("ChargeParameterDiscovery ongoing timeout reached, terminating session");
            ongoing_timeout_reached = true;
        } else {
            m_ctx.log("ChargeParameterDiscovery message timeout reached, terminating session");
        }
        m_ctx.session_stopped = true;
        return {};
    }

    if (ev != Event::V2GTP_MESSAGE) {
        return {};
    }

    m_ctx.stop_timeout(d20::TimeoutType::SEQUENCE);

    const auto variant = m_ctx.pull_response();

    if (const auto res = variant->get_if<message_2::ChargeParameterDiscoveryResponse>()) {
        const auto result = handle_response(*res);

        if (not result.valid) {
            m_ctx.log("ChargeParameterDiscovery failed, terminating session");
            m_ctx.session_stopped = true;
            return {};
        }

        if (not result.finished) {
            if (ongoing_timeout_reached) {
                m_ctx.session_stopped = true;
                return {};
            }
            send(ev);
            return {};
        }

        m_ctx.stop_timeout(d20::TimeoutType::ONGOING);

        // Publish ev_power_ready(true) once the charge parameters are settled, before any PowerDelivery
        // and before dc_power_on (Josev parity: ev_power_ready -> dc_power_on) [flow spec §4].
        m_ctx.feedback.ev_power_ready(true);

        if (result.sa_schedule_tuple_id.has_value()) {
            m_ctx.evse_info.sa_schedule_tuple_id = result.sa_schedule_tuple_id.value();
        }
        m_ctx.evse_info.selected_pmax_entry = result.selected_pmax_entry;
        m_ctx.evse_info.selected_pmax_schedule = result.selected_pmax_schedule;

        const bool is_dc = is_dc_mode(m_ctx.session_config.requested_energy_transfer_mode);

        if (is_dc and result.dc_limits.has_value()) {
            m_ctx.evse_info.dc_present_limits = result.dc_limits;
            m_ctx.feedback.dc_evse_present_limits(result.dc_limits.value());
        }
        if (result.ac_nominal_voltage.has_value()) {
            m_ctx.evse_info.ac_nominal_voltage = result.ac_nominal_voltage;
        }
        if (result.ac_max_current.has_value()) {
            m_ctx.evse_info.ac_max_current = result.ac_max_current;
        }

        if (auto stop = stop_if_pending(m_ctx)) {
            return stop;
        }

        if (is_dc) {
            return m_ctx.create_state<CableCheck>();
        }
        return m_ctx.create_state<PowerDelivery>(PowerDelivery::Phase::Start);
    }

    m_ctx.log("expected ChargeParameterDiscoveryRes! But code type id: %d", variant->get_type());
    m_ctx.session_stopped = true;
    return {};
}

} // namespace iso15118::d2::ev::state
