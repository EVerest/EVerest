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

// Maps the (d20) SECC session config into the DIN-specific SECC config. message_20 physical quantities
// are RationalNumbers; DIN uses plain doubles.
din::SessionConfig make_din_config(const session::SessionConfig& config) {
    namespace dt20 = message_20::datatypes;

    din::SessionConfig cfg;

    cfg.evse_id = din_evse_id_to_hex(config.evse_id);

    const auto& dc = config.dc_limits;
    cfg.evse_maximum_current_limit = dt20::from_RationalNumber(dc.charge_limits.current.max);
    cfg.evse_maximum_power_limit = dt20::from_RationalNumber(dc.charge_limits.power.max);
    cfg.evse_maximum_voltage_limit = dt20::from_RationalNumber(dc.voltage.max);
    cfg.evse_minimum_current_limit = dt20::from_RationalNumber(dc.charge_limits.current.min);
    cfg.evse_minimum_voltage_limit = dt20::from_RationalNumber(dc.voltage.min);
    cfg.evse_peak_current_ripple = 1.0;

    // Fall back to sane maxima when the config carries zero/unset DC limits (mirrors make_d2_config).
    if (not cfg.evse_maximum_power_limit.has_value() or cfg.evse_maximum_power_limit.value() <= 0.0) {
        cfg.evse_maximum_power_limit = 150000.0;
    }
    if (cfg.evse_maximum_current_limit <= 0.0) {
        cfg.evse_maximum_current_limit = 300.0;
    }
    if (cfg.evse_maximum_voltage_limit <= 0.0) {
        cfg.evse_maximum_voltage_limit = 900.0;
    }

    return cfg;
}

} // namespace

DinSeccEngine::DinSeccEngine(io::StreamOutputView output_view, const session::SessionConfig& config,
                             session::feedback::Callbacks callbacks, session::SessionLogger& log,
                             d20::Timeouts& timeouts) :
    message_exchange(output_view),
    ctx(std::move(callbacks), log, make_din_config(config), active_control_event, message_exchange, timeouts),
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
    // A module-reported EVSE error is a persistent status override: store it on the context so the DC
    // charge responses reflect it, and abort on emergency (mirrors the ISO 15118-2 engine).
    if (const auto* err = std::get_if<d20::EvseError>(&event)) {
        ctx.active_error = err->code;
        if (err->code == d20::EvseErrorCode::EmergencyShutdown) {
            logf_error("EVSE emergency shutdown reported; aborting the DIN SPEC 70121 session");
            ctx.session_stopped = true;
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

    ctx.set_active_timeout(timeout);
    [[maybe_unused]] const auto res = fsm.feed(din::Event::TIMEOUT);
}

bool DinSeccEngine::has_outgoing() const {
    return message_exchange.has_response();
}

std::optional<SeccEngine::Outgoing> DinSeccEngine::take_outgoing() {
    const auto [got_response, payload_size, payload_type, message_type] = message_exchange.check_and_clear_response();
    if (not got_response) {
        return std::nullopt;
    }
    // message_type is the concrete message_din::Type; report it so the module logs the real name.
    return Outgoing{payload_size, payload_type, message_type};
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
