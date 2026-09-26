// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <bitset>

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

// Charge and present-power elements are shared by both DER control modes.
template <typename Mode> void fill_der_common(Mode& mode, const AcChargeParams& params, dt::AcConnector connector) {
    emit_ac_limit(params.max_charge_power, params.phase_count, connector, mode.max_charge_power,
                  mode.max_charge_power_L2, mode.max_charge_power_L3);
    emit_ac_limit(params.min_charge_power, params.phase_count, connector, mode.min_charge_power,
                  mode.min_charge_power_L2, mode.min_charge_power_L3);

    // Only ever the module's measurement; see the note in dc_charge_loop.cpp.
    emit_ac_present(params.present_active_power, params.phase_count, connector, mode.present_active_power,
                    mode.present_active_power_L2, mode.present_active_power_L3);

    emit_ac_limit(params.max_discharge_power, params.phase_count, connector, mode.max_discharge_power,
                  mode.max_discharge_power_L2, mode.max_discharge_power_L3);
    emit_ac_limit(params.min_discharge_power, params.phase_count, connector, mode.min_discharge_power,
                  mode.min_discharge_power_L2, mode.min_discharge_power_L3);

    mode.grid_event_condition = 0;
}

// [V2G20-3192]: neither side sends parameters outside the functions the SECC enabled. A setpoint
// for a function the EV does not support cannot be acted on, so it is dropped rather than obeyed.
template <typename Mode>
Mode without_unsupported_setpoints(const Mode& res_mode, std::bitset<ev::DER_CONTROL_FUNCTION_COUNT> supported) {
    auto directive = res_mode;
    if (directive.dso_q_setpoint.has_value() and
        not supported.test(static_cast<size_t>(iec::DERControlName::DSOQSetpointProvision))) {
        logf_warning("DER response carries a DSO Q setpoint the EV does not support; ignoring it");
        directive.dso_q_setpoint.reset();
    }
    if (directive.dso_cos_phi_setpoint.has_value() and
        not supported.test(static_cast<size_t>(iec::DERControlName::DSOCosPhiSetpointProvision))) {
        logf_warning("DER response carries a DSO cos phi setpoint the EV does not support; ignoring it");
        directive.dso_cos_phi_setpoint.reset();
    }
    return directive;
}

message_20::DER_AC_ChargeLoopRequest make_request(const SessionId& session, const AcChargeParams& params,
                                                  dt::AcConnector connector, dt::ControlMode control_mode) {
    message_20::DER_AC_ChargeLoopRequest req;
    setup_header(req.header, session);
    req.meter_info_requested = false;
    req.display_parameters = std::nullopt;

    if (control_mode == dt::ControlMode::Scheduled) {
        // The energy request fields are optional in Scheduled mode and left unset; the SECC
        // dictates the set point from the schedule it selected.
        dt::DER_Scheduled_AC_CLReqControlMode mode;
        fill_der_common(mode, params, connector);
        mode.present_reactive_power = dt::RationalNumber{0, 0};
        req.control_mode = mode;
        return req;
    }

    dt::DER_Dynamic_AC_CLReqControlMode mode;
    mode.departure_time = std::nullopt;
    mode.target_energy_request = {0, 0};
    mode.max_energy_request = {0, 0};
    mode.min_energy_request = {0, 0};
    fill_der_common(mode, params, connector);
    mode.present_reactive_power = {0, 0};
    req.control_mode = mode;

    return req;
}

} // namespace

void AC_DER_IEC_ChargeLoop::enter() {
    logf_debug("Enter state: AC_DER_IEC_ChargeLoop");
    m_ctx.send_request(
        make_request(m_ctx.get_session(), m_ctx.get_ac_params(), m_ctx.ac_connector(), m_ctx.selected_control_mode()));
}

Result AC_DER_IEC_ChargeLoop::feed(Event ev) {
    if (ev != Event::V2GTP_MESSAGE) {
        return Result::ignored();
    }

    const auto variant = m_ctx.pull_response();

    const auto* res = expect_response<message_20::DER_AC_ChargeLoopResponse>(m_ctx, *variant);
    if (res == nullptr) {
        return Result::stopping();
    }

    const bool scheduled = (m_ctx.selected_control_mode() == dt::ControlMode::Scheduled);
    if (scheduled != std::holds_alternative<dt::DER_Scheduled_AC_CLResControlMode>(res->control_mode)) {
        logf_error("DER_AC_ChargeLoopResponse offers a control mode the EV did not request");
        m_ctx.stop_session();
        return Result::stopping();
    }

    if (res->status.has_value()) {
        if (res->status->notification == dt::EvseNotification::Terminate) {
            m_ctx.feedback.stop_from_charger();
            return m_ctx.create_state<PowerDelivery>(dt::Progress::Stop);
        }
        if (res->status->notification == dt::EvseNotification::Pause) {
            m_ctx.feedback.pause_from_charger();
            // SessionStop carries Pause, so the session id may be re-joined.
            m_ctx.set_pause_charging_requested(true);
            return m_ctx.create_state<PowerDelivery>(dt::Progress::Stop);
        }
    }

    if (m_ctx.is_stop_charging_requested() or m_ctx.is_pause_charging_requested()) {
        return m_ctx.create_state<PowerDelivery>(dt::Progress::Stop);
    }

    const auto supported = m_ctx.der_supported_functions();
    if (scheduled) {
        m_ctx.feedback.der_control_scheduled(without_unsupported_setpoints(
            std::get<dt::DER_Scheduled_AC_CLResControlMode>(res->control_mode), supported));
    } else {
        m_ctx.feedback.der_control(
            without_unsupported_setpoints(std::get<dt::DER_Dynamic_AC_CLResControlMode>(res->control_mode), supported));
    }

    m_ctx.send_request(
        make_request(m_ctx.get_session(), m_ctx.get_ac_params(), m_ctx.ac_connector(), m_ctx.selected_control_mode()));
    return Result::awaiting();
}

} // namespace iso15118::ev::d20::state
