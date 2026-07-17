// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/din/ev/state/charge_parameter_discovery.hpp>

#include <iso15118/din/ev/state/cable_check.hpp>
#include <iso15118/din/ev/timeouts.hpp>

#include <iso15118/detail/din/ev/state/charge_parameter_discovery.hpp>
#include <iso15118/detail/din/ev/state/state_helper.hpp>
#include <iso15118/detail/helper.hpp>

namespace iso15118::din::ev::state {

namespace charge_parameter_discovery {

namespace {

session::ev::feedback::DcMaximumLimits extract_limits(const dt::DcEvseChargeParameter& param) {
    session::ev::feedback::DcMaximumLimits limits;
    limits.current = static_cast<float>(param.evse_maximum_current_limit);
    limits.voltage = static_cast<float>(param.evse_maximum_voltage_limit);
    if (param.evse_maximum_power_limit.has_value()) {
        limits.power = static_cast<float>(param.evse_maximum_power_limit.value());
    } else {
        limits.power = static_cast<float>(param.evse_maximum_current_limit * param.evse_maximum_voltage_limit);
    }
    return limits;
}

bool evse_requests_stop(const dt::DcEvseStatus& status) {
    if (status.evse_notification == dt::EvseNotification::StopCharging) {
        return true;
    }
    switch (status.evse_status_code) {
    case dt::DcEvseStatusCode::EVSE_Shutdown:
    case dt::DcEvseStatusCode::EVSE_EmergencyShutdown:
    case dt::DcEvseStatusCode::EVSE_Malfunction:
        return true;
    default:
        return false;
    }
}

} // namespace

message_din::ChargeParameterDiscoveryRequest create_request(dt::EnergyTransferMode requested_energy_transfer_type,
                                                            const dt::DcEvChargeParameter& dc_ev_charge_parameter) {
    message_din::ChargeParameterDiscoveryRequest req;
    req.ev_requested_energy_transfer_type = requested_energy_transfer_type;
    req.dc_ev_charge_parameter = dc_ev_charge_parameter;
    return req;
}

Result handle_response(const message_din::ChargeParameterDiscoveryResponse& res) {
    if (res.response_code >= dt::ResponseCode::FAILED) {
        return {Action::Failed};
    }
    if (res.evse_processing != dt::EvseProcessing::Finished) {
        return {Action::Retry};
    }

    Result result;
    result.action = Action::Done;
    if (res.dc_evse_charge_parameter.has_value()) {
        const auto& param = res.dc_evse_charge_parameter.value();
        result.evse_parameter = param;
        result.limits = extract_limits(param);
        result.evse_stopping = evse_requests_stop(param.dc_evse_status);
    }
    return result;
}

} // namespace charge_parameter_discovery

using namespace charge_parameter_discovery;

void ChargeParameterDiscovery::enter() {
    m_ctx.log.enter_state("ChargeParameterDiscovery");
}

void ChargeParameterDiscovery::send(Event) {
    if (first_request) {
        m_ctx.start_timeout(d20::TimeoutType::ONGOING, TIMEOUT_CHARGE_PARAMETER_ONGOING_MS);
        first_request = false;
    }

    const auto& dc = m_ctx.session_config.dc;
    dt::DcEvChargeParameter dc_param;
    dc_param.dc_ev_status = make_dc_ev_status(m_ctx, false);
    dc_param.ev_maximum_current_limit = dc.max_current_limit;
    dc_param.ev_maximum_voltage_limit = dc.max_voltage_limit;
    dc_param.ev_maximum_power_limit = dc.max_power_limit;
    dc_param.ev_energy_capacity = dc.energy_capacity;
    dc_param.ev_energy_request = dc.energy_request;
    dc_param.full_soc = dc.full_soc;
    dc_param.bulk_soc = dc.bulk_soc;

    auto req = create_request(m_ctx.session_config.requested_energy_transfer_type, dc_param);
    m_ctx.setup_header(req.header);
    m_ctx.send_request(req);
    m_ctx.start_timeout(d20::TimeoutType::SEQUENCE, MESSAGE_TIMEOUT_MS);
}

din::ev::Result ChargeParameterDiscovery::feed(Event ev) {
    if (ev == Event::SEND_REQUEST) {
        send(ev);
        return {};
    }

    if (ev == Event::CONTROL_MESSAGE) {
        handle_dc_control_event(m_ctx);
        handle_stop_control_event(m_ctx);
        return {};
    }

    if (ev == Event::TIMEOUT) {
        const auto* timeout = m_ctx.get_active_timeout();
        if (timeout and *timeout == d20::TimeoutType::ONGOING) {
            m_ctx.log("ChargeParameterDiscovery ongoing timeout reached, terminating session");
            ongoing_timeout_reached = true;
        } else {
            m_ctx.log("ChargeParameterDiscovery message timeout reached, terminating session");
        }
        m_ctx.session_stopped = true;
        return {};
    }

    if (ev != Event::V2GTP_MESSAGE) {
        return {};
    }

    m_ctx.stop_timeout(d20::TimeoutType::SEQUENCE);

    const auto variant = m_ctx.pull_response();

    if (const auto res = variant->get_if<message_din::ChargeParameterDiscoveryResponse>()) {
        const auto result = handle_response(*res);

        switch (result.action) {
        case Action::Failed:
            m_ctx.log("ChargeParameterDiscovery failed, terminating session");
            m_ctx.session_stopped = true;
            return {};
        case Action::Done:
            m_ctx.stop_timeout(d20::TimeoutType::ONGOING);
            m_ctx.evse_info.dc_evse_parameter = result.evse_parameter;
            if (result.limits.has_value()) {
                m_ctx.evse_info.dc_present_limits = result.limits;
                m_ctx.feedback.dc_evse_present_limits(result.limits.value());
            }

            // Honour a stop/pause control event that already arrived during the handshake.
            if (auto stop = stop_if_pending(m_ctx)) {
                return stop;
            }
            // EvseV2G's no-energy-pause path finishes ChargeParameterDiscovery with
            // EVSENotification=StopCharging and then expects a SessionStopReq (din_server.cpp
            // WAIT_FOR_SESSIONSTOP). Diverting to SessionStop avoids a CableCheckReq that the SECC would
            // reject with FAILED_SequenceError.
            if (result.evse_stopping) {
                m_ctx.log("ChargeParameterDiscovery finished with SECC stop request, transitioning to SessionStop");
                m_ctx.pending_stop = ChargingSession::Terminate;
                return m_ctx.create_state<SessionStop>();
            }

            // Publish ev_power_ready(true) once the charge parameters are settled, before CableCheck.
            m_ctx.feedback.ev_power_ready(true);
            return m_ctx.create_state<CableCheck>();
        case Action::Retry:
        default:
            if (ongoing_timeout_reached) {
                m_ctx.session_stopped = true;
                return {};
            }
            send(ev);
            return {};
        }
    }

    m_ctx.log("expected ChargeParameterDiscoveryRes! But code type id: %d", variant->get_type());
    m_ctx.session_stopped = true;
    return {};
}

} // namespace iso15118::din::ev::state
