// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/din/state/charge_parameter_discovery.hpp>

#include <cmath>

#include <iso15118/din/state/cable_check.hpp>
#include <iso15118/din/state/session_stop.hpp>

#include <iso15118/detail/din/state/charge_parameter_discovery.hpp>
#include <iso15118/detail/din/state/state_helper.hpp>
#include <iso15118/detail/helper.hpp>

namespace iso15118::din::state {

namespace {
constexpr int16_t DIN_PMAX_MAX = 32767;         // SHRT_MAX; DIN PMax is a raw short in watts
constexpr uint32_t DIN_SA_SCHEDULE_DURATION = 86400; // must cover 24 hours [V2G-DC-556]

dt::SAScheduleList build_sa_schedule_list(const SessionConfig& config) {
    dt::SAScheduleList list;
    auto& tuple = list.emplace_back();
    tuple.sa_schedule_tuple_id = 1;
    tuple.pmax_schedule_id = 1;
    auto& entry = tuple.pmax_schedule.emplace_back();
    entry.start = 0;
    entry.duration = DIN_SA_SCHEDULE_DURATION;
    const double pmax = config.evse_maximum_power_limit.value_or(static_cast<double>(DIN_PMAX_MAX));
    entry.p_max = (pmax > static_cast<double>(DIN_PMAX_MAX)) ? DIN_PMAX_MAX : static_cast<int16_t>(pmax);
    return list;
}
} // namespace

message_din::ChargeParameterDiscoveryResponse handle_request(const message_din::ChargeParameterDiscoveryRequest& req,
                                                             const SessionConfig& config, bool processing_finished,
                                                             const dt::SessionId& session_id) {
    message_din::ChargeParameterDiscoveryResponse res;

    if (not validate_and_setup_header(res.header, session_id, req.header.session_id)) {
        return response_with_code(res, dt::ResponseCode::FAILED_UnknownSession);
    }

    // [V2G-DC-397] DIN 70121 is DC only.
    if (req.ev_requested_energy_transfer_type != dt::EnergyTransferMode::DC_core and
        req.ev_requested_energy_transfer_type != dt::EnergyTransferMode::DC_extended) {
        return response_with_code(res, dt::ResponseCode::FAILED_WrongEnergyTransferType);
    }

    dt::DcEvseChargeParameter param;
    param.dc_evse_status.evse_status_code = dt::DcEvseStatusCode::EVSE_Ready;
    param.dc_evse_status.evse_notification = dt::EvseNotification::None;
    param.evse_maximum_current_limit = config.evse_maximum_current_limit;
    param.evse_maximum_power_limit = config.evse_maximum_power_limit;
    param.evse_maximum_voltage_limit = config.evse_maximum_voltage_limit;
    param.evse_minimum_current_limit = config.evse_minimum_current_limit;
    param.evse_minimum_voltage_limit = config.evse_minimum_voltage_limit;
    param.evse_peak_current_ripple = config.evse_peak_current_ripple;
    param.evse_energy_to_be_delivered = config.evse_energy_to_be_delivered;
    res.dc_evse_charge_parameter = param;

    res.evse_processing = processing_finished ? dt::EvseProcessing::Finished : dt::EvseProcessing::Ongoing;

    // A SAScheduleList is mandatory once EVSEProcessing is Finished (EvseV2G always sends one). A single
    // PMaxSchedule tuple advertising the EVSE max power (capped at the DIN PMax short range).
    if (processing_finished) {
        res.sa_schedule_list = build_sa_schedule_list(config);
    }

    return response_with_code(res, dt::ResponseCode::OK);
}

void ChargeParameterDiscovery::enter() {
    m_ctx.log.enter_state("ChargeParameterDiscovery");
}

Result ChargeParameterDiscovery::feed(Event ev) {
    if (ev != Event::V2GTP_MESSAGE) {
        return {};
    }

    // An EV aborting mid-handshake sends SessionStopReq; hand it to SessionStop for a clean SessionStopRes.
    if (m_ctx.peek_request_type() == message_din::Type::SessionStopReq) {
        return m_ctx.create_state<SessionStop>();
    }

    const auto variant = m_ctx.pull_request();

    if (const auto req = variant->get_if<message_din::ChargeParameterDiscoveryRequest>()) {
        // Forward the EV's advertised maxima so the power supply is provisioned for the actual EV limits
        // (EvseV2G din_server.cpp:225-238); the SIL "falling back to 500V" root cause.
        if (req->dc_ev_charge_parameter.has_value()) {
            const auto& p = req->dc_ev_charge_parameter.value();
            session::feedback::DcMaximumLimits limits{};
            limits.voltage = static_cast<float>(p.ev_maximum_voltage_limit);
            limits.current = static_cast<float>(p.ev_maximum_current_limit);
            limits.power = p.ev_maximum_power_limit.has_value()
                               ? static_cast<float>(p.ev_maximum_power_limit.value())
                               : limits.voltage * limits.current;
            m_ctx.feedback.dc_max_limits(limits);
        }

        // EIM/SIL: the DC parameters are available immediately, so EVSEProcessing finishes at once.
        const auto res = handle_request(*req, m_ctx.session_config, true, m_ctx.get_session_id());
        m_ctx.respond(res);

        if (res.response_code >= dt::ResponseCode::FAILED) {
            m_ctx.session_stopped = true;
            return {};
        }

        if (res.evse_processing == dt::EvseProcessing::Finished) {
            return m_ctx.create_state<CableCheck>();
        }
        return {};
    }

    m_ctx.log("expected ChargeParameterDiscoveryReq! But code type id: %d", variant->get_type());
    message_din::ChargeParameterDiscoveryResponse res;
    setup_header(res.header, m_ctx.get_session_id());
    m_ctx.respond(response_with_code(res, dt::ResponseCode::FAILED_SequenceError));
    m_ctx.session_stopped = true;
    return {};
}

} // namespace iso15118::din::state
