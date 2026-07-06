// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <iso15118/detail/helper.hpp>
#include <iso15118/ev/d20/context.hpp>
#include <iso15118/ev/d20/state/ac_charge_loop.hpp>
#include <iso15118/ev/d20/state/power_delivery.hpp>
#include <iso15118/ev/detail/d20/context_helper.hpp>
#include <iso15118/message/ac_charge_loop.hpp>

namespace iso15118::ev::d20::state {

namespace {

namespace dt = message_20::datatypes;

message_20::AC_ChargeLoopRequest make_request(const SessionId& session, const AcChargeParams& params) {
    message_20::AC_ChargeLoopRequest req;
    setup_header(req.header, session);
    req.meter_info_requested = false;
    req.display_parameters = std::nullopt;

    dt::Dynamic_AC_CLReqControlMode mode;
    mode.departure_time = std::nullopt;
    mode.target_energy_request = {0, 0};
    mode.max_energy_request = {0, 0};
    mode.min_energy_request = {0, 0};
    mode.max_charge_power = dt::from_float(params.max_charge_power);
    mode.min_charge_power = dt::from_float(params.min_charge_power);
    mode.present_active_power = dt::from_float(params.present_active_power);
    mode.present_reactive_power = {0, 0};
    if (params.three_phase) {
        mode.max_charge_power_L2 = mode.max_charge_power;
        mode.max_charge_power_L3 = mode.max_charge_power;
        mode.min_charge_power_L2 = mode.min_charge_power;
        mode.min_charge_power_L3 = mode.min_charge_power;
        mode.present_active_power_L2 = mode.present_active_power;
        mode.present_active_power_L3 = mode.present_active_power;
    }
    req.control_mode = mode;

    return req;
}

} // namespace

void AC_ChargeLoop::enter() {
    m_ctx.log.enter_state("AC_ChargeLoop");
    m_ctx.respond(make_request(m_ctx.get_session(), m_ctx.get_ac_params()));
}

Result AC_ChargeLoop::feed(Event ev) {
    if (ev != Event::V2GTP_MESSAGE) {
        return {};
    }

    const auto variant = m_ctx.pull_response();

    const auto* res = expect_response<message_20::AC_ChargeLoopResponse>(m_ctx, *variant);
    if (res == nullptr) {
        return {};
    }

    const auto* mode = std::get_if<dt::Dynamic_AC_CLResControlMode>(&res->control_mode);
    if (mode == nullptr) {
        logf_error("AC_ChargeLoopResponse offers a control mode the EV did not request");
        m_ctx.stop_session();
        // no transition; the session finishes on the stop flag
        return {};
    }

    if (res->status.has_value() and res->status->notification == dt::EvseNotification::Terminate) {
        m_ctx.feedback.stop_from_charger();
        return m_ctx.create_state<PowerDelivery>(dt::Progress::Stop);
    }

    if (m_ctx.is_stop_charging_requested()) {
        return m_ctx.create_state<PowerDelivery>(dt::Progress::Stop);
    }

    m_ctx.feedback.ac_target_power(*mode);
    m_ctx.respond(make_request(m_ctx.get_session(), m_ctx.get_ac_params()));
    return {};
}

} // namespace iso15118::ev::d20::state
