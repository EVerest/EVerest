// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/session/d2_secc_engine.hpp>

#include <algorithm>
#include <memory>
#include <utility>

#include <iso15118/d2/state/session_setup.hpp>

#include <everest/util/misc/container.hpp>

#include <iso15118/detail/d2/vas.hpp>
#include <iso15118/detail/helper.hpp>
#include <iso15118/detail/session/d2_secc_engine.hpp>
#include <iso15118/message/common_types.hpp>
#include <iso15118/message_2/variant.hpp>

namespace iso15118 {

namespace m2dt = message_2::datatypes;
namespace m20dt = message_20::datatypes;

namespace {
// True when the limit set carries at least one positive maximum, i.e. the module actually reported it.
bool has_dc_maxima(const d20::DcTransferLimits& dc) {
    return m20dt::from_RationalNumber(dc.charge_limits.power.max) > 0.0f or
           m20dt::from_RationalNumber(dc.charge_limits.current.max) > 0.0f or
           m20dt::from_RationalNumber(dc.voltage.max) > 0.0f;
}

// The number of phases the EVSE's AC hardware can energise, read back from the advertised energy
// transfer modes (EvseManager derives those from max_phase_count_import).
//
// update_ac_maximum_limits reports the AC charge power SUMMED OVER ALL PHASES, while both consumers of
// the derived capability current need it PER PHASE: AC_EVSEChargeParameter.EVSEMaxCurrent is a per-phase
// value, and the SASchedule PMax multiplies it by the phase count of the mode the EV requested. Dividing
// the reported power by the phase count here is what makes PMax(AC_three_phase_core) come out as the
// hardware power instead of three times it. This is a deliberate deviation from EvseV2G, which divides
// the total power by the nominal voltage alone (charger/ISO15118_chargerImpl.cpp
// handle_update_ac_maximum_limits) and therefore over-reports both values by the phase count on a
// three-phase charger.
uint8_t ac_capability_phase_count(const d2::SessionConfig& out) {
    // ISO 15118-2 knows single- and three-phase AC only; a two-phase charger is advertised as
    // single-phase by EvseManager's mode mapping.
    return everest::lib::util::exists(out.supported_energy_transfer_modes,
                                      m2dt::EnergyTransferMode::AC_three_phase_core)
               ? 3
               : 1;
}
} // namespace

// Builds the SECC-side ISO 15118-2 config from the generic d20 EvseSetupConfig-derived SessionConfig.
d2::SessionConfig make_d2_config(const session::SessionConfig& config, bool tls_active) {
    d2::SessionConfig out;

    // ISO 15118-2 evseIDType is a string of min length 7 (max 37). A shorter/empty configured id would
    // encode a schema-invalid SessionSetupRes, so fall back to the library default in that case.
    static constexpr size_t ISO2_EVSE_ID_MIN_LEN = 7;
    if (config.evse_id.size() >= ISO2_EVSE_ID_MIN_LEN) {
        out.evse_id = config.evse_id;
    } else {
        logf_warning("Configured EVSEID '%s' is shorter than the ISO 15118-2 minimum of %zu characters; "
                     "using the default '%s'",
                     config.evse_id.c_str(), ISO2_EVSE_ID_MIN_LEN, out.evse_id.c_str());
    }
    out.tls_active = tls_active;

    // Advertise exactly the pre-20 modes the module configured (update_energy_transfer_modes). The
    // -20 service categories below are a lossy fallback for a module that never provided them: they
    // cannot distinguish DC_core from DC_extended, or carry DC_combo_core / DC_unique at all.
    if (not config.pre20_energy_transfer_modes.empty()) {
        for (const auto mode : config.pre20_energy_transfer_modes) {
            // Deduplicate: the advertised list is a fixed_vector sized for the six distinct modes.
            if (not everest::lib::util::exists(out.supported_energy_transfer_modes, mode)) {
                out.supported_energy_transfer_modes.push_back(mode);
            }
        }
    } else {
        bool has_dc = false;
        bool has_ac = false;
        for (const auto& service : config.supported_energy_transfer_services) {
            if (service == m20dt::ServiceCategory::DC or service == m20dt::ServiceCategory::DC_BPT or
                service == m20dt::ServiceCategory::MCS or service == m20dt::ServiceCategory::MCS_BPT) {
                has_dc = true;
            } else if (service == m20dt::ServiceCategory::AC or service == m20dt::ServiceCategory::AC_BPT) {
                has_ac = true;
            }
        }
        // Fall back to DC when the config carries no recognised energy service.
        if (not has_dc and not has_ac) {
            has_dc = true;
        }

        if (has_dc) {
            out.supported_energy_transfer_modes.push_back(m2dt::EnergyTransferMode::DC_extended);
            out.supported_energy_transfer_modes.push_back(m2dt::EnergyTransferMode::DC_core);
        }
        if (has_ac) {
            out.supported_energy_transfer_modes.push_back(m2dt::EnergyTransferMode::AC_three_phase_core);
            out.supported_energy_transfer_modes.push_back(m2dt::EnergyTransferMode::AC_single_phase_core);
        }
    }

    apply_dc_limits(out, config.dc_limits);
    // The ChargeParameterDiscoveryRes offer is the maximum the EVSE could ever deliver: the power-supply
    // hardware capabilities (set_powersupply_capabilities), not the live energy-management limits above.
    // A module that never reported capabilities (e.g. EvseManager's fake-DC/AC-with-SoC mode only seeds
    // update_dc_maximum_limits) falls back to those limits -- still reported data, never an invented
    // value. With neither reported the offer is 0 (safety).
    apply_dc_capabilities(out, has_dc_maxima(config.powersupply_limits) ? config.powersupply_limits : config.dc_limits);
    const bool offers_dc = std::any_of(out.supported_energy_transfer_modes.begin(),
                                       out.supported_energy_transfer_modes.end(), [](const auto mode) {
                                           return mode != m2dt::EnergyTransferMode::AC_single_phase_core and
                                                  mode != m2dt::EnergyTransferMode::AC_three_phase_core;
                                       });
    if (offers_dc and out.dc_capability_max_power <= 0.0f) {
        logf_warning("No DC power-supply capabilities or limits were reported; the "
                     "ChargeParameterDiscoveryRes will offer 0 W");
    }
    apply_physical_values(out, config.physical_values);

    // AC nominal voltage is not represented in the d20 limits; it comes from the module's
    // set_charging_parameters (applied above) and otherwise stays at the 230 V default. The capability
    // current (CPD EVSEMaxCurrent / PMax) is derived from the hardware AC charge power
    // (update_ac_maximum_limits) at that voltage, per phase (see ac_capability_phase_count); when the
    // module never reported it, the live limit -- already a per-phase current -- doubles as the
    // capability. With neither reported both stay 0 (safety: no invented values).
    const auto ac_power = m20dt::from_RationalNumber(config.ac_limits.charge_power.max);
    if (ac_power > 0.0f and out.ac_nominal_voltage > 0.0f) {
        out.ac_capability_max_current = ac_power / (out.ac_nominal_voltage * ac_capability_phase_count(out));
    } else if (config.iso2_ac_max_current.has_value()) {
        out.ac_capability_max_current = config.iso2_ac_max_current.value();
    }
    // EvseManager's update_ac_max_current (the live per-phase current limit, following external energy
    // limits) governs only the charge loop (ChargingStatusRes); mid-session changes arrive as
    // UpdateAcMaxCurrent control events (see on_control_event).
    out.ac_max_current = config.iso2_ac_max_current.value_or(out.ac_capability_max_current);

    // Plug-and-Charge (Contract payment) config, threaded from the module via EvseSetupConfig.
    out.pnc_enabled = config.iso2_pnc_enabled;
    // ExternalPayment is offered only when configured (session_setup payment_options containing
    // ExternalPayment maps to Authorization::EIM); a Contract-only SECC is permitted (EvseV2G parity).
    out.eim_enabled = std::find(config.authorization_services.begin(), config.authorization_services.end(),
                                m20dt::Authorization::EIM) != config.authorization_services.end();
    out.cert_install_service = config.cert_install_service;
    out.mo_root_cert_path = config.contract_mo_root_path;
    out.v2g_root_cert_path = config.contract_v2g_root_path;
    out.central_contract_validation_allowed = config.central_contract_validation_allowed;
    out.receipt_required = config.iso2_receipt_required;
    out.no_energy_pause = config.no_energy_pause;
    out.auth_timeout_eim_ms = session::auth_timeout_to_ms(config.auth_timeout_eim_s);
    out.auth_timeout_pnc_ms = session::auth_timeout_to_ms(config.auth_timeout_pnc_s);

    apply_vas_services(out, config.pre20_vas_services);

    return out;
}

// The external VAS offers as ISO 15118-2 Service entries (Table 105). ServiceID 1 is the charging service
// and ServiceID 2 the library's own Certificate service, so both are refused here; ServiceID 3 is the
// Internet access service. The ServiceList carries eight entries and the Certificate service takes one of
// them whenever it can be offered, so seven external services fit at most.
void apply_vas_services(d2::SessionConfig& out, const std::vector<session::VasService>& services) {
    static constexpr size_t NAME_MAX_LEN = 32;
    static constexpr size_t SCOPE_MAX_LEN = 64;
    static constexpr uint16_t INTERNET_SERVICE_ID = 3;

    const size_t capacity = out.offered_vas_services.max_size() - (out.cert_install_service ? 1 : 0);

    for (const auto& service : services) {
        if (service.id == out.charge_service_id or service.id == m2dt::CERTIFICATE_SERVICE_ID) {
            logf_warning("Ignoring external VAS with reserved ServiceID %u (charging / Certificate service)",
                         service.id);
            continue;
        }
        if (d2::is_offered_vas(out.offered_vas_services, service.id)) {
            logf_warning("Ignoring duplicate external VAS ServiceID %u", service.id);
            continue;
        }
        if (out.offered_vas_services.size() >= capacity) {
            logf_warning("ISO 15118-2 ServiceList is full (%zu external services); dropping VAS ServiceID %u", capacity,
                         service.id);
            continue;
        }

        m2dt::Service entry;
        entry.service_id = service.id;
        entry.free_service = service.free_service;
        if (service.id == INTERNET_SERVICE_ID) {
            entry.service_category = m2dt::ServiceCategory::Internet;
            entry.service_name = "InternetAccess";
        } else {
            entry.service_category = m2dt::ServiceCategory::OtherCustom;
            entry.service_name = service.name;
        }
        entry.service_scope = service.scope;

        if (entry.service_name.has_value() and entry.service_name->size() > NAME_MAX_LEN) {
            logf_warning("ServiceName of VAS %u exceeds %zu characters; truncated", service.id, NAME_MAX_LEN);
            entry.service_name->resize(NAME_MAX_LEN);
        }
        if (entry.service_scope.has_value() and entry.service_scope->size() > SCOPE_MAX_LEN) {
            logf_warning("ServiceScope of VAS %u exceeds %zu characters; truncated", service.id, SCOPE_MAX_LEN);
            entry.service_scope->resize(SCOPE_MAX_LEN);
        }
        out.offered_vas_services.push_back(std::move(entry));
    }
}

void apply_dc_limits(d2::SessionConfig& out, const d20::DcTransferLimits& dc) {
    // Safety: no invented values. A negative (invalid) limit clamps to 0 and an unreported one stays 0
    // -- only actually reported data ever tells the EV it may draw energy.
    const auto non_negative = [](float value) { return std::max(0.0f, value); };
    out.dc_max_power = non_negative(m20dt::from_RationalNumber(dc.charge_limits.power.max));
    out.dc_max_current = non_negative(m20dt::from_RationalNumber(dc.charge_limits.current.max));
    out.dc_max_voltage = non_negative(m20dt::from_RationalNumber(dc.voltage.max));
}

void apply_dc_capabilities(d2::SessionConfig& out, const d20::DcTransferLimits& dc) {
    // Safety: the advertised capability must never be invented. A negative (invalid) value clamps to 0
    // and an unreported one stays 0 -- only actually reported data ever advertises a positive offer, so
    // no default here (unlike the charge-loop values in apply_dc_limits).
    const auto non_negative = [](float value) { return std::max(0.0f, value); };
    out.dc_capability_max_power = non_negative(m20dt::from_RationalNumber(dc.charge_limits.power.max));
    out.dc_capability_max_current = non_negative(m20dt::from_RationalNumber(dc.charge_limits.current.max));
    out.dc_capability_max_voltage = non_negative(m20dt::from_RationalNumber(dc.voltage.max));
    out.dc_min_current = non_negative(m20dt::from_RationalNumber(dc.charge_limits.current.min));
    out.dc_min_voltage = non_negative(m20dt::from_RationalNumber(dc.voltage.min));
}

void apply_physical_values(d2::SessionConfig& out, const d20::PhysicalValues& values) {
    if (values.ac_nominal_voltage.has_value() and values.ac_nominal_voltage.value() > 0.0f) {
        out.ac_nominal_voltage = values.ac_nominal_voltage.value();
    }
    if (values.dc_peak_current_ripple.has_value()) {
        out.dc_peak_current_ripple = values.dc_peak_current_ripple.value();
    }
    if (values.dc_current_regulation_tolerance.has_value()) {
        out.dc_current_regulation_tolerance = values.dc_current_regulation_tolerance.value();
    }
    if (values.dc_energy_to_be_delivered.has_value()) {
        out.dc_energy_to_be_delivered = values.dc_energy_to_be_delivered.value();
    }
}

D2SeccEngine::D2SeccEngine(io::StreamOutputView output_view, const session::SessionConfig& config,
                           std::optional<d2::PauseContext>& pause_ctx, session::feedback::Callbacks callbacks,
                           d20::Timeouts& timeouts, bool tls_active) :
    message_exchange(output_view),
    ctx(std::move(callbacks), make_d2_config(config, tls_active), pause_ctx, active_control_event, message_exchange,
        timeouts),
    fsm(ctx.create_state<d2::state::SessionSetup>()) {
}

void D2SeccEngine::on_packet(io::v2gtp::PayloadType payload_type, const io::StreamInputView& view) {
    // All ISO 15118-2 messages share the single SAP payload type (0x8001); a frame carrying any other
    // V2GTP payload type is ignored (on par with the EvseV2G stack / libiso15118 finding F-001).
    if (payload_type != io::v2gtp::PayloadType::SAP) {
        return;
    }
    // disambiguation of the concrete message happens at decode.
    message_exchange.set_request(std::make_unique<message_2::Variant>(view));

    // Report the concrete incoming ISO 15118-2 request type so the module logs its real name.
    ctx.feedback.v2g_message(ctx.peek_request_type());

    drive_request(fsm, message_exchange, d2::Event::V2GTP_MESSAGE);
}

void D2SeccEngine::on_control_event(const d20::ControlEvent& event) {
    // An EVSE-initiated stop (module stop_charging command / driver shutdown) applies in every state,
    // not just the charge loop: latch it on the context so each subsequent status-carrying response
    // tells the EV to stop, and arm the guard against an EV that plainly ignores the request -- on its
    // expiry every further response is FAILED and the session ends (EvseV2G handle_stop_charging: a
    // 10 s graceful window, then stop_hlc fails everything). Context-level, not a per-state event.
    if (const auto* stop = std::get_if<d20::StopCharging>(&event)) {
        const bool requested = static_cast<bool>(*stop);
        if (requested and not stop_charging_guard_armed) {
            ctx.start_timeout(d20::TimeoutType::STOP_CHARGING, d20::TIMEOUT_STOP_CHARGING_GUARD);
            stop_charging_guard_armed = true;
        } else if (not requested and stop_charging_guard_armed) {
            ctx.stop_timeout(d20::TimeoutType::STOP_CHARGING);
            stop_charging_guard_armed = false;
        }
        ctx.charger_stop_requested = requested;
        if (not requested) {
            ctx.charger_stop_ignored = false;
        }
        return;
    }

    // The EVSE current limit changed (external energy limits): update the session config so the next
    // ChargingStatusRes announces the new EVSEMaxCurrent and the EV throttles accordingly. The
    // ChargeParameterDiscoveryRes offer stays at the hardware capability. Context-level, not a
    // per-state event.
    if (const auto* max_current = std::get_if<d20::UpdateAcMaxCurrent>(&event)) {
        ctx.session_config.ac_max_current = max_current->ampere;
        return;
    }

    // The EVSE DC limits changed (energy management pushes these for the whole session): update the
    // session config so the next CurrentDemandRes announces the new EVSEMaximum* and the EV throttles
    // accordingly. The ChargeParameterDiscoveryRes offer stays at the hardware capability.
    // Context-level, not a per-state event.
    if (const auto* limits = std::get_if<d20::DcTransferLimits>(&event)) {
        apply_dc_limits(ctx.session_config, *limits);
        return;
    }

    // The power-supply hardware capabilities changed (e.g. external derating): they feed the
    // ChargeParameterDiscoveryRes offer, which a [V2G2-813] renegotiation re-sends mid-session.
    if (const auto* caps = std::get_if<d20::UpdatePowersupplyLimits>(&event)) {
        apply_dc_capabilities(ctx.session_config, caps->limits);
        return;
    }

    // The hardware AC limits changed: re-derive the capability current the ChargeParameterDiscoveryRes
    // advertises. The live per-phase limit (ChargingStatusRes) is owned by UpdateAcMaxCurrent above.
    if (const auto* ac_limits = std::get_if<d20::AcTransferLimits>(&event)) {
        const auto ac_power = m20dt::from_RationalNumber(ac_limits->charge_power.max);
        if (ac_power > 0.0f and ctx.session_config.ac_nominal_voltage > 0.0f) {
            ctx.session_config.ac_capability_max_current =
                ac_power / (ctx.session_config.ac_nominal_voltage * ac_capability_phase_count(ctx.session_config));
        }
        return;
    }

    // Updated physical EVSE parameters (set_charging_parameters); they are read when the next
    // ChargeParameterDiscoveryRes is built. The AC max current is deliberately not re-derived here:
    // UpdateAcMaxCurrent carries the live per-phase limit and must keep precedence.
    if (const auto* values = std::get_if<d20::PhysicalValues>(&event)) {
        apply_physical_values(ctx.session_config, *values);
        return;
    }

    // The charger reports that no energy is available (IEC 61851-23:2023 CC.3.5.3).
    if (const auto* pause = std::get_if<d20::NoEnergyPause>(&event)) {
        ctx.session_config.no_energy_pause = pause->mode;
        return;
    }

    // A new meter reading (update_meter_info, pushed once per powermeter update): latch it on the context
    // so the charge loop reports MeterInfo from its very first response onwards. Context-level, not a
    // per-state event -- readings pushed before the charge loop starts (the module publishes throughout
    // the session) would otherwise be dropped, leaving the first ChargingStatusRes/CurrentDemandRes
    // without a reading.
    if (const auto* meter = std::get_if<d20::MeterInfo>(&event)) {
        m2dt::MeterInfo info{};
        info.meter_id = meter->meter_id;
        info.meter_reading = meter->meter_reading_wh;
        ctx.latest_meter_info = info;
        return;
    }

    // The module reported an isolation-monitoring result (update_isolation_status); the DC responses
    // after the cable check report it as EVSEIsolationStatus.
    if (const auto* isolation = std::get_if<d20::UpdateIsolationStatus>(&event)) {
        ctx.reported_isolation_status = isolation->status;
        return;
    }

    // ISO 15118-2 has no SECC-initiated pause: the SECC can only tell the EV to stop (EVSENotification
    // StopCharging via stop_charging). Say so instead of silently dropping the request (EvseV2G parity).
    if (const auto* pause = std::get_if<d20::PauseCharging>(&event); pause and static_cast<bool>(*pause)) {
        logf_warning("A charger-initiated pause is not supported in ISO 15118-2; use stop_charging instead");
        return;
    }

    // An EVSE error (module send_error / reset_error) is a persistent status override, not a per-state
    // event: store it on the context so the DC charge responses reflect it, and abort on emergency.
    if (const auto* err = std::get_if<d20::EvseError>(&event)) {
        ctx.active_error = err->code;
        if (err->code == d20::EvseErrorCode::EmergencyShutdown and not ctx.emergency_shutdown) {
            // [V2G2-539]/[V2G2-034]: the SECC answers FAILED and terminates the connection with it, instead of
            // dropping the TCP connection silently -- the EV would otherwise see a transport error and
            // never learn the reason. active_error above already puts EVSE_EmergencyShutdown into the DC
            // status of that response. The guard bounds the wait for the EV's next request; the physical
            // shutdown does not wait on any of this, it runs over the control pilot.
            logf_error("EVSE emergency shutdown reported; failing the next ISO 15118-2 response and terminating");
            ctx.emergency_shutdown = true;
            ctx.start_timeout(d20::TimeoutType::EMERGENCY_SHUTDOWN, d20::TIMEOUT_EMERGENCY_SHUTDOWN_GUARD);
        }
        return;
    }

    // Track the measured CP state on the context ([V2G2-920]..[V2G2-922] checks); still feed the
    // event to the FSM below so a state parked while waiting for CP State B resumes on it.
    if (const auto* cp = std::get_if<d20::CpStateChanged>(&event)) {
        ctx.current_cp_state = cp->state;
        // CP State A (unplug) ends the session, mirroring the DIN engine ([V2G-DC-962] analog): the
        // EV is gone, so close the TCP connection without the EV-first linger. Also applies while a
        // normal end is still in its close linger — a lingering DLINK_TERMINATE would otherwise fire
        // seconds later, into the SLAC matching of the next plug-in.
        if (cp->state == d20::CpState::A) {
            if (not ctx.session_stopped) {
                logf_info("CP State A detected, terminating the ISO 15118-2 session");
            }
            ctx.session_stopped = true;
            ctx.session_ended_with_error = true;
        }
    }

    active_control_event = event;
    [[maybe_unused]] const auto res = fsm.feed(d2::Event::CONTROL_MESSAGE);
    active_control_event.reset();
}

void D2SeccEngine::on_timeout(d20::TimeoutType timeout) {
    if (timeout == d20::TimeoutType::SEQUENCE) {
        logf_error("Sequence Timeout is reached. Stopping the session");
        ctx.session_stopped = true;
        return;
    }

    // The EV did not end the session within the grace period after the StopCharging request: enforce
    // the stop -- the next response (the charge loop delivers one within a second) is answered FAILED
    // and terminates the session (EvseV2G stop_hlc parity). An EV that sends nothing at all is bounded
    // by the sequence timeout above.
    // The EV sent nothing the emergency shutdown could be reported on: close anyway rather than hold
    // the connection until the sequence timeout.
    if (timeout == d20::TimeoutType::EMERGENCY_SHUTDOWN) {
        if (not ctx.session_stopped) {
            logf_warning("No request to answer within %%d ms of the emergency shutdown; closing the connection",
                         d20::TIMEOUT_EMERGENCY_SHUTDOWN_GUARD);
            ctx.session_stopped = true;
            ctx.session_ended_with_error = true;
        }
        return;
    }

    if (timeout == d20::TimeoutType::STOP_CHARGING) {
        stop_charging_guard_armed = false;
        if (not ctx.session_stopped and not ctx.session_paused) {
            logf_warning("The EV did not stop within %d ms after the StopCharging request; failing every "
                         "further response",
                         d20::TIMEOUT_STOP_CHARGING_GUARD);
            ctx.charger_stop_ignored = true;
        }
        return;
    }

    ctx.set_active_timeout(timeout);
    [[maybe_unused]] const auto res = fsm.feed(d2::Event::TIMEOUT);
}

bool D2SeccEngine::has_outgoing() const {
    return message_exchange.has_response();
}

std::optional<SeccOutgoing> D2SeccEngine::take_outgoing() {
    const auto [got_response, payload_size, payload_type, message_type] = message_exchange.check_and_clear_response();
    if (not got_response) {
        return std::nullopt;
    }
    // message_type is the concrete message_2::Type; report it so the module logs the real name.
    return SeccOutgoing{payload_size, payload_type, message_type};
}

bool D2SeccEngine::is_finished() const {
    return (ctx.session_stopped or ctx.session_paused) and not message_exchange.has_response();
}

bool D2SeccEngine::is_finished_with_error() const {
    return ctx.session_ended_with_error;
}

bool D2SeccEngine::is_paused() const {
    return ctx.session_paused;
}

std::optional<session::feedback::SessionStopAction> D2SeccEngine::pop_session_stop_res_pending() {
    return std::exchange(ctx.session_stop_res_pending, std::nullopt);
}

void D2SeccEngine::request_shutdown() {
    ctx.request_shutdown();
}

} // namespace iso15118
