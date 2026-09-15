// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <iso15118/ev/d20/engine.hpp>

#include <memory>

#include <iso15118/message/variant.hpp>

#include <iso15118/ev/d20/state/supported_app_protocol.hpp>
#include <iso15118/ev/d20/timeouts.hpp>
#include <iso15118/ev/detail/d20/context_helper.hpp>

namespace iso15118::ev::d20 {

Engine::Engine(feedback::Callbacks callbacks, message_20::datatypes::Identifier evcc_id,
               std::vector<message_20::SupportedAppProtocol> advertised_app_protocols,
               const std::optional<ControlEvent>& current_control_event,
               everest::lib::util::monitor<DcChargeParams>& dc_params,
               everest::lib::util::monitor<AcChargeParams>& ac_params,
               message_20::datatypes::ServiceCategory energy_service, DerControlFunctions der_control_functions,
               bool der_stop_on_unsupported_functions, SessionOptions options) :
    ctx(std::move(callbacks), message_exchange, std::move(evcc_id), std::move(advertised_app_protocols),
        current_control_event, dc_params, ac_params, energy_service, der_control_functions,
        der_stop_on_unsupported_functions, std::move(options)) {
}

void Engine::start() {
    fsm.emplace(ctx.create_state<state::SupportedAppProtocol>());
}

bool Engine::started() const {
    return fsm.has_value();
}

bool Engine::stage_response(io::v2gtp::PayloadType payload_type, const io::StreamInputView& view) {
    message_exchange.set_response(std::make_unique<message_20::Variant>(payload_type, view));
    return true;
}

V2gMessageType Engine::peek_response_type() const {
    return message_exchange.peek_response_type();
}

FeedOutcome Engine::feed(Event ev) {
    FeedOutcome outcome;
    if (not fsm.has_value()) {
        return outcome;
    }
    outcome.state_before = fsm->get_current_state_id();
    const auto fed = fsm->feed(ev);
    outcome.output = fed.output;
    outcome.transitioned = fed.transitioned();

    const auto negotiated = ctx.negotiated_protocol();
    const bool handover = negotiated.has_value() and negotiated.value() != ProtocolId::ISO15118_20;
    outcome.violation = disposition_violation(fed.output, ev == Event::V2GTP_MESSAGE, message_exchange.has_request(),
                                              ctx.is_session_stopped(), outcome.transitioned, handover);
    return outcome;
}

bool Engine::has_request() const {
    return message_exchange.has_request();
}

std::optional<std::pair<std::vector<uint8_t>, io::v2gtp::PayloadType>> Engine::take_request() {
    return message_exchange.take_request();
}

void Engine::discard_request() {
    if (message_exchange.has_request()) {
        message_exchange.take_request();
    }
}

std::chrono::milliseconds Engine::response_timeout() const {
    if (not fsm.has_value()) {
        return timeouts::MESSAGE;
    }
    return timeouts::response_timeout(fsm->get_current_state_id());
}

std::optional<std::chrono::milliseconds> Engine::ongoing_timeout() const {
    if (not fsm.has_value()) {
        return std::nullopt;
    }
    return timeouts::ongoing_timeout(fsm->get_current_state_id());
}

std::chrono::milliseconds Engine::min_request_interval() const {
    return std::chrono::milliseconds(0);
}

void Engine::latch(const ControlEvent& event) {
    if (const auto* stop = std::get_if<StopCharging>(&event); stop != nullptr and *stop) {
        ctx.set_stop_charging_requested(true);
    }
    if (const auto* pause = std::get_if<PauseCharging>(&event); pause != nullptr and *pause) {
        ctx.set_pause_charging_requested(true);
    }
    if (const auto* cp = std::get_if<CpState>(&event)) {
        ctx.set_cp_state(cp->c_or_d);
    }
}

void Engine::stop() {
    ctx.stop_session();
}

bool Engine::is_stopped() const {
    return ctx.is_session_stopped();
}

bool Engine::is_paused() const {
    return ctx.is_session_paused();
}

std::optional<std::array<uint8_t, 8>> Engine::session_id() const {
    return ctx.get_session().get_id();
}

std::optional<ProtocolId> Engine::negotiated_protocol() const {
    return ctx.negotiated_protocol();
}

std::optional<StateID> Engine::current_state() const {
    if (not fsm.has_value()) {
        return std::nullopt;
    }
    return fsm->get_current_state_id();
}

} // namespace iso15118::ev::d20
