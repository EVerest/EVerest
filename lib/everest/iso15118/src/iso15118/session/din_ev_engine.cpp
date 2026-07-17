// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/session/din_ev_engine.hpp>

#include <memory>
#include <utility>

#include <iso15118/din/ev/state/session_setup.hpp>

#include <iso15118/message_din/variant.hpp>

namespace iso15118 {

DinEvEngine::DinEvEngine(io::StreamOutputView output_view, din::ev::EvSessionConfig config,
                         session::ev::feedback::Callbacks callbacks, session::SessionLogger& log,
                         d20::Timeouts& timeouts) :
    message_exchange(output_view),
    ctx(std::move(callbacks), log, std::move(config), active_control_event, message_exchange, timeouts),
    fsm(ctx.create_state<din::ev::state::SessionSetup>()) {
}

void DinEvEngine::kick_first_request() {
    [[maybe_unused]] const auto res = fsm.feed(din::ev::Event::SEND_REQUEST);
}

void DinEvEngine::on_packet(io::v2gtp::PayloadType, const io::StreamInputView& view) {
    // DIN dispatch is by protocol context; the payload type is not needed to decode.
    message_exchange.set_request(std::make_unique<message_din::Variant>(view));

    // Report the concrete incoming DIN SPEC 70121 response type so the EVCC module logs its real name.
    ctx.feedback.v2g_message(ctx.peek_response_type());

    const auto res = fsm.feed(din::ev::Event::V2GTP_MESSAGE);

    // The new state emits its request when fed SEND_REQUEST (enter() stays side-effect free).
    if (res.transitioned() and not is_finished()) {
        [[maybe_unused]] const auto send_res = fsm.feed(din::ev::Event::SEND_REQUEST);
    }
}

void DinEvEngine::on_control_event(const d20::ev::ControlEvent& event) {
    active_control_event = event;
    [[maybe_unused]] const auto res = fsm.feed(din::ev::Event::CONTROL_MESSAGE);
    active_control_event.reset();
}

void DinEvEngine::on_timeout(d20::TimeoutType timeout) {
    ctx.set_active_timeout(timeout);
    [[maybe_unused]] const auto res = fsm.feed(din::ev::Event::TIMEOUT);
}

bool DinEvEngine::has_outgoing() const {
    return message_exchange.has_response();
}

std::optional<EvEngine::Outgoing> DinEvEngine::take_outgoing() {
    const auto [got_request, payload_size, payload_type, message_type] = message_exchange.check_and_clear_response();
    if (not got_request) {
        return std::nullopt;
    }
    // message_type is the concrete message_din::Type; report it so the EVCC module logs the real name.
    return Outgoing{payload_size, payload_type, message_type};
}

bool DinEvEngine::is_finished() const {
    return (ctx.session_stopped or ctx.session_paused) and not message_exchange.has_response();
}

bool DinEvEngine::is_paused() const {
    return ctx.session_paused;
}

std::optional<std::array<uint8_t, 8>> DinEvEngine::session_id() const {
    const auto& id = ctx.get_session_id();
    if (id == message_din::datatypes::SessionId{}) {
        return std::nullopt;
    }
    return id;
}

} // namespace iso15118
