// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/din/ev/state/current_demand.hpp>

#include <iso15118/din/ev/state/power_delivery.hpp>
#include <iso15118/din/ev/timeouts.hpp>

#include <iso15118/detail/din/ev/state/current_demand.hpp>
#include <iso15118/detail/din/ev/state/state_helper.hpp>
#include <iso15118/detail/helper.hpp>

namespace iso15118::din::ev::state {

namespace current_demand {

message_din::CurrentDemandRequest create_request(const RequestParams& params) {
    message_din::CurrentDemandRequest req;
    req.dc_ev_status = params.dc_ev_status;
    req.ev_target_voltage = params.target_voltage;
    req.ev_target_current = params.target_current;
    req.ev_maximum_voltage_limit = params.max_voltage_limit;
    req.ev_maximum_current_limit = params.max_current_limit;
    req.ev_maximum_power_limit = params.max_power_limit;
    req.charging_complete = params.charging_complete;
    return req;
}

Result handle_response(const message_din::CurrentDemandResponse& res) {
    Result result;
    result.valid = (res.response_code < dt::ResponseCode::FAILED);
    if (not result.valid) {
        return result;
    }

    result.present_voltage = static_cast<float>(res.evse_present_voltage);
    result.present_current = static_cast<float>(res.evse_present_current);

    const bool charger_stop = (res.dc_evse_status.evse_status_code != dt::DcEvseStatusCode::EVSE_Ready) or
                              (res.dc_evse_status.evse_notification == dt::EvseNotification::StopCharging);
    result.charger_state = charger_stop ? ChargerState::Stop : ChargerState::Continue;
    return result;
}

} // namespace current_demand

using namespace current_demand;

namespace {

RequestParams build_params(const Context& ctx) {
    const auto& dc = ctx.session_config.dc;
    const auto& cache = ctx.dc_cache;

    RequestParams p;
    p.dc_ev_status = make_dc_ev_status(ctx, true);
    p.target_voltage = cache.target_voltage.has_value() ? cache.target_voltage.value() : dc.target_voltage;
    p.target_current = cache.target_current.has_value() ? cache.target_current.value() : dc.target_current;
    p.max_voltage_limit = dc.max_voltage_limit;
    p.max_current_limit = cache.max_charge_current.has_value() ? cache.max_charge_current.value() : dc.max_current_limit;
    p.max_power_limit = cache.max_charge_power.has_value() ? std::optional<double>(cache.max_charge_power.value())
                                                          : dc.max_power_limit;
    p.charging_complete = (cache.present_soc >= 100);
    return p;
}

} // namespace

void CurrentDemand::enter() {
    m_ctx.log.enter_state("CurrentDemand");
}

void CurrentDemand::send(Event) {
    auto req = create_request(build_params(m_ctx));
    m_ctx.setup_header(req.header);
    m_ctx.send_request(req);
    m_ctx.start_timeout(d20::TimeoutType::SEQUENCE, MESSAGE_TIMEOUT_CURRENT_DEMAND_MS);
}

din::ev::Result CurrentDemand::feed(Event ev) {
    if (ev == Event::SEND_REQUEST) {
        send(ev);
        return {};
    }

    if (ev == Event::CONTROL_MESSAGE) {
        handle_dc_control_event(m_ctx);
        if (const auto* stop = m_ctx.get_control_event<StopCharging>(); stop and static_cast<bool>(*stop)) {
            stop_requested = true;
            stop_reason = ChargingSession::Terminate;
        } else if (const auto* pause = m_ctx.get_control_event<PauseCharging>(); pause and static_cast<bool>(*pause)) {
            stop_requested = true;
            stop_reason = ChargingSession::Pause;
        }
        return {};
    }

    if (ev == Event::TIMEOUT) {
        m_ctx.log("CurrentDemand message timeout reached, terminating session");
        m_ctx.session_stopped = true;
        return {};
    }

    if (ev != Event::V2GTP_MESSAGE) {
        return {};
    }

    m_ctx.stop_timeout(d20::TimeoutType::SEQUENCE);

    const auto variant = m_ctx.pull_response();

    if (const auto res = variant->get_if<message_din::CurrentDemandResponse>()) {
        const auto result = handle_response(*res);

        if (not result.valid) {
            m_ctx.log("CurrentDemand failed, terminating session");
            m_ctx.session_stopped = true;
            return {};
        }

        if (first_response) {
            m_ctx.feedback.signal(session::ev::feedback::Signal::CHARGE_LOOP_STARTED);
            first_response = false;
        }

        // SECC-driven stop (EVSE not ready / EVSENotification StopCharging).
        if (result.charger_state == ChargerState::Stop) {
            m_ctx.feedback.stop_from_charger();
            m_ctx.pending_stop = ChargingSession::Terminate;
            return m_ctx.create_state<PowerDelivery>(PowerDelivery::Phase::Stop);
        }

        // EV-initiated stop/pause (StopCharging/PauseCharging control event).
        if (stop_requested) {
            m_ctx.pending_stop = stop_reason;
            return m_ctx.create_state<PowerDelivery>(PowerDelivery::Phase::Stop);
        }

        send(ev);
        return {};
    }

    m_ctx.log("expected CurrentDemandRes! But code type id: %d", variant->get_type());
    m_ctx.session_stopped = true;
    return {};
}

} // namespace iso15118::din::ev::state
