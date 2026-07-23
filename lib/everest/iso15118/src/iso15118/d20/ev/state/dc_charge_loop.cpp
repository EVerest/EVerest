// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/d20/ev/state/dc_charge_loop.hpp>

#include <iso15118/d20/ev/state/power_delivery.hpp>
#include <iso15118/d20/ev/timeouts.hpp>

#include <iso15118/detail/d20/ev/state/dc_charge_loop.hpp>
#include <iso15118/detail/d20/ev/state/state_helper.hpp>
#include <iso15118/detail/helper.hpp>

namespace iso15118::d20::ev::state {

namespace dc_charge_loop {

namespace {
void fill_common(message_20::DC_ChargeLoopRequest& req, const RequestParams& params) {
    req.present_voltage = params.present_voltage;
    req.display_parameters = params.display;
    req.meter_info_requested = false;
}
} // namespace

message_20::DC_ChargeLoopRequest create_dynamic_request(const RequestParams& params, bool bpt) {
    message_20::DC_ChargeLoopRequest req;
    fill_common(req, params);

    const auto fill_dynamic = [&](auto& mode) {
        mode.target_energy_request = params.target_energy_request;
        mode.max_energy_request = params.max_energy_request;
        mode.min_energy_request = params.min_energy_request;
        mode.max_charge_power = params.max_charge_power;
        mode.min_charge_power = params.min_charge_power;
        mode.max_charge_current = params.max_charge_current;
        mode.max_voltage = params.max_voltage;
        mode.min_voltage = params.min_voltage;
    };

    if (bpt) {
        auto& mode = req.control_mode.emplace<dt::BPT_Dynamic_DC_CLReqControlMode>();
        fill_dynamic(mode);
        mode.max_discharge_power = params.max_discharge_power;
        mode.min_discharge_power = params.min_discharge_power;
        mode.max_discharge_current = params.max_discharge_current;
        mode.max_v2x_energy_request = params.max_v2x_energy_request;
        mode.min_v2x_energy_request = params.min_v2x_energy_request;
    } else {
        auto& mode = req.control_mode.emplace<dt::Dynamic_DC_CLReqControlMode>();
        fill_dynamic(mode);
    }

    return req;
}

message_20::DC_ChargeLoopRequest create_scheduled_request(const RequestParams& params, bool bpt) {
    message_20::DC_ChargeLoopRequest req;
    fill_common(req, params);

    const auto fill_scheduled = [&](auto& mode) {
        mode.target_current = params.target_current;
        mode.target_voltage = params.target_voltage;
        mode.max_charge_power = params.max_charge_power;
        mode.min_charge_power = params.min_charge_power;
        mode.max_charge_current = params.max_charge_current;
        mode.max_voltage = params.max_voltage;
        mode.min_voltage = params.min_voltage;
    };

    if (bpt) {
        auto& mode = req.control_mode.emplace<dt::BPT_Scheduled_DC_CLReqControlMode>();
        fill_scheduled(mode);
        mode.max_discharge_power = params.max_discharge_power;
        mode.min_discharge_power = params.min_discharge_power;
        mode.max_discharge_current = params.max_discharge_current;
    } else {
        auto& mode = req.control_mode.emplace<dt::Scheduled_DC_CLReqControlMode>();
        fill_scheduled(mode);
    }

    return req;
}

Result handle_response(const message_20::DC_ChargeLoopResponse& res) {
    Result result;
    result.valid = (res.response_code < dt::ResponseCode::FAILED);
    if (not result.valid) {
        return result;
    }

    result.present_voltage = dt::from_RationalNumber(res.present_voltage);
    result.present_current = dt::from_RationalNumber(res.present_current);

    if (res.status.has_value()) {
        result.notification = res.status->notification;
    }

    return result;
}

} // namespace dc_charge_loop

using namespace dc_charge_loop;

namespace {

RequestParams build_params(const Context& ctx) {
    const auto& dc = ctx.session_config.dc_charge_parameters;
    const auto& cache = ctx.dc_cache;

    const auto pick = [](const std::optional<float>& cached, const dt::RationalNumber& fallback) {
        return cached.has_value() ? dt::from_float(cached.value()) : fallback;
    };

    RequestParams p;
    p.present_voltage = dt::from_float(cache.present_voltage);
    p.display.present_soc = cache.present_soc;
    p.display.charging_complete = (cache.present_soc >= 100);

    // Dynamic energy window (see flow spec §3 DC_ChargeLoop).
    p.target_energy_request = pick(cache.target_energy_request, dc.target_energy_request.value_or(dc.energy_capacity));
    p.max_energy_request = pick(cache.max_energy_request, dc.max_energy_request.value_or(dt::from_float(60000.0f)));
    p.min_energy_request = pick(cache.min_energy_request, dc.min_energy_request.value_or(dt::from_float(1.0f)));

    // Charge limits.
    p.max_charge_power = pick(cache.max_charge_power, dc.max_charge_power);
    p.min_charge_power = dt::from_float(0.0f);
    p.max_charge_current = pick(cache.max_charge_current, dc.max_charge_current);
    p.max_voltage = dc.max_voltage;
    p.min_voltage = dt::from_float(150.0f);

    // Scheduled set points.
    p.target_current =
        cache.target_current.has_value() ? dt::from_float(cache.target_current.value()) : dc.target_current;
    p.target_voltage =
        cache.target_voltage.has_value() ? dt::from_float(cache.target_voltage.value()) : dc.target_voltage;

    if (const auto& bpt = ctx.session_config.dc_bpt_charge_parameters;
        bpt.has_value() and is_bpt_service(ctx.evse_info.selected_energy_service)) {
        p.max_discharge_power = bpt->max_discharge_power;
        p.min_discharge_power = bpt->min_discharge_power;
        p.max_discharge_current = bpt->max_discharge_current;
        p.max_v2x_energy_request = bpt->max_v2x_energy_request;
        p.min_v2x_energy_request = bpt->min_v2x_energy_request;
    }

    return p;
}

} // namespace

void DC_ChargeLoop::enter() {
    m_ctx.log.enter_state("DC_ChargeLoop");
}

void DC_ChargeLoop::send(Event) {
    const auto params = build_params(m_ctx);
    const bool bpt = is_bpt_service(m_ctx.evse_info.selected_energy_service);

    message_20::DC_ChargeLoopRequest req;
    if (m_ctx.evse_info.selected_control_mode == dt::ControlMode::Scheduled) {
        req = create_scheduled_request(params, bpt);
    } else {
        req = create_dynamic_request(params, bpt);
    }

    m_ctx.setup_header(req.header);
    m_ctx.send_request(req);
    m_ctx.start_timeout(d20::TimeoutType::SEQUENCE, MESSAGE_TIMEOUT_CHARGE_LOOP_MS);
}

d20::ev::Result DC_ChargeLoop::feed(Event ev) {
    if (ev == Event::SEND_REQUEST) {
        send(ev);
        return {};
    }

    if (ev == Event::CONTROL_MESSAGE) {
        handle_dc_control_event(m_ctx);
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
        m_ctx.log("DC_ChargeLoop message timeout reached, terminating session");
        m_ctx.session_stopped = true;
        return {};
    }

    if (ev != Event::V2GTP_MESSAGE) {
        return {};
    }

    m_ctx.stop_timeout(d20::TimeoutType::SEQUENCE);

    const auto variant = m_ctx.pull_response();

    if (const auto res = variant->get_if<message_20::DC_ChargeLoopResponse>()) {
        const auto result = handle_response(*res);

        if (not result.valid) {
            m_ctx.log("DC_ChargeLoop failed, terminating session");
            m_ctx.session_stopped = true;
            return {};
        }

        if (first_response) {
            m_ctx.feedback.signal(session::ev::feedback::Signal::CHARGE_LOOP_STARTED);
            first_response = false;
        }

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

    m_ctx.log("expected DC_ChargeLoopRes! But code type id: %d", variant->get_type());
    m_ctx.session_stopped = true;
    return {};
}

} // namespace iso15118::d20::ev::state
