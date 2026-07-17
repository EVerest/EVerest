// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/session/din_secc_engine.hpp>

#include <algorithm>
#include <memory>
#include <utility>

#include <iso15118/din/state/session_setup.hpp>

#include <iso15118/detail/helper.hpp>
#include <iso15118/message/common_types.hpp>
#include <iso15118/message_din/variant.hpp>

namespace iso15118 {

namespace {

// Maps the (d20) SECC session config into the DIN-specific SECC config. message_20 physical quantities
// are RationalNumbers; DIN uses plain doubles.
din::SessionConfig make_din_config(const session::SessionConfig& config) {
    namespace dt20 = message_20::datatypes;

    din::SessionConfig cfg;

    constexpr size_t EVSE_ID_MAX_BYTES = 32; // DIN EVSEID hexBinary MaxLength
    const auto evse_id_len = std::min(config.evse_id.size(), EVSE_ID_MAX_BYTES);
    cfg.evse_id.assign(config.evse_id.begin(), config.evse_id.begin() + evse_id_len);

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

void DinSeccEngine::request_shutdown() {
    ctx.request_shutdown();
}

} // namespace iso15118
