// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <iso15118/ev/d2/engine.hpp>

#include <memory>

#include <iso15118/detail/helper.hpp>
#include <iso15118/ev/d2/state/session_setup.hpp>
#include <iso15118/ev/d2/timeouts.hpp>

namespace iso15118::ev::d2 {

Engine::Engine(feedback::Callbacks callbacks, EvSessionParams params,
               const std::optional<ControlEvent>& current_control_event,
               everest::lib::util::monitor<DcChargeParams>& dc_params, bool has_cp_state_feedback,
               std::optional<std::array<uint8_t, 8>> resumed_session_id) :
    ctx(std::move(callbacks), message_exchange, std::move(params), current_control_event, dc_params,
        has_cp_state_feedback, resumed_session_id) {
}

void Engine::start() {
    fsm.emplace(ctx.create_state<state::SessionSetup>());
}

bool Engine::started() const {
    return fsm.has_value();
}

bool Engine::stage_response(io::v2gtp::PayloadType payload_type, const io::StreamInputView& view) {
    if (payload_type != io::v2gtp::PayloadType::SAP) {
        logf_warning("Ignoring V2GTP payload type 0x%04x on an ISO 15118-2 session",
                     static_cast<unsigned>(payload_type));
        return false;
    }
    message_exchange.set_response(std::make_unique<message_2::Variant>(view));
    return true;
}

V2gMessageType Engine::peek_response_type() const {
    return message_exchange.peek_response_type();
}

FeedOutcome<StateID> Engine::feed(Event ev) {
    FeedOutcome<StateID> outcome;
    if (not fsm.has_value()) {
        return outcome;
    }
    outcome.state_before = fsm->get_current_state_id();
    const auto fed = fsm->feed(ev);
    outcome.output = fed.output;
    outcome.transitioned = fed.transitioned();
    outcome.violation = disposition_violation(fed.output, ev == Event::V2GTP_MESSAGE, message_exchange.has_request(),
                                              ctx.is_session_stopped(), outcome.transitioned, false);
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
    return fsm.has_value() ? timeouts::response_timeout(fsm->get_current_state_id()) : timeouts::MESSAGE;
}

std::optional<std::chrono::milliseconds> Engine::ongoing_timeout() const {
    if (not fsm.has_value()) {
        return std::nullopt;
    }
    return timeouts::ongoing_timeout(fsm->get_current_state_id());
}

std::chrono::milliseconds Engine::min_request_interval() const {
    return timeouts::MIN_REQUEST_INTERVAL;
}

std::optional<StateID> Engine::current_state() const {
    if (not fsm.has_value()) {
        return std::nullopt;
    }
    return fsm->get_current_state_id();
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
    return ctx.get_session_id();
}

std::optional<ProtocolId> Engine::negotiated_protocol() const {
    return ProtocolId::ISO15118_2;
}

} // namespace iso15118::ev::d2
