// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/session/d2_ev_engine.hpp>

#include <memory>
#include <utility>

#include <iso15118/d2/ev/state/session_setup.hpp>

#include <iso15118/message_2/variant.hpp>

namespace iso15118 {

D2EvEngine::D2EvEngine(io::StreamOutputView output_view, d2::ev::EvSessionConfig config,
                       session::ev::feedback::Callbacks callbacks, session::SessionLogger& log,
                       d20::Timeouts& timeouts) :
    message_exchange(output_view),
    ctx(std::move(callbacks), log, std::move(config), active_control_event, message_exchange, timeouts),
    fsm(ctx.create_state<d2::ev::state::SessionSetup>()) {
}

void D2EvEngine::kick_first_request() {
    [[maybe_unused]] const auto res = fsm.feed(d2::ev::Event::SEND_REQUEST);
}

void D2EvEngine::on_packet(io::v2gtp::PayloadType, const io::StreamInputView& view) {
    // All ISO 15118-2 messages share the single SAP payload type; disambiguation happens at decode.
    message_exchange.set_request(std::make_unique<message_2::Variant>(view));

    // Report the concrete incoming ISO 15118-2 response type so the EVCC module logs its real name.
    ctx.feedback.v2g_message(ctx.peek_response_type());

    const auto res = fsm.feed(d2::ev::Event::V2GTP_MESSAGE);

    // The new state emits its request when fed SEND_REQUEST (enter() stays side-effect free).
    if (res.transitioned() and not is_finished()) {
        [[maybe_unused]] const auto send_res = fsm.feed(d2::ev::Event::SEND_REQUEST);
    }
}

void D2EvEngine::on_control_event(const d20::ev::ControlEvent& event) {
    active_control_event = event;
    [[maybe_unused]] const auto res = fsm.feed(d2::ev::Event::CONTROL_MESSAGE);
    active_control_event.reset();
}

void D2EvEngine::on_timeout(d20::TimeoutType timeout) {
    ctx.set_active_timeout(timeout);
    [[maybe_unused]] const auto res = fsm.feed(d2::ev::Event::TIMEOUT);
}

bool D2EvEngine::has_outgoing() const {
    return message_exchange.has_response();
}

std::optional<EvEngine::Outgoing> D2EvEngine::take_outgoing() {
    const auto [got_request, payload_size, payload_type, message_type] = message_exchange.check_and_clear_response();
    if (not got_request) {
        return std::nullopt;
    }
    // message_type is the concrete message_2::Type; report it so the EVCC module logs the real name.
    return Outgoing{payload_size, payload_type, message_type};
}

bool D2EvEngine::is_finished() const {
    return (ctx.session_stopped or ctx.session_paused) and not message_exchange.has_response();
}

bool D2EvEngine::is_paused() const {
    return ctx.session_paused;
}

std::optional<std::array<uint8_t, 8>> D2EvEngine::session_id() const {
    const auto& id = ctx.get_session_id();
    if (id == message_2::datatypes::SessionId{}) {
        return std::nullopt;
    }
    return id;
}

} // namespace iso15118
