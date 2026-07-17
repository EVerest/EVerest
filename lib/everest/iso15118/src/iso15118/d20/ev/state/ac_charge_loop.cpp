// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/d20/ev/state/ac_charge_loop.hpp>

#include <iso15118/d20/ev/state/power_delivery.hpp>
#include <iso15118/d20/ev/timeouts.hpp>

#include <iso15118/detail/d20/ev/state/ac_charge_loop.hpp>
#include <iso15118/detail/d20/ev/state/state_helper.hpp>
#include <iso15118/detail/helper.hpp>

namespace iso15118::d20::ev::state {

namespace ac_charge_loop {

namespace {
void fill_common(message_20::AC_ChargeLoopRequest& req, const RequestParams& params) {
    req.display_parameters = params.display;
    req.meter_info_requested = false;
}

// Copy target power set points from an AC control-mode response into AcTargetPower. Dynamic responses
// carry a mandatory target_active_power, scheduled responses an optional one; both share the remaining
// optional L2/L3 and reactive fields.
void assign(std::optional<dt::RationalNumber>& dst, const dt::RationalNumber& src) {
    dst = src;
}
void assign(std::optional<dt::RationalNumber>& dst, const std::optional<dt::RationalNumber>& src) {
    dst = src;
}
} // namespace

message_20::AC_ChargeLoopRequest create_dynamic_request(const RequestParams& params, bool bpt) {
    message_20::AC_ChargeLoopRequest req;
    fill_common(req, params);

    const auto fill_dynamic = [&](auto& mode) {
        mode.departure_time = params.departure_time;
        mode.target_energy_request = params.target_energy_request;
        mode.max_energy_request = params.max_energy_request;
        mode.min_energy_request = params.min_energy_request;
        mode.max_charge_power = params.max_charge_power;
        mode.min_charge_power = params.min_charge_power;
        mode.present_active_power = params.present_active_power;
        mode.present_reactive_power = params.present_reactive_power;
    };

    if (bpt) {
        auto& mode = req.control_mode.emplace<dt::BPT_Dynamic_AC_CLReqControlMode>();
        fill_dynamic(mode);
        mode.max_discharge_power = params.max_discharge_power;
        mode.min_discharge_power = params.min_discharge_power;
        mode.max_v2x_energy_request = params.max_v2x_energy_request;
        mode.min_v2x_energy_request = params.min_v2x_energy_request;
    } else {
        auto& mode = req.control_mode.emplace<dt::Dynamic_AC_CLReqControlMode>();
        fill_dynamic(mode);
    }

    return req;
}

message_20::AC_ChargeLoopRequest create_scheduled_request(const RequestParams& params, bool bpt) {
    message_20::AC_ChargeLoopRequest req;
    fill_common(req, params);

    const auto fill_scheduled = [&](auto& mode) {
        mode.target_energy_request = params.target_energy_request;
        mode.max_energy_request = params.max_energy_request;
        mode.min_energy_request = params.min_energy_request;
        mode.max_charge_power = params.max_charge_power;
        mode.min_charge_power = params.min_charge_power;
        mode.present_active_power = params.present_active_power;
        mode.present_reactive_power = params.present_reactive_power;
    };

    if (bpt) {
        auto& mode = req.control_mode.emplace<dt::BPT_Scheduled_AC_CLReqControlMode>();
        fill_scheduled(mode);
        mode.max_discharge_power = params.max_discharge_power;
        mode.min_discharge_power = params.min_discharge_power;
    } else {
        auto& mode = req.control_mode.emplace<dt::Scheduled_AC_CLReqControlMode>();
        fill_scheduled(mode);
    }

    return req;
}

Result handle_response(const message_20::AC_ChargeLoopResponse& res) {
    Result result;
    result.valid = (res.response_code < dt::ResponseCode::FAILED);
    if (not result.valid) {
        return result;
    }

    std::visit(
        [&](const auto& mode) {
            assign(result.target.target_active_power, mode.target_active_power);
            result.target.target_active_power_L2 = mode.target_active_power_L2;
            result.target.target_active_power_L3 = mode.target_active_power_L3;
            result.target.target_reactive_power = mode.target_reactive_power;
            result.target.target_reactive_power_L2 = mode.target_reactive_power_L2;
            result.target.target_reactive_power_L3 = mode.target_reactive_power_L3;
        },
        res.control_mode);

    result.target.target_frequency = res.target_frequency;

    if (res.status.has_value()) {
        result.notification = res.status->notification;
    }

    return result;
}

} // namespace ac_charge_loop

using namespace ac_charge_loop;

namespace {

RequestParams build_params(const Context& ctx) {
    const auto& ac = ctx.session_config.ac_charge_parameters;

    RequestParams p;
    p.display.present_soc = std::nullopt;

    // Dynamic energy window (see flow spec §3 AC_ChargeLoop): target 40 kWh, max 60 kWh, min -20 kWh.
    p.departure_time = 7200;
    p.target_energy_request = dt::from_float(40000.0f);
    p.max_energy_request = dt::from_float(60000.0f);
    p.min_energy_request = dt::from_float(-20000.0f);

    // Charge limits and present powers (SIL defaults applied when unset).
    p.max_charge_power = ac.max_charge_power;
    p.min_charge_power = ac.min_charge_power;
    p.present_active_power = ac.present_active_power.value_or(dt::from_float(15000.0f));
    p.present_reactive_power = ac.present_reactive_power.value_or(dt::from_float(0.0f));

    if (is_bpt_service(ctx.evse_info.selected_energy_service)) {
        p.max_discharge_power = ac.max_discharge_power.value_or(dt::from_float(11000.0f));
        p.min_discharge_power = ac.min_discharge_power.value_or(dt::from_float(1.0f));
    }

    return p;
}

} // namespace

void AC_ChargeLoop::enter() {
    m_ctx.log.enter_state("AC_ChargeLoop");
}

void AC_ChargeLoop::send(Event) {
    const auto params = build_params(m_ctx);
    const bool bpt = is_bpt_service(m_ctx.evse_info.selected_energy_service);

    message_20::AC_ChargeLoopRequest req;
    if (m_ctx.evse_info.selected_control_mode == dt::ControlMode::Scheduled) {
        req = create_scheduled_request(params, bpt);
    } else {
        req = create_dynamic_request(params, bpt);
    }

    m_ctx.setup_header(req.header);
    m_ctx.send_request(req);
    m_ctx.start_timeout(d20::TimeoutType::SEQUENCE, MESSAGE_TIMEOUT_CHARGE_LOOP_MS);
}

d20::ev::Result AC_ChargeLoop::feed(Event ev) {
    if (ev == Event::SEND_REQUEST) {
        send(ev);
        return {};
    }

    if (ev == Event::CONTROL_MESSAGE) {
        if (const auto* stop = m_ctx.get_control_event<StopCharging>(); stop and static_cast<bool>(*stop)) {
            stop_requested = true;
            stop_reason = dt::ChargingSession::Terminate;
        } else if (const auto* pause = m_ctx.get_control_event<PauseCharging>(); pause and static_cast<bool>(*pause)) {
            stop_requested = true;
            stop_reason = dt::ChargingSession::Pause;
        }
        return {};
    }

    if (ev == Event::TIMEOUT) {
        m_ctx.log("AC_ChargeLoop message timeout reached, terminating session");
        m_ctx.session_stopped = true;
        return {};
    }

    if (ev != Event::V2GTP_MESSAGE) {
        return {};
    }

    m_ctx.stop_timeout(d20::TimeoutType::SEQUENCE);

    const auto variant = m_ctx.pull_response();

    if (const auto res = variant->get_if<message_20::AC_ChargeLoopResponse>()) {
        const auto result = handle_response(*res);

        if (not result.valid) {
            m_ctx.log("AC_ChargeLoop failed, terminating session");
            m_ctx.session_stopped = true;
            return {};
        }

        if (first_response) {
            m_ctx.feedback.signal(session::ev::feedback::Signal::CHARGE_LOOP_STARTED);
            first_response = false;
        }

        // Publish the SECC target power on every AC_ChargeLoopRes [flow spec §3/§4.3].
        m_ctx.feedback.ac_evse_target_power(result.target);

        // EVSE-driven stop/pause via EvseNotification [flow spec §3/§4.4].
        if (result.notification.has_value()) {
            if (result.notification.value() == dt::EvseNotification::Terminate) {
                m_ctx.feedback.stop_from_charger();
                m_ctx.pending_stop = dt::ChargingSession::Terminate;
                return m_ctx.create_state<PowerDelivery>(PowerDelivery::Phase::Stop);
            }
            if (result.notification.value() == dt::EvseNotification::Pause) {
                m_ctx.feedback.pause_from_charger();
                m_ctx.pending_stop = dt::ChargingSession::Pause;
                return m_ctx.create_state<PowerDelivery>(PowerDelivery::Phase::Stop);
            }
        }

        // EV-initiated stop/pause (StopCharging/PauseCharging control event).
        if (stop_requested) {
            m_ctx.pending_stop = stop_reason;
            return m_ctx.create_state<PowerDelivery>(PowerDelivery::Phase::Stop);
        }

        // Continue the charging loop (paced by the session driver).
        send(ev);
        return {};
    }

    m_ctx.log("expected AC_ChargeLoopRes! But code type id: %d", variant->get_type());
    m_ctx.session_stopped = true;
    return {};
}

} // namespace iso15118::d20::ev::state
