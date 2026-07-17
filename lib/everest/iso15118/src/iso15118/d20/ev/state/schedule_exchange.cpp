// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/d20/ev/state/schedule_exchange.hpp>

#include <iso15118/d20/ev/state/dc_cable_check.hpp>
#include <iso15118/d20/ev/state/power_delivery.hpp>
#include <iso15118/d20/ev/timeouts.hpp>

#include <iso15118/detail/d20/ev/state/schedule_exchange.hpp>
#include <iso15118/detail/d20/ev/state/state_helper.hpp>
#include <iso15118/detail/helper.hpp>

namespace iso15118::d20::ev::state {

namespace schedule_exchange {

message_20::ScheduleExchangeRequest create_dynamic_request(const Params& params) {
    message_20::ScheduleExchangeRequest req;
    req.max_supporting_points = params.max_supporting_points;

    auto& mode = req.control_mode.emplace<dt::Dynamic_SEReqControlMode>();
    mode.departure_time = params.departure_time;
    mode.minimum_soc = params.minimum_soc;
    mode.target_soc = params.target_soc;
    mode.target_energy = params.target_energy;
    mode.max_energy = params.max_energy;
    mode.min_energy = params.min_energy;
    mode.max_v2x_energy = params.max_v2x_energy;
    mode.min_v2x_energy = params.min_v2x_energy;

    return req;
}

message_20::ScheduleExchangeRequest create_scheduled_request(const Params& params) {
    message_20::ScheduleExchangeRequest req;
    req.max_supporting_points = params.max_supporting_points;

    auto& mode = req.control_mode.emplace<dt::Scheduled_SEReqControlMode>();
    mode.departure_time = params.departure_time;
    mode.target_energy = params.target_energy;
    mode.max_energy = params.max_energy;
    mode.min_energy = params.min_energy;
    // EVEnergyOffer omitted (SECC tolerates it, Josev omits it) [flow spec §3].

    return req;
}

Result handle_response(const message_20::ScheduleExchangeResponse& res) {
    Result result;
    result.valid = (res.response_code < dt::ResponseCode::FAILED);
    result.finished = (res.processing == dt::Processing::Finished);
    return result;
}

} // namespace schedule_exchange

using namespace schedule_exchange;

namespace {

Params build_params(const Context& ctx) {
    const auto& dc = ctx.session_config.dc_charge_parameters;

    Params params;
    params.departure_time = 7200;
    params.minimum_soc = 30;
    params.target_soc = 80;

    // Energy request window (see flow spec §3). Use the configured values where present, otherwise
    // the SIL defaults (target 40 kWh, max 60 kWh, min -20 kWh).
    params.target_energy = dc.target_energy_request.value_or(dt::from_float(40000.0f));
    params.max_energy = dc.max_energy_request.value_or(dt::from_float(60000.0f));
    params.min_energy = dc.min_energy_request.value_or(dt::from_float(-20000.0f));

    if (const auto& bpt = ctx.session_config.dc_bpt_charge_parameters;
        bpt.has_value() and is_bpt_service(ctx.evse_info.selected_energy_service)) {
        params.max_v2x_energy = bpt->max_v2x_energy_request.value_or(dt::from_float(5000.0f));
        params.min_v2x_energy = bpt->min_v2x_energy_request.value_or(dt::from_float(0.0f));
        if (bpt->min_soc.has_value()) {
            params.minimum_soc = bpt->min_soc;
        }
        if (bpt->target_soc.has_value()) {
            params.target_soc = bpt->target_soc;
        }
    } else {
        params.max_v2x_energy = dt::from_float(5000.0f);
        params.min_v2x_energy = dt::from_float(0.0f);
    }

    return params;
}

} // namespace

void ScheduleExchange::enter() {
    m_ctx.log.enter_state("ScheduleExchange");
}

void ScheduleExchange::send(Event) {
    if (first_request) {
        const auto params = build_params(m_ctx);
        if (m_ctx.evse_info.selected_control_mode == dt::ControlMode::Scheduled) {
            cached_request = create_scheduled_request(params);
        } else {
            cached_request = create_dynamic_request(params);
        }
        first_request = false;
    }

    m_ctx.setup_header(cached_request.header);
    m_ctx.send_request(cached_request);
    m_ctx.start_timeout(d20::TimeoutType::SEQUENCE, MESSAGE_TIMEOUT_MS);
}

d20::ev::Result ScheduleExchange::feed(Event ev) {
    if (ev == Event::SEND_REQUEST) {
        send(ev);
        return {};
    }

    if (ev == Event::CONTROL_MESSAGE) {
        handle_stop_control_event(m_ctx);
        return {};
    }

    if (ev == Event::TIMEOUT) {
        m_ctx.log("ScheduleExchange message timeout reached, terminating session");
        m_ctx.session_stopped = true;
        return {};
    }

    if (ev != Event::V2GTP_MESSAGE) {
        return {};
    }

    m_ctx.stop_timeout(d20::TimeoutType::SEQUENCE);

    const auto variant = m_ctx.pull_response();

    if (const auto res = variant->get_if<message_20::ScheduleExchangeResponse>()) {
        const auto result = handle_response(*res);

        if (not result.valid) {
            m_ctx.log("ScheduleExchange failed, terminating session");
            m_ctx.session_stopped = true;
            return {};
        }

        if (not result.finished) {
            // SECC keeps processing; resend the cached request (paced by the session driver).
            send(ev);
            return {};
        }

        // Publish ev_power_ready(true) at ScheduleExchange Finished, before any PowerDelivery [flow spec §4.1].
        m_ctx.feedback.ev_power_ready(true);

        if (auto stop = stop_if_pending(m_ctx)) {
            return stop;
        }

        if (is_dc_service(m_ctx.evse_info.selected_energy_service)) {
            return m_ctx.create_state<DC_CableCheck>();
        }
        // AC branch: proceed directly to PowerDelivery(Start).
        return m_ctx.create_state<PowerDelivery>(PowerDelivery::Phase::Start);
    }

    m_ctx.log("expected ScheduleExchangeRes! But code type id: %d", variant->get_type());
    m_ctx.session_stopped = true;
    return {};
}

} // namespace iso15118::d20::ev::state
