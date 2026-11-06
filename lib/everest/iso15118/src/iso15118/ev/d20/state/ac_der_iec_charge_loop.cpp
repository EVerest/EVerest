// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <bitset>
#include <utility>

#include <iso15118/d20/der_functions.hpp>
#include <iso15118/detail/helper.hpp>
#include <iso15118/ev/d20/ac_phase_split.hpp>
#include <iso15118/ev/d20/context.hpp>
#include <iso15118/ev/d20/der/iec_functions.hpp>
#include <iso15118/ev/d20/discharge_sign.hpp>
#include <iso15118/ev/d20/state/ac_der_iec_charge_loop.hpp>
#include <iso15118/ev/d20/state/leave_charge_loop.hpp>
#include <iso15118/ev/detail/d20/ac_target_power.hpp>
#include <iso15118/ev/detail/d20/context_helper.hpp>
#include <iso15118/message/ac_der_iec_charge_loop.hpp>

namespace iso15118::ev::d20::state {

namespace {

namespace dt = message_20::datatypes;

// Charge and present-power elements are shared by both DER control modes.
template <typename Mode>
void fill_der_common(Mode& mode, const params::AcChargeParams& params, dt::AcConnector connector) {
    emit_ac_limit(params.max_charge_power, params.phase_count, connector, mode.max_charge_power,
                  mode.max_charge_power_L2, mode.max_charge_power_L3);
    emit_ac_limit(params.min_charge_power, params.phase_count, connector, mode.min_charge_power,
                  mode.min_charge_power_L2, mode.min_charge_power_L3);

    // Only ever the module's measurement; see the note in dc_charge_loop.cpp.
    emit_ac_present(params.present_active_power, params.phase_count, connector, mode.present_active_power,
                    mode.present_active_power_L2, mode.present_active_power_L3);

    emit_ac_discharge_limits(params, connector, mode);

    mode.grid_event_condition = 0;
}

// [V2G20-3192]: neither side sends parameters outside the functions the SECC enabled. A setpoint
// for a function the EV does not support cannot be acted on, so it is dropped rather than obeyed.
template <typename Mode>
Mode without_unsupported_setpoints(const Mode& res_mode,
                                   std::bitset<ev::d20::der::DER_CONTROL_FUNCTION_COUNT> supported) {
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

} // namespace

message_20::DER_AC_ChargeLoopRequest AC_DER_IEC_ChargeLoop::make_request(const params::AcChargeParams& params,
                                                                         dt::AcConnector connector,
                                                                         dt::ControlMode control_mode,
                                                                         const EnergyRequest& energy) {
    message_20::DER_AC_ChargeLoopRequest req;
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
    fill_energy_request(mode, energy);
    fill_der_common(mode, params, connector);
    mode.present_reactive_power = {0, 0};
    req.control_mode = mode;

    return req;
}

message_20::DER_AC_ChargeLoopRequest AC_DER_IEC_ChargeLoop::make_request(const Context& ctx) {
    return make_request(ctx.get_ac_params(), ctx.ac_connector(), ctx.selected_control_mode(), ctx.energy_request());
}

void AC_DER_IEC_ChargeLoop::enter() {
    logf_debug("Enter state: AC_DER_IEC_ChargeLoop");
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

    if (auto stop = leave_charge_loop(m_ctx, res->status)) {
        return std::move(*stop);
    }

    const auto supported = m_ctx.der_supported_functions();
    if (scheduled) {
        m_ctx.feedback.der_control_scheduled(without_unsupported_setpoints(
            std::get<dt::DER_Scheduled_AC_CLResControlMode>(res->control_mode), supported));
    } else {
        const auto& mode = std::get<dt::DER_Dynamic_AC_CLResControlMode>(res->control_mode);
        m_ctx.feedback.ac_target_power(make_ac_target_power(mode, res->target_frequency));
        m_ctx.feedback.der_control(without_unsupported_setpoints(mode, supported));
    }

    m_ctx.send_request(make_request(m_ctx));
    return Result::awaiting();
}

} // namespace iso15118::ev::d20::state
