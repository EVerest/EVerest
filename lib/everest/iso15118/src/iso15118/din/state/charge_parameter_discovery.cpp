// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/din/state/charge_parameter_discovery.hpp>

#include <cmath>

#include <iso15118/din/state/cable_check.hpp>
#include <iso15118/din/state/session_stop.hpp>

#include <iso15118/detail/din/state/charge_parameter_discovery.hpp>
#include <iso15118/detail/din/state/constants.hpp>
#include <iso15118/detail/din/state/sequence_error.hpp>
#include <iso15118/detail/din/state/session_stop.hpp>
#include <iso15118/detail/din/state/state_helper.hpp>
#include <iso15118/detail/helper.hpp>

namespace iso15118::din::state {

namespace {
constexpr int16_t DIN_PMAX_MAX = 32767; // SHRT_MAX; DIN PMax is a raw short in watts

dt::SAScheduleList build_sa_schedule_list(const SessionConfig& config) {
    dt::SAScheduleList list;
    auto& tuple = list.emplace_back();
    tuple.sa_schedule_tuple_id = 1;
    tuple.pmax_schedule_id = 1;
    auto& entry = tuple.pmax_schedule.emplace_back();
    entry.start = 0;
    entry.duration =
        (config.no_energy_pause == d20::NoEnergyPauseMode::None) ? DIN_SA_SCHEDULE_DURATION : DIN_PAUSE_DURATION;
    // PMax advertises the hardware capability, not the current energy-management grant, which reaches
    // the EV in every CurrentDemandRes. An unreported capability advertises 0, never an invented value.
    const double pmax = config.evse_capability_maximum_power_limit.value_or(0.0);
    entry.p_max = (pmax > static_cast<double>(DIN_PMAX_MAX)) ? DIN_PMAX_MAX : static_cast<int16_t>(pmax);
    return list;
}
} // namespace

namespace {
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

// [V2G-DC-397]: the EV may only request the mode the SECC offered, and the ChargeService advertises
// exactly one -- so this is a direct match of two enums that happen to be separate types.
bool requested_mode_is_offered(dt::EnergyTransferMode requested, dt::SupportedEnergyTransferMode offered) {
    switch (offered) {
    case dt::SupportedEnergyTransferMode::DC_core:
        return requested == dt::EnergyTransferMode::DC_core;
    case dt::SupportedEnergyTransferMode::DC_extended:
        return requested == dt::EnergyTransferMode::DC_extended;
    default:
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

    // DC_EVSEChargeParameter is mandatory in the response, so populate it before the checks below.
    // [V2G-DC-638]: a module-reported fault wins over EVSE_Ready and over a stop's EVSE_Shutdown.
    dt::DcEvseChargeParameter param;
    param.dc_evse_status.evse_status_code = error_status_code.value_or(
        charger_stop ? dt::DcEvseStatusCode::EVSE_Shutdown : dt::DcEvseStatusCode::EVSE_Ready);
    param.dc_evse_status.evse_notification = dt::EvseNotification::None;
    param.evse_maximum_current_limit = config.evse_capability_maximum_current_limit;
    param.evse_maximum_power_limit = config.evse_capability_maximum_power_limit;
    param.evse_maximum_voltage_limit = config.evse_capability_maximum_voltage_limit;
    param.evse_minimum_current_limit = config.evse_minimum_current_limit;
    param.evse_minimum_voltage_limit = config.evse_minimum_voltage_limit;
    param.evse_peak_current_ripple = config.evse_peak_current_ripple;
    param.evse_current_regulation_tolerance = config.evse_current_regulation_tolerance;
    param.evse_energy_to_be_delivered = config.evse_energy_to_be_delivered;
    res.dc_evse_charge_parameter = param;
    // [V2G-DC-397]: an EV asking for DC_core against a DC_extended-only EVSE has to be rejected.
    if (not requested_mode_is_offered(req.ev_requested_energy_transfer_type, config.energy_transfer_mode)) {
        return response_with_code(res, dt::ResponseCode::FAILED_WrongEnergyTransferType);
    }

    // [V2G-DC-398]: a DC session must not carry AC_EVChargeParameter.
    if (req.ac_ev_charge_parameter_present) {
        return response_with_code(res, dt::ResponseCode::FAILED_WrongChargeParameter);
    }

    // Without it there are no EV limits to provision the power supply with, and nothing to report as the
    // EV's charging needs.
    if (not req.dc_ev_charge_parameter.has_value()) {
        return response_with_code(res, dt::ResponseCode::FAILED_WrongChargeParameter);
    }

    // A negative EVMaximum*Limit is a wrong charge parameter [V2G-DC-455] (TC ..._005). DIN/-2 only:
    // ISO 15118-20 BPT permits negative setpoints, so its handler must not reject on sign.
    const auto& evp = req.dc_ev_charge_parameter.value();
    if (evp.ev_maximum_current_limit < 0.0 or evp.ev_maximum_voltage_limit < 0.0 or
        (evp.ev_maximum_power_limit.has_value() and evp.ev_maximum_power_limit.value() < 0.0)) {
        return response_with_code(res, dt::ResponseCode::FAILED_WrongChargeParameter);
    }

    // An EV whose maximum does not exceed the EVSE minimum cannot be served -- the ranges do not
    // overlap -- so answer wrong charge parameter with the EVSE announcing that it shuts down.
    if (evp.ev_maximum_current_limit <= config.evse_minimum_current_limit or
        evp.ev_maximum_voltage_limit <= config.evse_minimum_voltage_limit) {
        res.dc_evse_charge_parameter->dc_evse_status.evse_status_code = dt::DcEvseStatusCode::EVSE_Shutdown;
        return response_with_code(res, dt::ResponseCode::FAILED_WrongChargeParameter);
    }

    res.evse_processing = processing_finished ? dt::EvseProcessing::Finished : dt::EvseProcessing::Ongoing;

    // Mandatory once EVSEProcessing is Finished: one tuple advertising the EVSE max power.
    if (processing_finished) {
        res.sa_schedule_list = build_sa_schedule_list(config);

        // IEC 61851-23:2023 CC.3.5.3: no energy for this session, so tell the EV to stop rather than let it
        // run into a charge loop with no power. NotificationMaxDelay 0 asks for an immediate reaction.
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

Result ChargeParameterDiscovery::on_request(const message_din::Variant& received) {
    if (const auto req = received.get_if<message_din::ChargeParameterDiscoveryRequest>()) {
        // EIM/SIL: the DC parameters are available immediately, so EVSEProcessing finishes at once.
        const auto res = handle_request(*req, m_ctx.session_config, true, m_ctx.get_session_id(),
                                        m_ctx.evse().charger_stop_requested, m_ctx.error_status_code());
        m_ctx.respond(res);

        if (res.response_code >= dt::ResponseCode::FAILED) {
            m_ctx.session_stopped = true;
            return {};
        }

        // Provisions the power supply for the actual EV limits -- the SIL "falling back to 500V" root cause.
        // Only after the parameters validated OK, so a rejected limit is never pushed to the supply.
        const auto& p = req->dc_ev_charge_parameter.value();
        m_ctx.report_ev_status(p.dc_ev_status);

        session::feedback::DcMaximumLimits limits{};
        limits.voltage = static_cast<float>(p.ev_maximum_voltage_limit);
        limits.current = static_cast<float>(p.ev_maximum_current_limit);
        // EVMaximumPowerLimit is optional, unlike the limits above: leave it unset when the EV omitted it
        // rather than deriving voltage * current, which would put a number the EV never sent into ev_info.
        if (p.ev_maximum_power_limit.has_value()) {
            limits.power = static_cast<float>(p.ev_maximum_power_limit.value());
        }
        m_ctx.feedback.dc_max_limits(limits);

        // DIN SPEC 70121 carries no DepartureTime and no AC parameters.
        session::feedback::EvChargeParameters parameters{};
        parameters.requested_energy_transfer = req->ev_requested_energy_transfer_type;
        parameters.dc = to_dc_ev_charge_parameters(p);
        m_ctx.feedback.ev_charge_parameters(parameters);

        if (res.evse_processing == dt::EvseProcessing::Finished) {
            // No energy for this session: park in SessionStop rather than move on to a cable check the charger
            // has no power for. The EV is expected to answer the StopCharging notification with a SessionStopReq,
            // which is the only request in sequence there. Section 9.7.4.2.4 does not cover the no-energy case at
            // all, so no requirement compels this; it is chosen for the tighter accepted set (staying here would
            // keep admitting a ChargeParameterDiscoveryReq the charger cannot act on) and for EvseV2G parity.
            // AllowEvToIgnorePause deliberately proceeds: the notification was sent, but an EV ignoring it may
            // charge on.
            if (m_ctx.session_config.no_energy_pause == d20::NoEnergyPauseMode::BeforeCableCheck or
                m_ctx.session_config.no_energy_pause == d20::NoEnergyPauseMode::AfterCableCheckPreCharge) {
                logf_info("No energy available, pausing the DIN session before the cable check "
                          "(IEC 61851-23:2023 CC.3.5.3)");
                return m_ctx.create_state<SessionStop>();
            }
            return m_ctx.create_state<CableCheck>();
        }
        return {};
    }

    // [V2G-DC-495]/[V2G-DC-966] admit a SessionStopReq here.
    if (const auto stop = received.get_if<message_din::SessionStopRequest>()) {
        return process_session_stop(m_ctx, *stop);
    }

    logf_warning("Expected ChargeParameterDiscoveryReq or SessionStopReq! But code type id: %d", received.get_type());
    respond_sequence_error(m_ctx, received);
    m_ctx.session_stopped = true;
    return {};
}

} // namespace iso15118::din::state
