// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <optional>
#include <variant>

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

// Scheduled mode dictates a set point instead of an energy window: the limits stay, the energy
// request fields do not exist here.
void fill_scheduled_charge(dt::Scheduled_DC_CLReqControlMode& mode, const DcChargeParams& params) {
    mode.target_voltage = dt::from_float(params.target_voltage);
    mode.target_current = dt::from_float(params.target_current);
    mode.max_charge_power = dt::from_float(params.max_charge_power);
    mode.min_charge_power = dt::RationalNumber{0, 0};
    mode.max_charge_current = dt::from_float(params.max_charge_current);
    mode.max_voltage = dt::from_float(params.max_voltage);
    mode.min_voltage = dt::from_float(params.min_voltage);
}

message_20::DC_ChargeLoopRequest make_request(const SessionId& session, const DcChargeParams& params,
                                              dt::ServiceCategory service, dt::ControlMode control_mode) {
    message_20::DC_ChargeLoopRequest req;
    setup_header(req.header, session);
    req.meter_info_requested = false;
    req.display_parameters = std::nullopt;
    // Only ever the module's measurement: substituting the EV's target or the SECC's
    // own reading would be indistinguishable from it on the wire.
    req.present_voltage = dt::from_float(params.present_voltage);

    const bool bpt = (service == dt::ServiceCategory::DC_BPT);

    if (control_mode == dt::ControlMode::Scheduled) {
        if (bpt) {
            dt::BPT_Scheduled_DC_CLReqControlMode mode;
            fill_scheduled_charge(mode, params);
            mode.max_discharge_power = dt::from_float(params.max_discharge_power);
            mode.min_discharge_power = dt::from_float(params.min_discharge_power);
            mode.max_discharge_current = dt::from_float(params.max_discharge_current);
            req.control_mode = mode;
        } else {
            dt::Scheduled_DC_CLReqControlMode mode;
            fill_scheduled_charge(mode, params);
            req.control_mode = mode;
        }
        return req;
    }

    if (bpt) {
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

bool mode_matches_session(const message_20::DC_ChargeLoopResponse& res, dt::ServiceCategory service,
                          dt::ControlMode control_mode) {
    const bool bpt = (service == dt::ServiceCategory::DC_BPT);
    if (control_mode == dt::ControlMode::Scheduled) {
        return bpt ? std::holds_alternative<dt::BPT_Scheduled_DC_CLResControlMode>(res.control_mode)
                   : std::holds_alternative<dt::Scheduled_DC_CLResControlMode>(res.control_mode);
    }
    return bpt ? std::holds_alternative<dt::BPT_Dynamic_DC_CLResControlMode>(res.control_mode)
               : std::holds_alternative<dt::Dynamic_DC_CLResControlMode>(res.control_mode);
}

void assign_limit(float& target, const dt::RationalNumber& value, bool& any) {
    target = dt::from_RationalNumber(value);
    any = true;
}

void assign_limit(float& target, const std::optional<dt::RationalNumber>& value, bool& any) {
    if (value.has_value()) {
        assign_limit(target, *value, any);
    }
}

// SECC limits carried by the response; the Scheduled modes make all three optional.
std::optional<feedback::DcMaximumLimits> evse_present_limits(const message_20::DC_ChargeLoopResponse& res) {
    return std::visit(
        [](const auto& mode) -> std::optional<feedback::DcMaximumLimits> {
            feedback::DcMaximumLimits limits{};
            bool any = false;
            assign_limit(limits.voltage, mode.max_voltage, any);
            assign_limit(limits.current, mode.max_charge_current, any);
            assign_limit(limits.power, mode.max_charge_power, any);
            if (not any) {
                return std::nullopt;
            }
            return limits;
        },
        res.control_mode);
}

} // namespace

void DC_ChargeLoop::enter() {
    logf_debug("Enter state: DC_ChargeLoop");
    m_ctx.send_request(make_request(m_ctx.get_session(), m_ctx.get_dc_params(), m_ctx.selected_service(),
                                    m_ctx.selected_control_mode()));
}

Result DC_ChargeLoop::feed(Event ev) {
    if (ev != Event::V2GTP_MESSAGE) {
        return Result::ignored();
    }

    const auto variant = m_ctx.pull_response();

    const auto* res = expect_response<message_20::DC_ChargeLoopResponse>(m_ctx, *variant);
    if (res == nullptr) {
        return Result::stopping();
    }

    if (not mode_matches_session(*res, m_ctx.selected_service(), m_ctx.selected_control_mode())) {
        logf_error("DC_ChargeLoopResponse offers a control mode the EV did not request");
        m_ctx.stop_session();
        // no transition; the session finishes on the stop flag
        return Result::stopping();
    }

    if (const auto limits = evse_present_limits(*res)) {
        m_ctx.feedback.dc_evse_present_limits(*limits);
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

    m_ctx.send_request(make_request(m_ctx.get_session(), m_ctx.get_dc_params(), m_ctx.selected_service(),
                                    m_ctx.selected_control_mode()));
    return Result::awaiting();
}

} // namespace iso15118::ev::d20::state
