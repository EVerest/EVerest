// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/din/state/charge_parameter_discovery.hpp>

#include <cmath>

#include <iso15118/din/state/cable_check.hpp>
#include <iso15118/din/state/session_stop.hpp>

#include <iso15118/detail/din/state/charge_parameter_discovery.hpp>
#include <iso15118/detail/din/state/sequence_error.hpp>
#include <iso15118/detail/din/state/state_helper.hpp>
#include <iso15118/detail/helper.hpp>

namespace iso15118::din::state {

namespace {
constexpr int16_t DIN_PMAX_MAX = 32767;              // SHRT_MAX; DIN PMax is a raw short in watts
constexpr uint32_t DIN_SA_SCHEDULE_DURATION = 86400; // must cover 24 hours [V2G-DC-556]
// Schedule horizon offered while pausing for lack of energy: the EV is told to come back, not to plan a
// full day of charging (EvseV2G din_server.cpp PAUSE_DURATION).
constexpr uint32_t DIN_PAUSE_DURATION = 60 * 30;

dt::SAScheduleList build_sa_schedule_list(const SessionConfig& config) {
    dt::SAScheduleList list;
    auto& tuple = list.emplace_back();
    tuple.sa_schedule_tuple_id = 1;
    tuple.pmax_schedule_id = 1;
    auto& entry = tuple.pmax_schedule.emplace_back();
    entry.start = 0;
    entry.duration =
        (config.no_energy_pause == d20::NoEnergyPauseMode::None) ? DIN_SA_SCHEDULE_DURATION : DIN_PAUSE_DURATION;
    // PMax advertises the hardware capability, not the current energy-management grant; the live limits
    // reach the EV in every CurrentDemandRes instead (EvseV2G parity). Safety: an unreported capability
    // advertises 0, never an invented value.
    const double pmax = config.evse_capability_maximum_power_limit.value_or(0.0);
    entry.p_max = (pmax > static_cast<double>(DIN_PMAX_MAX)) ? DIN_PMAX_MAX : static_cast<int16_t>(pmax);
    return list;
}
} // namespace

namespace {
// Maps the EV's DC_EVChargeParameter onto the protocol-neutral module feedback.
session::feedback::DcEvChargeParameters to_dc_ev_charge_parameters(const dt::DcEvChargeParameter& in) {
    session::feedback::DcEvChargeParameters out{};
    out.max_current = static_cast<float>(in.ev_maximum_current_limit);
    out.max_voltage = static_cast<float>(in.ev_maximum_voltage_limit);
    if (in.ev_maximum_power_limit.has_value()) {
        out.max_power = static_cast<float>(in.ev_maximum_power_limit.value());
    }
    if (in.ev_energy_capacity.has_value()) {
        out.energy_capacity = static_cast<float>(in.ev_energy_capacity.value());
    }
    if (in.ev_energy_request.has_value()) {
        out.energy_request = static_cast<float>(in.ev_energy_request.value());
    }
    out.full_soc = in.full_soc;
    out.bulk_soc = in.bulk_soc;
    out.ress_soc = in.dc_ev_status.ev_ress_soc;
    return out;
}

// [V2G-DC-397]: the EV may only request the energy transfer mode the SECC offered in ServiceDiscoveryRes,
// and DIN SPEC 70121 only ever serves the DC modes. The offered mode is a single value (the ChargeService
// advertises exactly one), so this is a direct match of the two enums, which are separate types.
bool requested_mode_is_offered(dt::EnergyTransferMode requested, dt::SupportedEnergyTransferMode offered) {
    switch (offered) {
    case dt::SupportedEnergyTransferMode::DC_core:
        return requested == dt::EnergyTransferMode::DC_core;
    case dt::SupportedEnergyTransferMode::DC_extended:
        return requested == dt::EnergyTransferMode::DC_extended;
    default:
        // AC and the combined AC/DC modes: not served by this DC-only SECC.
        return false;
    }
}
} // namespace

message_din::ChargeParameterDiscoveryResponse handle_request(const message_din::ChargeParameterDiscoveryRequest& req,
                                                             const SessionConfig& config, bool processing_finished,
                                                             const dt::SessionId& session_id, bool charger_stop,
                                                             std::optional<dt::DcEvseStatusCode> error_status_code) {
    message_din::ChargeParameterDiscoveryResponse res;
    setup_header(res.header, session_id);

    // DC_EVSEChargeParameter is mandatory in ChargeParameterDiscoveryRes and must be present even on a
    // FAILED_UnknownSession / FAILED_WrongEnergyTransferType response, so populate it before those checks.
    // An EVSE-initiated stop (stop_charging) is signalled with EVSE_Shutdown in every state, not just the
    // charge loop (EvseV2G parity; the EVSENotification stays None for DC [V2G-DC-500]).
    // [V2G-DC-638] EVSE_Ready is used "unless a different value shall be used according to another
    // requirement": a module-reported fault (send_error) wins over both EVSE_Ready and the EVSE_Shutdown
    // of a charger-initiated stop, it is the more specific reason. [V2G-DC-637] keeps it informational:
    // the code does not steer the charging process, it tells the EV what is going on.
    dt::DcEvseChargeParameter param;
    param.dc_evse_status.evse_status_code = error_status_code.value_or(
        charger_stop ? dt::DcEvseStatusCode::EVSE_Shutdown : dt::DcEvseStatusCode::EVSE_Ready);
    param.dc_evse_status.evse_notification = dt::EvseNotification::None;
    // The maxima are the hardware capabilities: the CPD offer is the maximum the EVSE can deliver, so a
    // temporary energy-management restriction must not shrink it -- the live limits reach the EV in
    // every CurrentDemandRes instead (EvseV2G parity: CPD announces power_capabilities).
    param.evse_maximum_current_limit = config.evse_capability_maximum_current_limit;
    param.evse_maximum_power_limit = config.evse_capability_maximum_power_limit;
    param.evse_maximum_voltage_limit = config.evse_capability_maximum_voltage_limit;
    param.evse_minimum_current_limit = config.evse_minimum_current_limit;
    param.evse_minimum_voltage_limit = config.evse_minimum_voltage_limit;
    param.evse_peak_current_ripple = config.evse_peak_current_ripple;
    param.evse_current_regulation_tolerance = config.evse_current_regulation_tolerance;
    param.evse_energy_to_be_delivered = config.evse_energy_to_be_delivered;
    res.dc_evse_charge_parameter = param;

    // [V2G-DC-391] the SessionID must match the one assigned in SessionSetup; a mismatch is answered with
    // FAILED_UnknownSession (carrying the mandatory DC_EVSEChargeParameter filled above) and terminates.
    if (session_id != req.header.session_id) {
        return response_with_code(res, dt::ResponseCode::FAILED_UnknownSession);
    }

    // [V2G-DC-397] DIN 70121 is DC only, and the requested mode must be the one the SECC advertised as
    // its ChargeService SupportedEnergyTransferMode -- an EV asking for DC_core against a DC_extended-only
    // EVSE has to be rejected (EvseV2G din_server.cpp compares against the advertised mode as well).
    if (not requested_mode_is_offered(req.ev_requested_energy_transfer_type, config.energy_transfer_mode)) {
        return response_with_code(res, dt::ResponseCode::FAILED_WrongEnergyTransferType);
    }

    // [V2G-DC-398] A DC session must not carry AC_EVChargeParameter; answered with
    // FAILED_WrongChargeParameter (EvseV2G din_server.cpp checks AC_EVChargeParameter_isUsed).
    if (req.ac_ev_charge_parameter_present) {
        return response_with_code(res, dt::ResponseCode::FAILED_WrongChargeParameter);
    }

    // A DC session needs the DC_EVChargeParameter: without it there are no EV limits to provision the
    // power supply with and nothing to report as the EV's charging needs. Wrong charge parameter, the
    // same answer ISO 15118-2 gives for a DC request lacking its DC_EVChargeParameter.
    if (not req.dc_ev_charge_parameter.has_value()) {
        return response_with_code(res, dt::ResponseCode::FAILED_WrongChargeParameter);
    }

    // Validate the EV-advertised DC maxima. EVMaximum{Current,Voltage,Power}Limit are non-negative
    // physical quantities; a negative value (e.g. EVMaximumCurrentLimit -100 A) is a wrong charge
    // parameter [V2G-DC-455], answered with FAILED_WrongChargeParameter (TC ..._din_charge_parameter_
    // discovery_005). The mandatory DC_EVSEChargeParameter was filled above so the FAILED response still
    // carries it. (DIN/-2 only: ISO 15118-20 BPT permits negative setpoints, so its handler must not.)
    const auto& evp = req.dc_ev_charge_parameter.value();
    if (evp.ev_maximum_current_limit < 0.0 or evp.ev_maximum_voltage_limit < 0.0 or
        (evp.ev_maximum_power_limit.has_value() and evp.ev_maximum_power_limit.value() < 0.0)) {
        return response_with_code(res, dt::ResponseCode::FAILED_WrongChargeParameter);
    }

    // An EV whose maximum current or voltage does not exceed the EVSE minimum cannot be served: the two
    // ranges do not overlap. Wrong charge parameter, with the EVSE announcing that it shuts down
    // (EvseV2G din_server.cpp; the same rule as its ISO 15118-2 handler).
    if (evp.ev_maximum_current_limit <= config.evse_minimum_current_limit or
        evp.ev_maximum_voltage_limit <= config.evse_minimum_voltage_limit) {
        res.dc_evse_charge_parameter->dc_evse_status.evse_status_code = dt::DcEvseStatusCode::EVSE_Shutdown;
        return response_with_code(res, dt::ResponseCode::FAILED_WrongChargeParameter);
    }

    res.evse_processing = processing_finished ? dt::EvseProcessing::Finished : dt::EvseProcessing::Ongoing;

    // A SAScheduleList is mandatory once EVSEProcessing is Finished (EvseV2G always sends one). A single
    // PMaxSchedule tuple advertising the EVSE max power (capped at the DIN PMax short range).
    if (processing_finished) {
        res.sa_schedule_list = build_sa_schedule_list(config);

        // IEC 61851-23:2023 CC.3.5.3: the charger has no energy for this session, so tell the EV to stop
        // rather than let it run into a charge loop with no power. NotificationMaxDelay 0 asks for an
        // immediate reaction (EvseV2G din_server.cpp does the same for every pause mode).
        if (config.no_energy_pause != d20::NoEnergyPauseMode::None) {
            res.dc_evse_charge_parameter->dc_evse_status.evse_notification = dt::EvseNotification::StopCharging;
            res.dc_evse_charge_parameter->dc_evse_status.notification_max_delay = 0;
        }
    }

    return response_with_code(res, dt::ResponseCode::OK);
}

void ChargeParameterDiscovery::enter() {
    logf_debug("Enter state: ChargeParameterDiscovery");
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
        // EIM/SIL: the DC parameters are available immediately, so EVSEProcessing finishes at once.
        const auto res = handle_request(*req, m_ctx.session_config, true, m_ctx.get_session_id(),
                                        m_ctx.charger_stop_requested, m_ctx.error_status_code());
        m_ctx.respond(res);

        if (res.response_code >= dt::ResponseCode::FAILED) {
            m_ctx.session_stopped = true;
            return {};
        }

        // Forward the EV's advertised maxima so the power supply is provisioned for the actual EV limits
        // (EvseV2G din_server.cpp:225-238); the SIL "falling back to 500V" root cause. Done only after the
        // parameters validated OK, so a rejected (e.g. negative) limit is never pushed to the supply.
        // An OK response guarantees the DC_EVChargeParameter (handle_request rejects its absence).
        const auto& p = req->dc_ev_charge_parameter.value();
        m_ctx.report_ev_status(p.dc_ev_status);

        session::feedback::DcMaximumLimits limits{};
        limits.voltage = static_cast<float>(p.ev_maximum_voltage_limit);
        limits.current = static_cast<float>(p.ev_maximum_current_limit);
        // EVMaximumPowerLimit is optional in DC_EVChargeParameterType (unlike the voltage and current
        // limits above); leave it unset when the EV omitted it rather
        // than deriving voltage * current, which would put a number the EV never sent into
        // dc_ev_maximum_limits and from there into ev_info.maximum_power_limit (EvseV2G din_server.cpp:300
        // forwards the EVMaximumPowerLimit_isUsed flag instead).
        if (p.ev_maximum_power_limit.has_value()) {
            limits.power = static_cast<float>(p.ev_maximum_power_limit.value());
        }
        m_ctx.feedback.dc_max_limits(limits);

        // The per-session EV facts the module surfaces as ev_info (battery capacity, energy request,
        // full/bulk SoC) and as the OCPP ChargingNeeds notification. DIN SPEC 70121 carries no
        // DepartureTime and no AC parameters.
        session::feedback::EvChargeParameters parameters{};
        parameters.requested_energy_transfer = req->ev_requested_energy_transfer_type;
        parameters.dc = to_dc_ev_charge_parameters(p);
        m_ctx.feedback.ev_charge_parameters(parameters);

        if (res.evse_processing == dt::EvseProcessing::Finished) {
            // No energy for this session: stay here instead of moving on to the cable check, which the
            // charger has no power for. The EV is expected to answer the StopCharging notification with a
            // SessionStopReq (handled above); anything else runs into the sequence-error path below.
            // This mirrors EvseV2G, which parks DIN in WAIT_FOR_SESSIONSTOP for both stopping modes --
            // DIN always pauses before the cable check, unlike ISO 15118-2. AllowEvToIgnorePause
            // deliberately proceeds: the notification was sent, but an EV ignoring it may charge on.
            if (m_ctx.session_config.no_energy_pause == d20::NoEnergyPauseMode::BeforeCableCheck or
                m_ctx.session_config.no_energy_pause == d20::NoEnergyPauseMode::AfterCableCheckPreCharge) {
                logf_info("No energy available, pausing the DIN session before the cable check "
                          "(IEC 61851-23:2023 CC.3.5.3)");
                return {};
            }
            return m_ctx.create_state<CableCheck>();
        }
        return {};
    }

    logf_warning("Expected ChargeParameterDiscoveryReq! But code type id: %d", variant->get_type());
    // [V2G-DC-539]: answer with the received-type response carrying FAILED_SequenceError, then close.
    respond_sequence_error(m_ctx, *variant);
    m_ctx.session_stopped = true;
    return {};
}

} // namespace iso15118::din::state
