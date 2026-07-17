// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/d2/ev/state/current_demand.hpp>

#include <iso15118/d2/ev/state/power_delivery.hpp>
#include <iso15118/d2/ev/timeouts.hpp>

#include <iso15118/detail/d2/ev/state/current_demand.hpp>
#include <iso15118/detail/d2/ev/state/state_helper.hpp>
#include <iso15118/detail/helper.hpp>

namespace iso15118::d2::ev::state {

namespace current_demand {

message_2::CurrentDemandRequest create_request(const RequestParams& params) {
    message_2::CurrentDemandRequest req;
    req.dc_ev_status.ev_ready = true;
    req.dc_ev_status.ev_error_code = dt::DC_EVErrorCode::NO_ERROR;
    req.dc_ev_status.ev_ress_soc = static_cast<int8_t>(params.present_soc);
    req.ev_target_current = dt::to_physical_value(params.target_current, dt::Unit::A);
    req.ev_target_voltage = dt::to_physical_value(params.target_voltage, dt::Unit::V);
    req.ev_maximum_current_limit = dt::to_physical_value(params.max_current, dt::Unit::A);
    if (params.max_power.has_value()) {
        req.ev_maximum_power_limit = dt::to_physical_value(params.max_power.value(), dt::Unit::W);
    }
    // Table 55 ([V2G2-258]): TRUE means full charge (100 % SOC) complete.
    req.charging_complete = (params.present_soc >= 100);
    return req;
}

Result handle_response(const message_2::CurrentDemandResponse& res) {
    Result result;
    result.valid = (res.response_code < dt::ResponseCode::FAILED);
    if (not result.valid) {
        return result;
    }

    result.present_voltage = static_cast<float>(dt::from_physical_value(res.evse_present_voltage));
    result.present_current = static_cast<float>(dt::from_physical_value(res.evse_present_current));
    result.notification = res.dc_evse_status.notification;

    const auto status_code = res.dc_evse_status.status_code;
    const bool status_stop = (status_code == dt::DC_EVSEStatusCode::EVSE_Shutdown) or
                             (status_code == dt::DC_EVSEStatusCode::EVSE_EmergencyShutdown) or
                             (status_code == dt::DC_EVSEStatusCode::EVSE_Malfunction) or
                             (status_code == dt::DC_EVSEStatusCode::EVSE_NotReady);
    result.charger_requested_stop =
        status_stop or (res.dc_evse_status.notification == dt::EVSENotification::StopCharging);
    result.renegotiation_or_receipt = (res.dc_evse_status.notification == dt::EVSENotification::ReNegotiation) or
                                      res.receipt_required.value_or(false);
    return result;
}

} // namespace current_demand

using namespace current_demand;

namespace {

RequestParams build_params(const Context& ctx) {
    const auto& config = ctx.session_config;
    const auto& cache = ctx.dc_cache;

    RequestParams p;
    p.present_soc = cache.present_soc;
    p.target_voltage = cache.target_voltage.value_or(config.dc_target_voltage);
    p.target_current = cache.target_current.value_or(config.dc_target_current);
    p.max_current = cache.max_charge_current.value_or(config.dc_ev_max_current);
    p.max_power = cache.max_charge_power.has_value() ? cache.max_charge_power : config.dc_ev_max_power;
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

d2::ev::Result CurrentDemand::feed(Event ev) {
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
        m_ctx.log("CurrentDemand message timeout reached, terminating session");
        m_ctx.session_stopped = true;
        return {};
    }

    if (ev != Event::V2GTP_MESSAGE) {
        return {};
    }

    m_ctx.stop_timeout(d20::TimeoutType::SEQUENCE);

    const auto variant = m_ctx.pull_response();

    if (const auto res = variant->get_if<message_2::CurrentDemandResponse>()) {
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

        // EVSE-driven stop via EVSENotification StopCharging or a non-ready DC_EVSEStatus [flow spec §4.4].
        if (result.charger_requested_stop) {
            m_ctx.feedback.stop_from_charger();
            m_ctx.pending_stop = dt::ChargingSession::Terminate;
            return m_ctx.create_state<PowerDelivery>(PowerDelivery::Phase::Stop);
        }

        // EVSE-requested schedule renegotiation: perform it. The EV re-enters PowerDelivery with
        // ChargeProgress=Renegotiate, which the SECC answers by returning to ChargeParameterDiscovery for
        // a new SAScheduleList; the session stays alive.
        if (result.notification == dt::EVSENotification::ReNegotiation) {
            m_ctx.log("CurrentDemand received ReNegotiation; renegotiating via PowerDelivery(Renegotiate)");
            return m_ctx.create_state<PowerDelivery>(PowerDelivery::Phase::Renegotiate);
        }

        // ReceiptRequired: EIM has no MeteringReceipt path, so gracefully terminate via
        // PowerDelivery(Stop) -> WeldingDetection -> SessionStop instead of continuing the loop.
        if (result.renegotiation_or_receipt) {
            m_ctx.log("CurrentDemand received ReceiptRequired (unsupported under EIM), "
                      "terminating session gracefully");
            m_ctx.pending_stop = dt::ChargingSession::Terminate;
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

} // namespace iso15118::d2::ev::state
