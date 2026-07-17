// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/d2/state/charge_parameter_discovery.hpp>

#include <algorithm>
#include <cmath>

#include <iso15118/d2/state/cable_check.hpp>
#include <iso15118/d2/state/power_delivery.hpp>
#include <iso15118/d2/state/session_stop.hpp>

#include <iso15118/message/ac_charge_parameter_discovery.hpp>
#include <iso15118/message/common_types.hpp>

#include <iso15118/detail/d2/state/charge_parameter_discovery.hpp>
#include <iso15118/detail/d2/state/sequence_error.hpp>
#include <iso15118/detail/d2/state/state_helper.hpp>
#include <iso15118/detail/helper.hpp>

namespace iso15118::d2::state {

using dt::to_physical_value;
using dt::Unit;

dt::SAScheduleList build_sa_schedule_list(const d2::SessionConfig& config, bool is_dc) {
    dt::SAScheduleList list;
    auto& tuple = list.emplace_back();
    tuple.sa_schedule_tuple_id = 1; // [V2G2-773]: must be in 1..255
    auto& entry = tuple.pmax_schedule.emplace_back();
    entry.start = 0;
    entry.duration = config.sa_schedule_duration;
    const float pmax = is_dc ? config.dc_max_power : (config.ac_max_current * config.ac_nominal_voltage);
    entry.p_max = to_physical_value(pmax, Unit::W);
    return list;
}

namespace {

namespace m20dt = message_20::datatypes;

// Forward the EV's advertised maxima to the module so the power supply is provisioned for the actual EV
// limits instead of the SECC defaults (EvseV2G iso_server.cpp:390-403 DC / 320-346 AC).
void forward_ev_limits(const message_2::ChargeParameterDiscoveryRequest& req, bool is_dc,
                       const session::Feedback& feedback) {
    if (is_dc) {
        if (not req.dc_ev_charge_parameter.has_value()) {
            return;
        }
        const auto& p = req.dc_ev_charge_parameter.value();
        session::feedback::DcMaximumLimits limits{};
        limits.voltage = static_cast<float>(dt::from_physical_value(p.ev_maximum_voltage_limit));
        limits.current = static_cast<float>(dt::from_physical_value(p.ev_maximum_current_limit));
        limits.power = p.ev_maximum_power_limit.has_value()
                           ? static_cast<float>(dt::from_physical_value(p.ev_maximum_power_limit.value()))
                           : limits.voltage * limits.current;
        feedback.dc_max_limits(limits);
    } else if (req.ac_ev_charge_parameter.has_value()) {
        const auto& p = req.ac_ev_charge_parameter.value();
        // The ISO-2 AC EV limits are voltage/current; the module's AC feedback carries charge power, so
        // forward the derived max/min charge power (the EVSE-side V/I clamp still governs the setpoint).
        const auto max_v = dt::from_physical_value(p.ev_max_voltage);
        const auto max_i = dt::from_physical_value(p.ev_max_current);
        const auto min_i = dt::from_physical_value(p.ev_min_current);
        m20dt::AC_CPDReqEnergyTransferMode mode{};
        mode.max_charge_power = m20dt::from_float(static_cast<float>(max_v * max_i));
        mode.min_charge_power = m20dt::from_float(static_cast<float>(max_v * min_i));
        feedback.ac_limits(mode);
    }
}

void fill_dc(message_2::ChargeParameterDiscoveryResponse& res, const d2::SessionConfig& config) {
    auto& dc = res.dc_evse_charge_parameter.emplace();
    dc.dc_evse_status.notification = dt::EVSENotification::None;
    dc.dc_evse_status.notification_max_delay = 0;
    dc.dc_evse_status.isolation_status = dt::IsolationLevel::Invalid;
    dc.dc_evse_status.status_code = dt::DC_EVSEStatusCode::EVSE_Ready;
    dc.evse_maximum_current_limit = to_physical_value(config.dc_max_current, Unit::A);
    dc.evse_maximum_power_limit = to_physical_value(config.dc_max_power, Unit::W);
    dc.evse_maximum_voltage_limit = to_physical_value(config.dc_max_voltage, Unit::V);
    dc.evse_minimum_current_limit = to_physical_value(config.dc_min_current, Unit::A);
    dc.evse_minimum_voltage_limit = to_physical_value(config.dc_min_voltage, Unit::V);
    dc.evse_peak_current_ripple = to_physical_value(config.dc_peak_current_ripple, Unit::A);
}

void fill_ac(message_2::ChargeParameterDiscoveryResponse& res, const d2::SessionConfig& config) {
    auto& ac = res.ac_evse_charge_parameter.emplace();
    ac.ac_evse_status = make_ac_evse_status();
    ac.evse_nominal_voltage = to_physical_value(config.ac_nominal_voltage, Unit::V);
    ac.evse_max_current = to_physical_value(config.ac_max_current, Unit::A);
}

} // namespace

message_2::ChargeParameterDiscoveryResponse handle_request(const message_2::ChargeParameterDiscoveryRequest& req,
                                                           const dt::SessionId& session_id,
                                                           const d2::SessionConfig& config) {
    message_2::ChargeParameterDiscoveryResponse res;
    res.header.session_id = session_id;

    const auto mode = req.requested_energy_transfer_mode;
    const auto& modes = config.supported_energy_transfer_modes;
    if (std::find(modes.begin(), modes.end(), mode) == modes.end()) {
        res.response_code = dt::ResponseCode::FAILED_WrongEnergyTransferMode;
        res.evse_processing = dt::EVSEProcessing::Finished;
        return res;
    }

    const bool is_dc = is_dc_mode(mode);

    // The parameter phase is always Finished (limits are known up front, EvseV2G parity).
    res.evse_processing = dt::EVSEProcessing::Finished;
    res.sa_schedule_list = build_sa_schedule_list(config, is_dc);

    if (is_dc) {
        if (not req.dc_ev_charge_parameter.has_value()) {
            res.response_code = dt::ResponseCode::FAILED_WrongChargeParameter;
            return res;
        }
        fill_dc(res, config);
    } else {
        if (not req.ac_ev_charge_parameter.has_value()) {
            res.response_code = dt::ResponseCode::FAILED_WrongChargeParameter;
            return res;
        }
        fill_ac(res, config);
    }

    res.response_code = dt::ResponseCode::OK;
    return res;
}

void ChargeParameterDiscovery::enter() {
    m_ctx.log.enter_state("ChargeParameterDiscovery");
}

Result ChargeParameterDiscovery::feed(Event ev) {
    if (ev != Event::V2GTP_MESSAGE) {
        return {};
    }

    // An EV aborting mid-handshake sends SessionStopReq; hand it to SessionStop for a clean SessionStopRes.
    if (m_ctx.peek_request_type() == message_2::Type::SessionStopReq) {
        return m_ctx.create_state<SessionStop>();
    }

    const auto variant = m_ctx.pull_request();

    const auto req = variant->get_if<message_2::ChargeParameterDiscoveryRequest>();
    if (req == nullptr) {
        m_ctx.log("expected ChargeParameterDiscoveryReq! But code type id: %d", variant->get_type());
        // [V2G2-539]: answer with the received-type response carrying FAILED_SequenceError, then close.
        respond_sequence_error(m_ctx, *variant);
        m_ctx.session_stopped = true;
        return {};
    }

    // The request must echo the assigned SessionID [V2G2-388]; a mismatch is answered with
    // ChargeParameterDiscoveryRes/FAILED_UnknownSession and terminates the session.
    if (reject_unknown_session(m_ctx, *variant)) {
        return {};
    }

    m_ctx.dc_charging = is_dc_mode(req->requested_energy_transfer_mode);

    forward_ev_limits(*req, m_ctx.dc_charging, m_ctx.feedback);

    const auto res = handle_request(*req, m_ctx.get_session_id(), m_ctx.session_config);
    m_ctx.respond(res);

    if (res.response_code >= dt::ResponseCode::FAILED) {
        m_ctx.session_stopped = true;
        return {};
    }

    if (res.sa_schedule_list.has_value() and not res.sa_schedule_list->empty()) {
        m_ctx.sa_schedule_list = res.sa_schedule_list.value();
        m_ctx.sa_schedule_tuple_id = res.sa_schedule_list->front().sa_schedule_tuple_id;
    }

    if (m_ctx.dc_charging) {
        return m_ctx.create_state<CableCheck>();
    }
    return m_ctx.create_state<PowerDelivery>();
}

} // namespace iso15118::d2::state
