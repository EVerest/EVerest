// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <iso15118/detail/helper.hpp>
#include <iso15118/ev/d20/context.hpp>
#include <iso15118/ev/d20/state/dc_charge_loop.hpp>
#include <iso15118/ev/d20/state/power_delivery.hpp>
#include <iso15118/ev/detail/d20/context_helper.hpp>
#include <iso15118/message/dc_charge_loop.hpp>

namespace iso15118::ev::d20::state {

namespace {

namespace dt = message_20::datatypes;

void fill_dynamic_charge(dt::Dynamic_DC_CLReqControlMode& mode, const DcChargeParams& params) {
    mode.departure_time = std::nullopt;
    mode.target_energy_request = dt::from_float(params.energy_capacity);
    mode.max_energy_request = dt::from_float(params.energy_capacity);
    mode.min_energy_request = {0, 0};
    mode.max_charge_power = dt::from_float(params.max_charge_power);
    mode.min_charge_power = {0, 0};
    mode.max_charge_current = dt::from_float(params.max_charge_current);
    mode.max_voltage = dt::from_float(params.max_voltage);
    mode.min_voltage = dt::from_float(params.min_voltage);
}

// Approximation, not a measurement: the EV has no present-voltage source of its own yet
// and the field is mandatory on the wire, where a zero reads as a measured zero. Prefer a
// module-fed value, else the voltage the SECC last reported, else the EV's target.
float approximate_present_voltage(const DcChargeParams& params, std::optional<float> reported_by_evse) {
    if (params.present_voltage != 0.0f) {
        return params.present_voltage;
    }
    return reported_by_evse.value_or(params.target_voltage);
}

message_20::DC_ChargeLoopRequest make_request(const SessionId& session, const DcChargeParams& params,
                                              dt::ServiceCategory service,
                                              std::optional<float> present_voltage_reported_by_evse) {
    message_20::DC_ChargeLoopRequest req;
    setup_header(req.header, session);
    req.meter_info_requested = false;
    req.display_parameters = std::nullopt;
    req.present_voltage = dt::from_float(approximate_present_voltage(params, present_voltage_reported_by_evse));

    if (service == dt::ServiceCategory::DC_BPT) {
        dt::BPT_Dynamic_DC_CLReqControlMode mode;
        fill_dynamic_charge(mode, params);
        mode.max_discharge_power = dt::from_float(params.max_discharge_power);
        mode.min_discharge_power = dt::from_float(params.min_discharge_power);
        mode.max_discharge_current = dt::from_float(params.max_discharge_current);
        req.control_mode = mode;
    } else {
        dt::Dynamic_DC_CLReqControlMode mode;
        fill_dynamic_charge(mode, params);
        req.control_mode = mode;
    }

    return req;
}

} // namespace

void DC_ChargeLoop::enter() {
    logf_debug("Enter state: DC_ChargeLoop");
    m_ctx.send_request(make_request(m_ctx.get_session(), m_ctx.get_dc_params(), m_ctx.selected_service(),
                                    m_ctx.evse_present_voltage()));
}

Result DC_ChargeLoop::feed(Event ev) {
    if (ev != Event::V2GTP_MESSAGE) {
        return {};
    }

    const auto variant = m_ctx.pull_response();

    const auto* res = expect_response<message_20::DC_ChargeLoopResponse>(m_ctx, *variant);
    if (res == nullptr) {
        return {};
    }

    const bool mode_matches_session =
        (m_ctx.selected_service() == dt::ServiceCategory::DC_BPT)
            ? std::holds_alternative<dt::BPT_Dynamic_DC_CLResControlMode>(res->control_mode)
            : std::holds_alternative<dt::Dynamic_DC_CLResControlMode>(res->control_mode);
    if (not mode_matches_session) {
        logf_error("DC_ChargeLoopResponse offers a control mode the EV did not request");
        m_ctx.stop_session();
        // no transition; the session finishes on the stop flag
        return {};
    }

    m_ctx.set_evse_present_voltage(dt::from_RationalNumber(res->present_voltage));

    if (res->status.has_value() and res->status->notification == dt::EvseNotification::Terminate) {
        m_ctx.feedback.stop_from_charger();
        return m_ctx.create_state<PowerDelivery>(dt::Progress::Stop);
    }

    if (m_ctx.is_stop_charging_requested()) {
        return m_ctx.create_state<PowerDelivery>(dt::Progress::Stop);
    }

    m_ctx.send_request(make_request(m_ctx.get_session(), m_ctx.get_dc_params(), m_ctx.selected_service(),
                                    m_ctx.evse_present_voltage()));
    return {};
}

} // namespace iso15118::ev::d20::state
