// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/d2/state/charge_parameter_discovery.hpp>

#include <cmath>

#include <iso15118/d2/state/cable_check.hpp>
#include <iso15118/d2/state/power_delivery.hpp>
#include <iso15118/d2/state/pre_charge.hpp>
#include <iso15118/d2/state/session_stop.hpp>

#include <iso15118/message/ac_charge_parameter_discovery.hpp>
#include <iso15118/message/common_types.hpp>

#include <iso15118/detail/d2/state/charge_parameter_discovery.hpp>

#include <everest/util/misc/container.hpp>
#include <iso15118/detail/d2/state/sequence_error.hpp>
#include <iso15118/detail/d2/state/state_helper.hpp>
#include <iso15118/detail/helper.hpp>

namespace iso15118::d2::state {

using dt::to_physical_value;
using dt::Unit;

dt::SAScheduleList build_sa_schedule_list(const d2::SessionConfig& config, dt::EnergyTransferMode mode,
                                          std::optional<uint32_t> departure_time) {
    dt::SAScheduleList list;
    auto& tuple = list.emplace_back();
    tuple.sa_schedule_tuple_id = 1; // [V2G2-773]: must be in 1..255
    auto& entry = tuple.pmax_schedule.emplace_back();
    entry.start = 0;
    // [V2G2-303]: honour the EV's requested DepartureTime as the schedule horizon, else the configured
    // default. While pausing for lack of energy a short horizon is offered regardless -- the EV is told
    // to come back rather than to plan a day of charging.
    constexpr uint32_t PAUSE_DURATION = 60 * 30;
    entry.duration = (departure_time.has_value() and departure_time.value() > 0) ? departure_time.value()
                                                                                 : config.sa_schedule_duration;
    if (config.no_energy_pause != d20::NoEnergyPauseMode::None) {
        entry.duration = PAUSE_DURATION;
    }
    // PMax advertises the hardware capability, not the current energy-management grant, which reaches
    // the EV in the charge loop. ac_capability_max_current is per phase, so a three-phase request lands
    // back exactly on the hardware charge power the module reported (see d2_secc_engine.cpp).
    const float ac_phases = (mode == dt::EnergyTransferMode::AC_single_phase_core) ? 1.0f : 3.0f;
    const float pmax = is_dc_mode(mode) ? config.dc_capability_max_power
                                        : (config.ac_capability_max_current * config.ac_nominal_voltage * ac_phases);
    entry.p_max = to_physical_value(pmax, Unit::W);
    return list;
}

namespace {

namespace m20dt = message_20::datatypes;

// Provisions the power supply for the actual EV limits instead of the SECC defaults, and carries the
// per-session EV facts the module surfaces as ev_info and as the OCPP ChargingNeeds notification.
void forward_ev_limits(const message_2::ChargeParameterDiscoveryRequest& req, bool is_dc,
                       const session::Feedback& feedback) {
    session::feedback::EvChargeParameters parameters{};
    parameters.requested_energy_transfer = req.requested_energy_transfer_mode;

    if (is_dc) {
        if (not req.dc_ev_charge_parameter.has_value()) {
            return;
        }
        const auto& p = req.dc_ev_charge_parameter.value();
        session::feedback::DcMaximumLimits limits{};
        limits.voltage = static_cast<float>(dt::from_physical_value(p.ev_maximum_voltage_limit));
        limits.current = static_cast<float>(dt::from_physical_value(p.ev_maximum_current_limit));
        // EVMaximumPowerLimit is optional, unlike the limits above: leave it unset when the EV omitted it
        // rather than deriving voltage * current, which would put a number the EV never sent into ev_info.
        if (p.ev_maximum_power_limit.has_value()) {
            limits.power = static_cast<float>(dt::from_physical_value(p.ev_maximum_power_limit.value()));
        }
        feedback.dc_max_limits(limits);

        auto& dc = parameters.dc.emplace();
        dc.max_current = limits.current;
        dc.max_voltage = limits.voltage;
        dc.max_power = limits.power;
        if (p.ev_energy_capacity.has_value()) {
            dc.energy_capacity = static_cast<float>(dt::from_physical_value(p.ev_energy_capacity.value()));
        }
        if (p.ev_energy_request.has_value()) {
            dc.energy_request = static_cast<float>(dt::from_physical_value(p.ev_energy_request.value()));
        }
        dc.full_soc = p.full_soc;
        dc.bulk_soc = p.bulk_soc;
        dc.ress_soc = p.dc_ev_status.ev_ress_soc;
        parameters.departure_time = p.departure_time;
    } else if (req.ac_ev_charge_parameter.has_value()) {
        const auto& p = req.ac_ev_charge_parameter.value();
        // The ISO-2 AC EV limits are voltage/current while the module's AC feedback carries charge power.
        const auto max_v = dt::from_physical_value(p.ev_max_voltage);
        const auto max_i = dt::from_physical_value(p.ev_max_current);
        const auto min_i = dt::from_physical_value(p.ev_min_current);
        m20dt::AC_CPDReqEnergyTransferMode mode{};
        mode.max_charge_power = m20dt::from_float(static_cast<float>(max_v * max_i));
        mode.min_charge_power = m20dt::from_float(static_cast<float>(max_v * min_i));
        feedback.ac_limits(mode);

        auto& ac = parameters.ac.emplace();
        ac.e_amount = static_cast<float>(dt::from_physical_value(p.e_amount));
        ac.max_voltage = static_cast<float>(max_v);
        ac.max_current = static_cast<float>(max_i);
        ac.min_current = static_cast<float>(min_i);
        parameters.departure_time = p.departure_time;
    } else {
        return;
    }

    feedback.ev_charge_parameters(parameters);
}

void fill_dc(message_2::ChargeParameterDiscoveryResponse& res, const d2::SessionConfig& config) {
    auto& dc = res.dc_evse_charge_parameter.emplace();
    dc.dc_evse_status.notification = dt::EVSENotification::None;
    dc.dc_evse_status.notification_max_delay = 0;
    dc.dc_evse_status.isolation_status = dt::IsolationLevel::Invalid;
    dc.dc_evse_status.status_code = dt::DC_EVSEStatusCode::EVSE_Ready;
    // [V2G2-315] wants the maximum the EVSE can deliver, so a temporary energy-management restriction
    // must not shrink the offer -- the live limits reach the EV in every CurrentDemandRes instead.
    dc.evse_maximum_current_limit = to_physical_value(config.dc_capability_max_current, Unit::A);
    dc.evse_maximum_power_limit = to_physical_value(config.dc_capability_max_power, Unit::W);
    dc.evse_maximum_voltage_limit = to_physical_value(config.dc_capability_max_voltage, Unit::V);
    dc.evse_minimum_current_limit = to_physical_value(config.dc_min_current, Unit::A);
    dc.evse_minimum_voltage_limit = to_physical_value(config.dc_min_voltage, Unit::V);
    dc.evse_peak_current_ripple = to_physical_value(config.dc_peak_current_ripple, Unit::A);
    // Optional elements, sent only when the module reported them (set_charging_parameters).
    if (config.dc_current_regulation_tolerance.has_value()) {
        dc.evse_current_regulation_tolerance =
            to_physical_value(config.dc_current_regulation_tolerance.value(), Unit::A);
    }
    if (config.dc_energy_to_be_delivered.has_value()) {
        dc.evse_energy_to_be_delivered = to_physical_value(config.dc_energy_to_be_delivered.value(), Unit::Wh);
    }
}

// IEC 61851-23:2023 CC.3.5.3: no energy for this session, so tell the EV to stop rather than let it
// run into a charge loop with no power. A pause before the cable check asks for an immediate
// reaction; the later ones grant the EV a grace period.
void apply_no_energy_pause(message_2::ChargeParameterDiscoveryResponse& res, const d2::SessionConfig& config) {
    if (config.no_energy_pause == d20::NoEnergyPauseMode::None or not res.dc_evse_charge_parameter.has_value()) {
        return;
    }
    constexpr uint16_t PAUSE_NOTIFICATION_DELAY_S = 300;
    auto& status = res.dc_evse_charge_parameter->dc_evse_status;
    status.notification = dt::EVSENotification::StopCharging;
    status.notification_max_delay =
        (config.no_energy_pause == d20::NoEnergyPauseMode::BeforeCableCheck) ? 0 : PAUSE_NOTIFICATION_DELAY_S;
}

void fill_ac(message_2::ChargeParameterDiscoveryResponse& res, const d2::SessionConfig& config) {
    auto& ac = res.ac_evse_charge_parameter.emplace();
    ac.ac_evse_status = make_ac_evse_status();
    ac.evse_nominal_voltage = to_physical_value(config.ac_nominal_voltage, Unit::V);
    // Hardware capability, like the DC maxima above; the live per-phase limit goes in ChargingStatusRes.
    ac.evse_max_current = to_physical_value(config.ac_capability_max_current, Unit::A);
}

} // namespace

namespace {
void apply_charger_stop(message_2::ChargeParameterDiscoveryResponse& res, bool charger_stop) {
    if (not charger_stop) {
        return;
    }
    if (res.dc_evse_charge_parameter.has_value()) {
        auto& status = res.dc_evse_charge_parameter->dc_evse_status;
        status.notification = dt::EVSENotification::StopCharging;
        status.notification_max_delay = 0;
        status.status_code = dt::DC_EVSEStatusCode::EVSE_Shutdown;
    }
    if (res.ac_evse_charge_parameter.has_value()) {
        auto& status = res.ac_evse_charge_parameter->ac_evse_status;
        status.notification = dt::EVSENotification::StopCharging;
        status.notification_max_delay = 0;
    }
}

message_2::ChargeParameterDiscoveryResponse build_response(const message_2::ChargeParameterDiscoveryRequest& req,
                                                           const dt::SessionId& session_id,
                                                           const d2::SessionConfig& config) {
    message_2::ChargeParameterDiscoveryResponse res;
    res.header.session_id = session_id;

    const auto mode = req.requested_energy_transfer_mode;
    const auto& modes = config.supported_energy_transfer_modes;
    const bool is_dc = is_dc_mode(mode);

    if (not everest::lib::util::exists(modes, mode)) {
        res.response_code = dt::ResponseCode::FAILED_WrongEnergyTransferMode;
        res.evse_processing = dt::EVSEProcessing::Finished;
        // [V2G2-736]: a FAILED response must still carry every XSD-mandatory parameter.
        if (is_dc) {
            fill_dc(res, config);
        } else {
            fill_ac(res, config);
        }
        return res;
    }

    // The parameter phase is always Finished (limits are known up front, EvseV2G parity).
    res.evse_processing = dt::EVSEProcessing::Finished;

    std::optional<uint32_t> departure_time;
    if (is_dc) {
        if (not req.dc_ev_charge_parameter.has_value()) {
            res.response_code = dt::ResponseCode::FAILED_WrongChargeParameter;
            fill_dc(res, config); // [V2G2-736]: mandatory parameter even on FAILED
            return res;
        }
        // A negative EVMaximum*Limit is a wrong charge parameter [V2G2-477] (TC ..._004). DIN/-2 only:
        // ISO 15118-20 BPT permits negative setpoints, so its handler must not reject on sign.
        const auto& evp = req.dc_ev_charge_parameter.value();
        if (dt::from_physical_value(evp.ev_maximum_current_limit) < 0.0 or
            dt::from_physical_value(evp.ev_maximum_voltage_limit) < 0.0 or
            (evp.ev_maximum_power_limit.has_value() and
             dt::from_physical_value(evp.ev_maximum_power_limit.value()) < 0.0)) {
            res.response_code = dt::ResponseCode::FAILED_WrongChargeParameter;
            fill_dc(res, config); // [V2G2-736]: mandatory parameter even on FAILED
            return res;
        }
        // An EV whose maximum does not exceed the EVSE minimum cannot be served -- the ranges do not
        // overlap -- so answer wrong charge parameter with the EVSE announcing that it shuts down.
        if (dt::from_physical_value(evp.ev_maximum_current_limit) <= config.dc_min_current or
            dt::from_physical_value(evp.ev_maximum_voltage_limit) <= config.dc_min_voltage) {
            res.response_code = dt::ResponseCode::FAILED_WrongChargeParameter;
            fill_dc(res, config); // [V2G2-736]: mandatory parameter even on FAILED
            res.dc_evse_charge_parameter->dc_evse_status.status_code = dt::DC_EVSEStatusCode::EVSE_Shutdown;
            return res;
        }
        departure_time = req.dc_ev_charge_parameter->departure_time;
        fill_dc(res, config);
        apply_no_energy_pause(res, config);
    } else {
        if (not req.ac_ev_charge_parameter.has_value()) {
            res.response_code = dt::ResponseCode::FAILED_WrongChargeParameter;
            fill_ac(res, config); // [V2G2-736]: mandatory parameter even on FAILED
            return res;
        }
        departure_time = req.ac_ev_charge_parameter->departure_time;
        fill_ac(res, config);
    }

    res.sa_schedule_list = build_sa_schedule_list(config, mode, departure_time);

    res.response_code = dt::ResponseCode::OK;
    return res;
}
} // namespace

// [V2G2-366]: a module-reported fault wins over both the EVSE_Ready this state would claim and the
// EVSE_Shutdown of a charger-initiated stop, being the more specific reason. [V2G2-880] keeps it
// informational: the codes tell the EV what is going on, they do not steer charging.
void apply_evse_error(message_2::ChargeParameterDiscoveryResponse& res,
                      std::optional<dt::DC_EVSEStatusCode> error_status_code, bool rcd_error) {
    if (error_status_code.has_value() and res.dc_evse_charge_parameter.has_value()) {
        res.dc_evse_charge_parameter->dc_evse_status.status_code = error_status_code.value();
    }
    if (res.ac_evse_charge_parameter.has_value()) {
        res.ac_evse_charge_parameter->ac_evse_status.rcd = rcd_error;
    }
}

message_2::ChargeParameterDiscoveryResponse handle_request(const message_2::ChargeParameterDiscoveryRequest& req,
                                                           const dt::SessionId& session_id,
                                                           const d2::SessionConfig& config, bool charger_stop,
                                                           std::optional<dt::DC_EVSEStatusCode> error_status_code,
                                                           bool rcd_error) {
    auto res = build_response(req, session_id, config);
    apply_charger_stop(res, charger_stop);
    apply_evse_error(res, error_status_code, rcd_error);
    return res;
}

void ChargeParameterDiscovery::enter() {
    logf_debug("Enter state: ChargeParameterDiscovery");
}

Result ChargeParameterDiscovery::on_request(const message_2::Variant& received) {
    // [V2G2-688] loops the request back here while the SECC still answers EVSEProcessing=Ongoing.
    const auto type = received.get_type();
    if (type == message_2::Type::ChargeParameterDiscoveryReq) {
        return process_charge_parameter_discovery(m_ctx, received.get<message_2::ChargeParameterDiscoveryRequest>());
    } else {
        logf_warning("Expected ChargeParameterDiscoveryReq! But got type id: %d", received.get_type());
        respond_sequence_error(m_ctx, received.get_type());
        return {};
    }
}

// An action, not a wait state: also reached from PostCharge when the EV restarts the parameter
// exchange after PowerDelivery(Stop) ([V2G2-601]).
Result process_charge_parameter_discovery(Context& m_ctx, const message_2::ChargeParameterDiscoveryRequest& req) {
    if (req.dc_ev_charge_parameter.has_value()) {
        m_ctx.report_ev_status(req.dc_ev_charge_parameter->dc_ev_status);
    }

    const bool is_dc = is_dc_mode(req.requested_energy_transfer_mode);

    auto res = handle_request(req, m_ctx.get_session_id(), m_ctx.session_config, m_ctx.evse().charger_stop_requested,
                              m_ctx.error_status_code(), m_ctx.rcd_error());
    // The builder reports Invalid isolation, correct for the initial exchange. On the [V2G2-813]
    // renegotiation path the isolation was verified, so report the module's value instead.
    if (res.dc_evse_charge_parameter.has_value()) {
        apply_isolation_status(m_ctx, res.dc_evse_charge_parameter->dc_evse_status);
    }
    m_ctx.respond(res);

    if (res.response_code >= dt::ResponseCode::FAILED) {
        m_ctx.session_stopped = true;
        return {};
    }

    // Only after they validated OK, so a rejected limit is never pushed to the power supply.
    forward_ev_limits(req, is_dc, m_ctx.feedback);

    if (res.sa_schedule_list.has_value() and not res.sa_schedule_list->empty()) {
        m_ctx.set_sa_schedules(res.sa_schedule_list.value(), res.sa_schedule_list->front().sa_schedule_tuple_id);
    }

    if (is_dc) {
        // No energy, and the charger cannot even run the cable check: skip it and wait for the EV to react
        // to the StopCharging notification. PreCharge hands any non-PreChargeReq to PowerDelivery, so both a
        // stubborn PreChargeReq and the expected PowerDeliveryReq(Stop) / SessionStopReq are accepted.
        if (m_ctx.session_config.no_energy_pause == d20::NoEnergyPauseMode::BeforeCableCheck) {
            logf_info("No energy available, skipping the cable check (IEC 61851-23:2023 CC.3.5.3)");
            return m_ctx.create_state<PreChargeStart>();
        }
        return m_ctx.create_state<CableCheck>();
    }
    return m_ctx.create_state<AcPowerDelivery>();
}

} // namespace iso15118::d2::state
