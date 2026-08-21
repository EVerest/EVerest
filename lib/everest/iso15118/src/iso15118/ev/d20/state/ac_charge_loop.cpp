// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <iso15118/detail/helper.hpp>
#include <iso15118/ev/ac_phase_split.hpp>
#include <iso15118/ev/d20/context.hpp>
#include <iso15118/ev/d20/state/ac_charge_loop.hpp>
#include <iso15118/ev/d20/state/power_delivery.hpp>
#include <iso15118/ev/detail/d20/context_helper.hpp>
#include <iso15118/message/ac_charge_loop.hpp>

namespace iso15118::ev::d20::state {

namespace {

namespace dt = message_20::datatypes;

void assign(std::optional<dt::RationalNumber>& out, const std::optional<float>& value) {
    out = value.has_value() ? std::make_optional(dt::from_float(*value)) : std::nullopt;
}

void fill_dynamic_charge(dt::Dynamic_AC_CLReqControlMode& mode, const AcChargeParams& params,
                         dt::AcConnector connector) {
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

    // Only ever the module's measurement; see the note in dc_charge_loop.cpp. It is a single
    // aggregate reading, so it is split the same way rather than invented per line.
    const auto present = split_ac_limit(params.present_active_power, params.phase_count, connector);
    mode.present_active_power = dt::from_float(present.base);
    assign(mode.present_active_power_L2, present.l2);
    assign(mode.present_active_power_L3, present.l3);

    mode.present_reactive_power = {0, 0};
}

message_20::AC_ChargeLoopRequest make_request(const SessionId& session, const AcChargeParams& params,
                                              dt::ServiceCategory service, dt::AcConnector connector) {
    message_20::AC_ChargeLoopRequest req;
    setup_header(req.header, session);
    req.meter_info_requested = false;
    req.display_parameters = std::nullopt;

    if (service == dt::ServiceCategory::AC_BPT) {
        dt::BPT_Dynamic_AC_CLReqControlMode mode;
        fill_dynamic_charge(mode, params, connector);

        const auto max_discharge = split_ac_limit(params.max_discharge_power, params.phase_count, connector);
        mode.max_discharge_power = dt::from_float(max_discharge.base);
        assign(mode.max_discharge_power_L2, max_discharge.l2);
        assign(mode.max_discharge_power_L3, max_discharge.l3);

        const auto min_discharge = split_ac_limit(params.min_discharge_power, params.phase_count, connector);
        mode.min_discharge_power = dt::from_float(min_discharge.base);
        assign(mode.min_discharge_power_L2, min_discharge.l2);
        assign(mode.min_discharge_power_L3, min_discharge.l3);

        req.control_mode = mode;
    } else {
        dt::Dynamic_AC_CLReqControlMode mode;
        fill_dynamic_charge(mode, params, connector);
        req.control_mode = mode;
    }

    return req;
}

} // namespace

void AC_ChargeLoop::enter() {
    logf_debug("Enter state: AC_ChargeLoop");
    m_ctx.send_request(make_request(m_ctx.get_session(), m_ctx.get_ac_params(), m_ctx.selected_service(),
                                    m_ctx.selected_ac_connector().value_or(dt::AcConnector::SinglePhase)));
}

Result AC_ChargeLoop::feed(Event ev) {
    if (ev != Event::V2GTP_MESSAGE) {
        return Result::ignored();
    }

    const auto variant = m_ctx.pull_response();

    const auto* res = expect_response<message_20::AC_ChargeLoopResponse>(m_ctx, *variant);
    if (res == nullptr) {
        return Result::stopping();
    }

    const dt::Dynamic_AC_CLResControlMode* mode =
        (m_ctx.selected_service() == dt::ServiceCategory::AC_BPT)
            ? std::get_if<dt::BPT_Dynamic_AC_CLResControlMode>(&res->control_mode)
            : std::get_if<dt::Dynamic_AC_CLResControlMode>(&res->control_mode);
    if (mode == nullptr) {
        logf_error("AC_ChargeLoopResponse offers a control mode the EV did not request");
        m_ctx.stop_session();
        // no transition; the session finishes on the stop flag
        return Result::stopping();
    }

    if (res->status.has_value() and res->status->notification == dt::EvseNotification::Terminate) {
        m_ctx.feedback.stop_from_charger();
        return m_ctx.create_state<PowerDelivery>(dt::Progress::Stop);
    }

    if (m_ctx.is_stop_charging_requested()) {
        return m_ctx.create_state<PowerDelivery>(dt::Progress::Stop);
    }

    m_ctx.feedback.ac_target_power(*mode);
    m_ctx.send_request(make_request(m_ctx.get_session(), m_ctx.get_ac_params(), m_ctx.selected_service(),
                                    m_ctx.selected_ac_connector().value_or(dt::AcConnector::SinglePhase)));
    return Result::awaiting();
}

} // namespace iso15118::ev::d20::state
