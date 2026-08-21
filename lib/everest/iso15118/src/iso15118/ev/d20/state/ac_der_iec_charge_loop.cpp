// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <iso15118/d20/der_functions.hpp>
#include <iso15118/detail/helper.hpp>
#include <iso15118/ev/ac_phase_split.hpp>
#include <iso15118/ev/d20/context.hpp>
#include <iso15118/ev/d20/state/ac_der_iec_charge_loop.hpp>
#include <iso15118/ev/d20/state/power_delivery.hpp>
#include <iso15118/ev/der_control_functions.hpp>
#include <iso15118/ev/detail/d20/context_helper.hpp>
#include <iso15118/message/ac_der_iec_charge_loop.hpp>

namespace iso15118::ev::d20::state {

namespace {

namespace dt = message_20::datatypes;

void assign(std::optional<dt::RationalNumber>& out, const std::optional<float>& value) {
    out = value.has_value() ? std::make_optional(dt::from_float(*value)) : std::nullopt;
}

message_20::DER_AC_ChargeLoopRequest make_request(const SessionId& session, const AcChargeParams& params,
                                                  dt::AcConnector connector) {
    message_20::DER_AC_ChargeLoopRequest req;
    setup_header(req.header, session);
    req.meter_info_requested = false;
    req.display_parameters = std::nullopt;

    dt::DER_Dynamic_AC_CLReqControlMode mode;
    mode.departure_time = std::nullopt;
    mode.target_energy_request = {0, 0};
    mode.max_energy_request = {0, 0};
    mode.min_energy_request = {0, 0};
    const auto max = split_ac_limit(params.max_charge_power, params.phase_count, connector);
    mode.max_charge_power = dt::from_float(max.base);
    assign(mode.max_charge_power_L2, max.l2);
    assign(mode.max_charge_power_L3, max.l3);

    const auto min = split_ac_limit(params.min_charge_power, params.phase_count, connector);
    mode.min_charge_power = dt::from_float(min.base);
    assign(mode.min_charge_power_L2, min.l2);
    assign(mode.min_charge_power_L3, min.l3);
    // Only ever the module's measurement; see the note in dc_charge_loop.cpp.
    const auto present = split_ac_limit(params.present_active_power, params.phase_count, connector);
    mode.present_active_power = dt::from_float(present.base);
    assign(mode.present_active_power_L2, present.l2);
    assign(mode.present_active_power_L3, present.l3);
    mode.present_reactive_power = {0, 0};
    const auto max_discharge = split_ac_limit(params.max_discharge_power, params.phase_count, connector);
    mode.max_discharge_power = dt::from_float(max_discharge.base);
    assign(mode.max_discharge_power_L2, max_discharge.l2);
    assign(mode.max_discharge_power_L3, max_discharge.l3);

    const auto min_discharge = split_ac_limit(params.min_discharge_power, params.phase_count, connector);
    mode.min_discharge_power = dt::from_float(min_discharge.base);
    assign(mode.min_discharge_power_L2, min_discharge.l2);
    assign(mode.min_discharge_power_L3, min_discharge.l3);
    mode.grid_event_condition = 0;
    req.control_mode = mode;

    return req;
}

} // namespace

void AC_DER_IEC_ChargeLoop::enter() {
    logf_debug("Enter state: AC_DER_IEC_ChargeLoop");
    m_ctx.send_request(make_request(m_ctx.get_session(), m_ctx.get_ac_params(),
                                    m_ctx.selected_ac_connector().value_or(dt::AcConnector::SinglePhase)));
}

Result AC_DER_IEC_ChargeLoop::feed(Event ev) {
    if (ev != Event::V2GTP_MESSAGE) {
        return {};
    }

    const auto variant = m_ctx.pull_response();

    const auto* res = expect_response<message_20::DER_AC_ChargeLoopResponse>(m_ctx, *variant);
    if (res == nullptr) {
        return {};
    }

    const auto* mode = std::get_if<dt::DER_Dynamic_AC_CLResControlMode>(&res->control_mode);
    if (mode == nullptr) {
        logf_error("DER_AC_ChargeLoopResponse offers a control mode the EV did not request");
        m_ctx.stop_session();
        return {};
    }

    if (res->status.has_value() and res->status->notification == dt::EvseNotification::Terminate) {
        m_ctx.feedback.stop_from_charger();
        return m_ctx.create_state<PowerDelivery>(dt::Progress::Stop);
    }

    if (m_ctx.is_stop_charging_requested()) {
        return m_ctx.create_state<PowerDelivery>(dt::Progress::Stop);
    }

    // Filter DSO setpoints the EV never negotiated: a SECC may still send them, but
    // acting on an un-negotiated setpoint is out of contract, so strip it here.
    auto directive = *mode;
    const auto negotiated = m_ctx.der_negotiated_functions();
    if (directive.dso_q_setpoint.has_value() and
        not negotiated.test(static_cast<size_t>(iec::DERControlName::DSOQSetpointProvision))) {
        logf_warning("DER response carries a DSO Q setpoint that was not negotiated; ignoring it");
        directive.dso_q_setpoint.reset();
    }
    if (directive.dso_cos_phi_setpoint.has_value() and
        not negotiated.test(static_cast<size_t>(iec::DERControlName::DSOCosPhiSetpointProvision))) {
        logf_warning("DER response carries a DSO cos phi setpoint that was not negotiated; ignoring it");
        directive.dso_cos_phi_setpoint.reset();
    }

    m_ctx.feedback.der_control(directive);
    m_ctx.send_request(make_request(m_ctx.get_session(), m_ctx.get_ac_params(),
                                    m_ctx.selected_ac_connector().value_or(dt::AcConnector::SinglePhase)));
    return {};
}

} // namespace iso15118::ev::d20::state
