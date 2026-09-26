// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <optional>
#include <variant>

#include <iso15118/d20/ac_powers.hpp>
#include <iso15118/detail/helper.hpp>
#include <iso15118/ev/ac_phase_split.hpp>
#include <iso15118/ev/d20/context.hpp>
#include <iso15118/ev/d20/state/ac_charge_loop.hpp>
#include <iso15118/ev/d20/state/power_delivery.hpp>
#include <iso15118/ev/detail/d20/ac_target_power.hpp>
#include <iso15118/ev/detail/d20/context_helper.hpp>
#include <iso15118/ev/service_family.hpp>
#include <iso15118/message/ac_charge_loop.hpp>

namespace iso15118::ev::d20::state {

namespace {

namespace dt = message_20::datatypes;

void fill_dynamic_charge(dt::Dynamic_AC_CLReqControlMode& mode, const AcChargeParams& params,
                         dt::AcConnector connector) {
    mode.departure_time = std::nullopt;
    mode.target_energy_request = {0, 0};
    mode.max_energy_request = {0, 0};
    mode.min_energy_request = {0, 0};

    emit_ac_limit(params.max_charge_power, params.phase_count, connector, mode.max_charge_power,
                  mode.max_charge_power_L2, mode.max_charge_power_L3);
    emit_ac_limit(params.min_charge_power, params.phase_count, connector, mode.min_charge_power,
                  mode.min_charge_power_L2, mode.min_charge_power_L3);

    // Only ever the module's measurement; see the note in dc_charge_loop.cpp.
    emit_ac_present(params.present_active_power, params.phase_count, connector, mode.present_active_power,
                    mode.present_active_power_L2, mode.present_active_power_L3);

    mode.present_reactive_power = {0, 0};
}

// The energy request fields are optional in Scheduled mode and left unset; the SECC dictates the
// set point from the schedule it selected.
void fill_scheduled_charge(dt::Scheduled_AC_CLReqControlMode& mode, const AcChargeParams& params,
                           dt::AcConnector connector) {
    emit_ac_limit(params.max_charge_power, params.phase_count, connector, mode.max_charge_power,
                  mode.max_charge_power_L2, mode.max_charge_power_L3);
    emit_ac_limit(params.min_charge_power, params.phase_count, connector, mode.min_charge_power,
                  mode.min_charge_power_L2, mode.min_charge_power_L3);

    // Only ever the module's measurement; see the note in dc_charge_loop.cpp.
    emit_ac_present(params.present_active_power, params.phase_count, connector, mode.present_active_power,
                    mode.present_active_power_L2, mode.present_active_power_L3);

    mode.present_reactive_power = dt::RationalNumber{0, 0};
}

message_20::AC_ChargeLoopRequest make_request(const SessionId& session, const AcChargeParams& params,
                                              dt::ServiceCategory service, dt::AcConnector connector,
                                              dt::ControlMode control_mode) {
    message_20::AC_ChargeLoopRequest req;
    setup_header(req.header, session);
    req.meter_info_requested = false;
    req.display_parameters = std::nullopt;

    const bool bpt = is_bpt(service);

    if (control_mode == dt::ControlMode::Scheduled) {
        if (bpt) {
            dt::BPT_Scheduled_AC_CLReqControlMode mode;
            fill_scheduled_charge(mode, params, connector);
            emit_ac_limit(params.max_discharge_power, params.phase_count, connector, mode.max_discharge_power,
                          mode.max_discharge_power_L2, mode.max_discharge_power_L3);
            emit_ac_limit(params.min_discharge_power, params.phase_count, connector, mode.min_discharge_power,
                          mode.min_discharge_power_L2, mode.min_discharge_power_L3);
            req.control_mode = mode;
        } else {
            dt::Scheduled_AC_CLReqControlMode mode;
            fill_scheduled_charge(mode, params, connector);
            req.control_mode = mode;
        }
        return req;
    }

    if (bpt) {
        dt::BPT_Dynamic_AC_CLReqControlMode mode;
        fill_dynamic_charge(mode, params, connector);

        emit_ac_limit(params.max_discharge_power, params.phase_count, connector, mode.max_discharge_power,
                      mode.max_discharge_power_L2, mode.max_discharge_power_L3);
        emit_ac_limit(params.min_discharge_power, params.phase_count, connector, mode.min_discharge_power,
                      mode.min_discharge_power_L2, mode.min_discharge_power_L3);

        req.control_mode = mode;
    } else {
        dt::Dynamic_AC_CLReqControlMode mode;
        fill_dynamic_charge(mode, params, connector);
        req.control_mode = mode;
    }

    return req;
}

// Set point of a Dynamic or Scheduled (BPT or not) response; nullopt when the response carries none.
std::optional<iso15118::d20::AcTargetPower> target_power(const message_20::AC_ChargeLoopResponse& res) {
    auto target = std::visit([&](const auto& mode) { return make_ac_target_power(mode, res.target_frequency); },
                             res.control_mode);
    // Without a target power the frequency is dropped too, until a consumer needs a frequency-only target.
    if (not target.target_active_power.has_value()) {
        return std::nullopt;
    }
    return target;
}

bool mode_matches_session(const message_20::AC_ChargeLoopResponse& res, dt::ServiceCategory service,
                          dt::ControlMode control_mode) {
    const bool bpt = is_bpt(service);
    if (control_mode == dt::ControlMode::Scheduled) {
        return bpt ? std::holds_alternative<dt::BPT_Scheduled_AC_CLResControlMode>(res.control_mode)
                   : std::holds_alternative<dt::Scheduled_AC_CLResControlMode>(res.control_mode);
    }
    return bpt ? std::holds_alternative<dt::BPT_Dynamic_AC_CLResControlMode>(res.control_mode)
               : std::holds_alternative<dt::Dynamic_AC_CLResControlMode>(res.control_mode);
}

} // namespace

void AC_ChargeLoop::enter() {
    logf_debug("Enter state: AC_ChargeLoop");
    m_ctx.send_request(make_request(m_ctx.get_session(), m_ctx.get_ac_params(), m_ctx.selected_service(),
                                    m_ctx.ac_connector(), m_ctx.selected_control_mode()));
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

    if (not mode_matches_session(*res, m_ctx.selected_service(), m_ctx.selected_control_mode())) {
        logf_error("AC_ChargeLoopResponse offers a control mode the EV did not request");
        m_ctx.stop_session();
        // no transition; the session finishes on the stop flag
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

    if (const auto target = target_power(*res)) {
        m_ctx.feedback.ac_target_power(*target);
    }
    m_ctx.send_request(make_request(m_ctx.get_session(), m_ctx.get_ac_params(), m_ctx.selected_service(),
                                    m_ctx.ac_connector(), m_ctx.selected_control_mode()));
    return Result::awaiting();
}

} // namespace iso15118::ev::d20::state
