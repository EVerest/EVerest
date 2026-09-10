// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <iso15118/ev/d2/state/charge_parameter_discovery.hpp>

#include <iso15118/detail/helper.hpp>
#include <iso15118/ev/d2/state/cable_check.hpp>
#include <iso15118/ev/d2/state/power_delivery.hpp>
#include <iso15118/ev/detail/d2/context_helper.hpp>
#include <iso15118/ev/detail/d2/state/charge_parameter_discovery.hpp>
#include <iso15118/ev/detail/d2/state/state_helper.hpp>

namespace iso15118::ev::d2::state {

namespace charge_parameter_discovery {

using dt::from_physical_value;
using dt::to_physical_value;
using dt::Unit;

message_2::ChargeParameterDiscoveryRequest create_dc_request(dt::EnergyTransferMode mode,
                                                             const DcChargeParams& params) {
    message_2::ChargeParameterDiscoveryRequest req;
    req.requested_energy_transfer_mode = mode;
    req.max_entries_sa_schedule_tuple = MAX_ENTRIES_SA_SCHEDULE_TUPLE;

    auto& dc = req.dc_ev_charge_parameter.emplace();
    // [V2G2-478] mirror of DIN: the EV is not yet ready to take power at parameter discovery.
    dc.dc_ev_status = make_dc_ev_status(params, false);
    dc.ev_maximum_current_limit = to_physical_value(params.max_charge_current, Unit::A);
    dc.ev_maximum_voltage_limit = to_physical_value(params.max_voltage, Unit::V);
    if (params.max_charge_power > 0.0f) {
        dc.ev_maximum_power_limit = to_physical_value(params.max_charge_power, Unit::W);
    }
    dc.ev_energy_capacity = to_physical_value(params.energy_capacity, Unit::Wh);
    return req;
}

message_2::ChargeParameterDiscoveryRequest create_ac_request(dt::EnergyTransferMode mode, const Iso2AcParams& params) {
    message_2::ChargeParameterDiscoveryRequest req;
    req.requested_energy_transfer_mode = mode;
    req.max_entries_sa_schedule_tuple = MAX_ENTRIES_SA_SCHEDULE_TUPLE;

    auto& ac = req.ac_ev_charge_parameter.emplace();
    ac.e_amount = to_physical_value(params.e_amount, Unit::Wh);
    ac.ev_max_voltage = to_physical_value(params.ev_max_voltage, Unit::V);
    ac.ev_max_current = to_physical_value(params.ev_max_current, Unit::A);
    ac.ev_min_current = to_physical_value(params.ev_min_current, Unit::A);
    return req;
}

Result handle_response(const message_2::ChargeParameterDiscoveryResponse& res) {
    Result result;
    // The response code is validated by expect_response; only EVSEProcessing and the offer decide here.
    result.valid = true;
    result.finished = (res.evse_processing == dt::EVSEProcessing::Finished);

    if (res.sa_schedule_list.has_value() and not res.sa_schedule_list->empty()) {
        const auto& tuple = res.sa_schedule_list->front();
        result.sa_schedule_tuple_id = tuple.sa_schedule_tuple_id;
        result.selected_pmax_schedule = tuple.pmax_schedule;
    } else if (result.finished) {
        // [V2G2-286]/[V2G2-773]: the following PowerDeliveryReq must reference a SAScheduleTupleID from
        // this response. A Finished response without a usable SAScheduleList leaves no legal way on.
        result.valid = false;
    }

    if (res.dc_evse_charge_parameter.has_value()) {
        const auto& dc = res.dc_evse_charge_parameter.value();
        session::feedback::DcMaximumLimits limits;
        limits.voltage = static_cast<float>(from_physical_value(dc.evse_maximum_voltage_limit));
        limits.current = static_cast<float>(from_physical_value(dc.evse_maximum_current_limit));
        limits.power = static_cast<float>(from_physical_value(dc.evse_maximum_power_limit));
        result.dc_limits = limits;
    }

    if (res.ac_evse_charge_parameter.has_value()) {
        result.ac_nominal_voltage = res.ac_evse_charge_parameter->evse_nominal_voltage;
    }

    return result;
}

} // namespace charge_parameter_discovery

void ChargeParameterDiscovery::send() {
    const auto mode = m_ctx.params().energy_transfer_mode;
    m_ctx.send_request(is_dc_mode(mode) ? charge_parameter_discovery::create_dc_request(mode, m_ctx.get_dc_params())
                                        : charge_parameter_discovery::create_ac_request(mode, m_ctx.params().ac));
}

void ChargeParameterDiscovery::enter() {
    logf_debug("Enter state: ChargeParameterDiscovery (ISO 15118-2)");
    send();
}

Result ChargeParameterDiscovery::feed(Event ev) {
    if (ev != Event::V2GTP_MESSAGE) {
        return Result::ignored();
    }

    const auto variant = m_ctx.pull_response();
    const auto* res = expect_response<message_2::ChargeParameterDiscoveryResponse>(m_ctx, *variant);
    if (res == nullptr) {
        return Result::stopping();
    }

    const auto result = charge_parameter_discovery::handle_response(*res);

    if (auto stop = stop_before_start(m_ctx)) {
        return std::move(*stop);
    }

    if (not result.finished) {
        send();
        return Result::awaiting();
    }

    if (not result.valid) {
        logf_error("ChargeParameterDiscoveryRes is Finished without a usable SAScheduleList; stopping the session");
        m_ctx.stop_session();
        return Result::stopping();
    }

    // Charge parameters settled: the EV is ready to take power, before any PowerDelivery.
    m_ctx.feedback.ev_power_ready();

    if (result.sa_schedule_tuple_id.has_value()) {
        m_ctx.evse_info.sa_schedule_tuple_id = result.sa_schedule_tuple_id.value();
    }
    m_ctx.evse_info.selected_pmax_schedule = result.selected_pmax_schedule;

    const bool is_dc = is_dc_mode(m_ctx.params().energy_transfer_mode);
    if (is_dc and result.dc_limits.has_value()) {
        m_ctx.feedback.dc_evse_present_limits(result.dc_limits.value());
    }
    if (result.ac_nominal_voltage.has_value()) {
        m_ctx.evse_info.ac_nominal_voltage = result.ac_nominal_voltage;
    }

    if (is_dc) {
        return m_ctx.create_state<CableCheck>();
    }
    return m_ctx.create_state<PowerDelivery>(PowerDelivery::Phase::Start);
}

} // namespace iso15118::ev::d2::state
