// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/session/din_secc_engine.hpp>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <iso15118/din/state/session_setup.hpp>

#include <iso15118/detail/helper.hpp>
#include <iso15118/detail/session/din_secc_engine.hpp>
#include <iso15118/message/common_types.hpp>
#include <iso15118/message_din/variant.hpp>

namespace iso15118 {

namespace {

// [V2G-DC-620/621]: transform a DIN SPEC 91286 EVSE ID string into its hexBinary representation. The id
// alphabet is DIGIT and '*'; each character maps to one nibble ('0'-'9' -> 0x0-0x9, '*' -> 0xA) and two
// nibbles pack into one byte (an odd trailing nibble is padded with the unused value 0xF). An empty id,
// or one carrying a character outside the DIN SPEC 91286 alphabet (e.g. an ISO-15118-style id with
// letters), is sent as the single zero byte 0x00 [V2G-DC-876], which the EVCC then ignores [V2G-DC-877].
std::vector<uint8_t> din_evse_id_to_hex(const std::string& evse_id) {
    constexpr size_t MAX_CHARS = 32; // DIN SPEC 91286: up to 32 characters -> up to 16 hexBinary bytes

    std::vector<uint8_t> nibbles;
    for (const char c : evse_id) {
        if (nibbles.size() >= MAX_CHARS) {
            break;
        }
        if (c >= '0' and c <= '9') {
            nibbles.push_back(static_cast<uint8_t>(c - '0'));
        } else if (c == '*') {
            nibbles.push_back(0xA);
        } else {
            logf_warning("DIN EVSEID '%s' is not a valid DIN SPEC 91286 id; sending 0x00", evse_id.c_str());
            return {0x00};
        }
    }

    if (nibbles.empty()) {
        return {0x00};
    }

    std::vector<uint8_t> out;
    out.reserve((nibbles.size() + 1) / 2);
    for (size_t i = 0; i < nibbles.size(); i += 2) {
        const uint8_t hi = nibbles[i];
        const uint8_t lo = (i + 1 < nibbles.size()) ? nibbles[i + 1] : 0x0F;
        out.push_back(static_cast<uint8_t>((hi << 4) | lo));
    }
    return out;
}

// True when the limit set carries at least one positive maximum, i.e. the module actually reported it.
bool has_dc_maxima(const d20::DcTransferLimits& dc) {
    namespace dt20 = message_20::datatypes;
    return dt20::from_RationalNumber(dc.charge_limits.power.max) > 0.0f or
           dt20::from_RationalNumber(dc.charge_limits.current.max) > 0.0f or
           dt20::from_RationalNumber(dc.voltage.max) > 0.0f;
}

} // namespace

// Maps the (d20) SECC session config into the DIN-specific SECC config. message_20 physical quantities
// are RationalNumbers; DIN uses plain doubles.
din::SessionConfig make_din_config(const session::SessionConfig& config) {
    din::SessionConfig cfg;

    cfg.evse_id = din_evse_id_to_hex(config.evse_id);
    cfg.evse_peak_current_ripple = 1.0;

    // The DIN ChargeService advertises exactly one energy transfer mode. Pick it from the modes the
    // module configured (update_energy_transfer_modes): DC_extended when offered, else DC_core -- the
    // two modes this DC-only SECC serves. With neither configured (or nothing configured at all) the
    // DC_extended default stands, so an EV asking for DC_core against e.g. a DC_combo_core-only config
    // is still rejected per [V2G-DC-397].
    for (const auto mode : config.pre20_energy_transfer_modes) {
        if (mode == shared_datatypes::EnergyTransferMode::DC_extended) {
            cfg.energy_transfer_mode = message_din::datatypes::SupportedEnergyTransferMode::DC_extended;
            break;
        }
        if (mode == shared_datatypes::EnergyTransferMode::DC_core) {
            cfg.energy_transfer_mode = message_din::datatypes::SupportedEnergyTransferMode::DC_core;
            // Keep looking: DC_extended wins if it appears later in the list.
        }
    }

    apply_dc_limits(cfg, config.dc_limits);
    // The ChargeParameterDiscoveryRes offer is the maximum the EVSE could ever deliver: the power-supply
    // hardware capabilities (set_powersupply_capabilities), not the live energy-management limits above.
    // A module that never reported capabilities (e.g. EvseManager's fake-DC/AC-with-SoC mode only seeds
    // update_dc_maximum_limits) falls back to those limits -- still reported data, never an invented
    // value. With neither reported the offer is 0 (safety).
    apply_dc_capabilities(cfg, has_dc_maxima(config.powersupply_limits) ? config.powersupply_limits : config.dc_limits);
    if (not cfg.evse_capability_maximum_power_limit.has_value() or
        cfg.evse_capability_maximum_power_limit.value() <= 0.0) {
        logf_warning("No DC power-supply capabilities or limits were reported; the "
                     "ChargeParameterDiscoveryRes will offer 0 W");
    }
    apply_physical_values(cfg, config.physical_values);
    cfg.no_energy_pause = config.no_energy_pause;
    // DIN SPEC 70121 authorization is EIM only, so only that timeout applies.
    cfg.auth_timeout_eim_ms = session::auth_timeout_to_ms(config.auth_timeout_eim_s);

    return cfg;
}

void apply_dc_limits(din::SessionConfig& cfg, const d20::DcTransferLimits& dc) {
    namespace dt20 = message_20::datatypes;

    // Safety: no invented values. A negative (invalid) limit clamps to 0 and an unreported one stays 0
    // -- only actually reported data ever tells the EV it may draw energy.
    const auto non_negative = [](float value) { return static_cast<double>(std::max(0.0f, value)); };
    cfg.evse_maximum_current_limit = non_negative(dt20::from_RationalNumber(dc.charge_limits.current.max));
    cfg.evse_maximum_power_limit = non_negative(dt20::from_RationalNumber(dc.charge_limits.power.max));
    cfg.evse_maximum_voltage_limit = non_negative(dt20::from_RationalNumber(dc.voltage.max));
}

void apply_dc_capabilities(din::SessionConfig& cfg, const d20::DcTransferLimits& dc) {
    namespace dt20 = message_20::datatypes;

    // Safety: the advertised capability must never be invented. A negative (invalid) value clamps to 0
    // and an unreported one stays 0 -- only actually reported data ever advertises a positive offer, so
    // no default here (unlike the charge-loop values in apply_dc_limits).
    const auto non_negative = [](float value) { return static_cast<double>(std::max(0.0f, value)); };
    cfg.evse_capability_maximum_current_limit = non_negative(dt20::from_RationalNumber(dc.charge_limits.current.max));
    cfg.evse_capability_maximum_power_limit = non_negative(dt20::from_RationalNumber(dc.charge_limits.power.max));
    cfg.evse_capability_maximum_voltage_limit = non_negative(dt20::from_RationalNumber(dc.voltage.max));
    cfg.evse_minimum_current_limit = non_negative(dt20::from_RationalNumber(dc.charge_limits.current.min));
    cfg.evse_minimum_voltage_limit = non_negative(dt20::from_RationalNumber(dc.voltage.min));
}

void apply_physical_values(din::SessionConfig& cfg, const d20::PhysicalValues& values) {
    if (values.dc_peak_current_ripple.has_value()) {
        cfg.evse_peak_current_ripple = values.dc_peak_current_ripple.value();
    }
    if (values.dc_current_regulation_tolerance.has_value()) {
        cfg.evse_current_regulation_tolerance = values.dc_current_regulation_tolerance.value();
    }
    if (values.dc_energy_to_be_delivered.has_value()) {
        cfg.evse_energy_to_be_delivered = values.dc_energy_to_be_delivered.value();
    }
}

DinSeccEngine::DinSeccEngine(io::StreamOutputView output_view, const session::SessionConfig& config,
                             session::feedback::Callbacks callbacks, d20::Timeouts& timeouts) :
    message_exchange(output_view),
    ctx(std::move(callbacks), make_din_config(config), active_control_event, message_exchange, timeouts),
    fsm(ctx.create_state<din::state::SessionSetup>()) {
}

void DinSeccEngine::on_packet(io::v2gtp::PayloadType, const io::StreamInputView& view) {
    // DIN dispatch is by protocol context; the payload type is not needed to decode.
    message_exchange.set_request(std::make_unique<message_din::Variant>(view));

    // Report the concrete incoming DIN SPEC 70121 request type so the module logs its real name.
    ctx.feedback.v2g_message(ctx.peek_request_type());

    drive_request(fsm, message_exchange, din::Event::V2GTP_MESSAGE);
}

void DinSeccEngine::on_control_event(const d20::ControlEvent& event) {
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

    // The EVSE DC limits changed (energy management pushes these for the whole session): update the
    // session config so the next CurrentDemandRes announces the new EVSEMaximum* and the EV throttles
    // accordingly. The ChargeParameterDiscoveryRes offer stays at the hardware capability.
    // Context-level, not a per-state event.
    if (const auto* limits = std::get_if<d20::DcTransferLimits>(&event)) {
        apply_dc_limits(ctx.session_config, *limits);
        return;
    }

    // The power-supply hardware capabilities changed (e.g. external derating): they feed the
    // ChargeParameterDiscoveryRes offer, so apply them in case that response is still to be sent.
    if (const auto* caps = std::get_if<d20::UpdatePowersupplyLimits>(&event)) {
        apply_dc_capabilities(ctx.session_config, caps->limits);
        return;
    }

    // Updated physical EVSE parameters (set_charging_parameters); they are read when the next
    // ChargeParameterDiscoveryRes is built.
    if (const auto* values = std::get_if<d20::PhysicalValues>(&event)) {
        apply_physical_values(ctx.session_config, *values);
        return;
    }

    // The charger reports that no energy is available (IEC 61851-23:2023 CC.3.5.3).
    if (const auto* pause = std::get_if<d20::NoEnergyPause>(&event)) {
        ctx.session_config.no_energy_pause = pause->mode;
        return;
    }

    // The module reported an isolation-monitoring result (update_isolation_status); the DC responses
    // after the cable check report it as EVSEIsolationStatus.
    if (const auto* isolation = std::get_if<d20::UpdateIsolationStatus>(&event)) {
        ctx.reported_isolation_status = isolation->status;
        return;
    }

    // DIN SPEC 70121 has no SECC-initiated pause: the SECC can only tell the EV to stop (EVSENotification
    // StopCharging via stop_charging). Say so instead of silently dropping the request (EvseV2G parity).
    if (const auto* pause = std::get_if<d20::PauseCharging>(&event); pause and static_cast<bool>(*pause)) {
        logf_warning("A charger-initiated pause is not supported in DIN SPEC 70121; use stop_charging instead");
        return;
    }

    // A module-reported EVSE error is a persistent status override: store it on the context so the DC
    // charge responses reflect it, and abort on emergency (mirrors the ISO 15118-2 engine).
    if (const auto* err = std::get_if<d20::EvseError>(&event)) {
        ctx.active_error = err->code;
        if (err->code == d20::EvseErrorCode::EmergencyShutdown and not ctx.emergency_shutdown) {
            // [V2G-DC-866]: the SECC answers FAILED and terminates the connection with it, instead of
            // dropping the TCP connection silently -- the EV would otherwise see a transport error and
            // never learn the reason. active_error above already puts EVSE_EmergencyShutdown into the DC
            // status of that response. The guard bounds the wait for the EV's next request; the physical
            // shutdown does not wait on any of this, it runs over the control pilot.
            logf_error("EVSE emergency shutdown reported; failing the next DIN SPEC 70121 response and terminating");
            ctx.emergency_shutdown = true;
            ctx.start_timeout(d20::TimeoutType::EMERGENCY_SHUTDOWN, d20::TIMEOUT_EMERGENCY_SHUTDOWN_GUARD);
        }
        return;
    }

    // Track the measured CP state on the context ([V2G-DC-988] checks) and still feed the event to
    // the FSM below: a state deferring its response while waiting for CP State B resumes on it.
    if (const auto* cp = std::get_if<d20::CpStateChanged>(&event)) {
        ctx.current_cp_state = cp->state;
        // [V2G-DC-962] CP State A (unplug) ends the session: switch off the oscillator (done by the
        // charger on the unplug event) and close the TCP connection without the EV-first linger.
        // Also applies while a normal end is still in its EV-first close linger -- the EV is gone,
        // so finish now (a lingering DLINK_TERMINATE would otherwise fire seconds later, into the
        // SLAC matching of the next plug-in).
        if (cp->state == d20::CpState::A) {
            if (not ctx.session_stopped) {
                logf_info("CP State A detected, terminating the DIN session [V2G-DC-962]");
            }
            ctx.session_stopped = true;
            ctx.session_ended_with_error = true;
        } else if (cp->state == d20::CpState::B and ctx.expect_cp_state_cd and not ctx.power_delivery_stopped and
                   not ctx.session_stopped) {
            // [V2G-DC-668] An unexpected CP State B during the DC charging phase (from CableCheck
            // until PowerDelivery(Stop), where the EV must be in C/D) is a fault: the SECC carries
            // out an EVSE-initiated emergency shutdown and terminates the session without delay.
            logf_info("Unexpected CP State B during the DC charging phase, terminating the DIN session [V2G-DC-668]");
            ctx.session_stopped = true;
            ctx.session_ended_with_error = true;
        }
    }

    active_control_event = event;
    [[maybe_unused]] const auto res = fsm.feed(din::Event::CONTROL_MESSAGE);
    active_control_event.reset();
}

void DinSeccEngine::on_timeout(d20::TimeoutType timeout) {
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
    [[maybe_unused]] const auto res = fsm.feed(din::Event::TIMEOUT);
}

bool DinSeccEngine::has_outgoing() const {
    return message_exchange.has_response();
}

std::optional<SeccOutgoing> DinSeccEngine::take_outgoing() {
    const auto [got_response, payload_size, payload_type, message_type] = message_exchange.check_and_clear_response();
    if (not got_response) {
        return std::nullopt;
    }
    // message_type is the concrete message_din::Type; report it so the module logs the real name.
    return SeccOutgoing{payload_size, payload_type, message_type};
}

bool DinSeccEngine::is_finished() const {
    return (ctx.session_stopped or ctx.session_paused) and not message_exchange.has_response();
}

bool DinSeccEngine::is_paused() const {
    return ctx.session_paused;
}

bool DinSeccEngine::is_finished_with_error() const {
    return ctx.session_ended_with_error;
}

std::optional<session::feedback::SessionStopAction> DinSeccEngine::pop_session_stop_res_pending() {
    return std::exchange(ctx.session_stop_res_pending, std::nullopt);
}

void DinSeccEngine::request_shutdown() {
    ctx.request_shutdown();
}

} // namespace iso15118
