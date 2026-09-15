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
bool has_dc_maxima(const d20::DcTransferLimits& dc) {
    return m20dt::from_RationalNumber(dc.charge_limits.power.max) > 0.0f or
           m20dt::from_RationalNumber(dc.charge_limits.current.max) > 0.0f or
           m20dt::from_RationalNumber(dc.voltage.max) > 0.0f;
}

// update_ac_maximum_limits reports the AC charge power SUMMED OVER ALL PHASES, while both consumers
// of the derived capability current need it PER PHASE. Dividing by the phase count here is a
// deliberate deviation from EvseV2G, which divides by the nominal voltage alone and so over-reports
// both values by the phase count on a three-phase charger.
uint8_t ac_capability_phase_count(const d2::SessionConfig& out) {
    // A two-phase charger is advertised as single-phase by EvseManager's mode mapping.
    return everest::lib::util::exists(out.supported_energy_transfer_modes,
                                      m2dt::EnergyTransferMode::AC_three_phase_core)
               ? 3
               : 1;
}
} // namespace

d2::SessionConfig make_d2_config(const session::SessionConfig& config, bool tls_active) {
    d2::SessionConfig out;

    // evseIDType has a minimum length of 7, so a shorter or empty configured id would encode a
    // schema-invalid SessionSetupRes.
    static constexpr size_t ISO2_EVSE_ID_MIN_LEN = 7;
    if (config.evse_id.size() >= ISO2_EVSE_ID_MIN_LEN) {
        out.evse_id = config.evse_id;
    } else {
        logf_warning("Configured EVSEID '%s' is shorter than the ISO 15118-2 minimum of %zu characters; "
                     "using the default '%s'",
                     config.evse_id.c_str(), ISO2_EVSE_ID_MIN_LEN, out.evse_id.c_str());
    }
    out.tls_active = tls_active;

    // The -20 service categories are a lossy fallback for a module that never configured pre-20 modes:
    // they cannot distinguish DC_core from DC_extended, or carry DC_combo_core / DC_unique at all.
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
    // The offer is the maximum the EVSE could ever deliver, not the live energy-management limits. A
    // module that never reported capabilities falls back to those limits -- still reported data, never
    // an invented value; with neither reported the offer is 0.
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

    // AC nominal voltage is not in the d20 limits; it comes from set_charging_parameters and otherwise
    // stays at the 230 V default. Where the module reported no capability the live limit -- already a
    // per-phase current -- doubles as one, and with neither reported both stay 0.
    const auto ac_power = m20dt::from_RationalNumber(config.ac_limits.charge_power.max);
    if (ac_power > 0.0f and out.ac_nominal_voltage > 0.0f) {
        out.ac_capability_max_current = ac_power / (out.ac_nominal_voltage * ac_capability_phase_count(out));
    } else if (config.iso2_ac_max_current.has_value()) {
        out.ac_capability_max_current = config.iso2_ac_max_current.value();
    }
    // update_ac_max_current governs only the charge loop (ChargingStatusRes); mid-session changes
    // arrive as UpdateAcMaxCurrent control events.
    out.ac_max_current = config.iso2_ac_max_current.value_or(out.ac_capability_max_current);

    out.pnc_enabled = config.iso2_pnc_enabled;
    // A Contract-only SECC is permitted (EvseV2G parity), so ExternalPayment is offered only when configured.
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

// ServiceID 1 is the charging service and 2 the library's own Certificate service, so both are
// refused here. The ServiceList carries eight entries and Certificate takes one whenever it can be
// offered, so at most seven external services fit.
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
    const auto non_negative = [](float value) { return std::max(0.0f, value); };
    out.dc_max_power = non_negative(m20dt::from_RationalNumber(dc.charge_limits.power.max));
    out.dc_max_current = non_negative(m20dt::from_RationalNumber(dc.charge_limits.current.max));
    out.dc_max_voltage = non_negative(m20dt::from_RationalNumber(dc.voltage.max));
}

void apply_dc_capabilities(d2::SessionConfig& out, const d20::DcTransferLimits& dc) {
    // No default here, unlike the charge-loop values in apply_dc_limits: only reported data ever
    // advertises a positive offer.
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
    // All ISO 15118-2 messages share the single SAP payload type (0x8001); any other V2GTP payload
    // type is ignored (EvseV2G parity, libiso15118 finding F-001).
    if (payload_type != io::v2gtp::PayloadType::SAP) {
        return;
    }
    message_exchange.set_request(std::make_unique<message_2::Variant>(view));

    // The request type is reported by StateBase::feed(), which consumes it.
    fsm.feed(d2::Event::V2GTP_MESSAGE);
}

void D2SeccEngine::on_control_event(const d20::ControlEvent& event) {
    // An EVSE-initiated stop is latched on the context, not delivered per state, so every later
    // status-carrying response tells the EV to stop; the guard fails the session if the EV ignores it.
    if (const auto* stop = std::get_if<d20::StopCharging>(&event)) {
        const bool requested = static_cast<bool>(*stop);
        if (requested and not stop_charging_guard_armed) {
            ctx.start_timeout(d20::TimeoutType::STOP_CHARGING, d20::TIMEOUT_STOP_CHARGING_GUARD);
            stop_charging_guard_armed = true;
        } else if (not requested and stop_charging_guard_armed) {
            ctx.stop_timeout(d20::TimeoutType::STOP_CHARGING);
            stop_charging_guard_armed = false;
        }
        ctx.set_charger_stop_requested(requested);
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

    // These feed the ChargeParameterDiscoveryRes offer, which a [V2G2-813] renegotiation re-sends.
    if (const auto* caps = std::get_if<d20::UpdatePowersupplyLimits>(&event)) {
        apply_dc_capabilities(ctx.session_config, caps->limits);
        return;
    }

    // Re-derives the advertised capability current; the live per-phase limit is owned by UpdateAcMaxCurrent.
    if (const auto* ac_limits = std::get_if<d20::AcTransferLimits>(&event)) {
        const auto ac_power = m20dt::from_RationalNumber(ac_limits->charge_power.max);
        if (ac_power > 0.0f and ctx.session_config.ac_nominal_voltage > 0.0f) {
            ctx.session_config.ac_capability_max_current =
                ac_power / (ctx.session_config.ac_nominal_voltage * ac_capability_phase_count(ctx.session_config));
        }
        return;
    }

    // The AC max current is deliberately not re-derived here: UpdateAcMaxCurrent carries the live
    // per-phase limit and must keep precedence.
    if (const auto* values = std::get_if<d20::PhysicalValues>(&event)) {
        apply_physical_values(ctx.session_config, *values);
        return;
    }

    if (const auto* pause = std::get_if<d20::NoEnergyPause>(&event)) {
        ctx.session_config.no_energy_pause = pause->mode;
        return;
    }

    // Latched on the context rather than delivered per state: the module publishes readings throughout
    // the session, so one arriving before the charge loop starts would otherwise be dropped and the
    // first ChargingStatusRes/CurrentDemandRes would carry no reading.
    if (const auto* meter = std::get_if<d20::MeterInfo>(&event)) {
        m2dt::MeterInfo info{};
        info.meter_id = meter->meter_id;
        info.meter_reading = meter->meter_reading_wh;
        ctx.set_meter_info(info);
        return;
    }

    if (const auto* isolation = std::get_if<d20::UpdateIsolationStatus>(&event)) {
        ctx.set_isolation_status(isolation->status);
        return;
    }

    // ISO 15118-2 has no SECC-initiated pause -- the SECC can only ask the EV to stop. Say so rather
    // than drop the request silently (EvseV2G parity).
    if (const auto* pause = std::get_if<d20::PauseCharging>(&event); pause and static_cast<bool>(*pause)) {
        logf_warning("A charger-initiated pause is not supported in ISO 15118-2; use stop_charging instead");
        return;
    }

    // A persistent status override, not a per-state event, so it lives on the context.
    if (const auto* err = std::get_if<d20::EvseError>(&event)) {
        ctx.set_active_error(err->code);
        if (err->code == d20::EvseErrorCode::EmergencyShutdown and not ctx.evse().emergency_shutdown) {
            // [V2G2-539]/[V2G2-034]: answer FAILED and terminate with it rather than dropping the TCP
            // connection, which would leave the EV with a transport error and no reason. The physical shutdown
            // does not wait on any of this -- it runs over the control pilot.
            logf_error("EVSE emergency shutdown reported; failing the next ISO 15118-2 response and terminating");
            ctx.set_emergency_shutdown();
            ctx.start_timeout(d20::TimeoutType::EMERGENCY_SHUTDOWN, d20::TIMEOUT_EMERGENCY_SHUTDOWN_GUARD);
        }
        return;
    }

    // Still feed the event to the FSM below, so a state parked waiting for CP State B resumes on it.
    if (const auto* cp = std::get_if<d20::CpStateChanged>(&event)) {
        ctx.set_cp_state(cp->state);
        // CP State A (unplug) ends the session ([V2G-DC-962] analog): close without the EV-first linger.
        // This also applies during a normal end's close linger -- a lingering DLINK_TERMINATE would
        // otherwise fire seconds later, into the SLAC matching of the next plug-in.
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

    // The EV ignored the StopCharging request: the next response is answered FAILED and ends the
    // session (EvseV2G stop_hlc parity). An EV that sends nothing is bounded by the sequence timeout.
    // Nothing arrived that the emergency shutdown could be reported on, so close anyway.
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
    // Nothing to latch: request_shutdown() also pushes StopCharging{true}, which both charge loops
    // already act on. The -20 and DIN contexts keep a separate shutdown_requested() flag because they
    // use it to refuse closing the contactor on a PowerDeliveryReq(Start) during shutdown; ISO 15118-2
    // treats a charger-initiated stop as a request with a grace window instead. Kept because
    // SeccEngine requires it.
}

} // namespace iso15118
