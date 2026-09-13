// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <optional>

#include <everest/util/fsm/fsm.hpp>

#include <iso15118/d2/context.hpp>
#include <iso15118/d2/state/session_setup.hpp>
#include <iso15118/d2/states.hpp>
#include <iso15118/d20/control_event.hpp>
#include <iso15118/d20/timeout.hpp>
#include <iso15118/din/context.hpp>
#include <iso15118/din/state/session_setup.hpp>
#include <iso15118/din/states.hpp>
#include <iso15118/io/logging.hpp>
#include <iso15118/io/stream_view.hpp>
#include <iso15118/message_2/variant.hpp>
#include <iso15118/message_din/variant.hpp>
#include <iso15118/session/feedback.hpp>
#include <iso15118/session/secc_engine.hpp>

namespace iso15118::test {

namespace detail {
// Called from a member initializer that runs before the context is built, so even the initial
// state's enter() stays quiet.
inline bool silence_logging() {
    io::set_logging_callback([](LogLevel, std::string) {});
    return true;
}
} // namespace detail

class DinSeccFsm {
public:
    explicit DinSeccFsm(din::SessionConfig config, session::feedback::Callbacks callbacks = {}) :
        ctx(std::move(callbacks), std::move(config), active_control_event, msg_exch, timeouts),
        fsm(ctx.create_state<din::state::SessionSetup>()) {
    }

    din::Context& context() {
        return ctx;
    }

    din::StateID state() const {
        return fsm.get_current_state_id();
    }

    // Fed the way the engine does, through the real EXI codec: a Variant built directly from a message
    // struct carries no decoded header, so the states' SessionID checks would see none.
    template <typename Request> void drive(const Request& request) {
        clear_response();
        const io::StreamOutputView view{request_buffer.data(), request_buffer.size()};
        const auto len = message_din::serialize(request, view);
        msg_exch.set_request(std::make_unique<message_din::Variant>(io::StreamInputView{request_buffer.data(), len}));
        fsm.feed(din::Event::V2GTP_MESSAGE);
    }

    void control(const d20::ControlEvent& event) {
        active_control_event = event;
        fsm.feed(din::Event::CONTROL_MESSAGE);
        active_control_event.reset();
    }

    void timeout(d20::TimeoutType type) {
        ctx.set_active_timeout(type);
        fsm.feed(din::Event::TIMEOUT);
    }

    template <typename Response> std::optional<Response> response() {
        return msg_exch.get_response<Response>();
    }

    bool has_response() const {
        return msg_exch.has_response();
    }

    void clear_response() {
        msg_exch.check_and_clear_response();
    }

private:
    const bool logging_silenced{detail::silence_logging()};
    std::array<uint8_t, 4096> buffer{};
    std::array<uint8_t, 4096> request_buffer{};
    din::MessageExchange msg_exch{io::StreamOutputView{buffer.data(), buffer.size()}};
    std::optional<d20::ControlEvent> active_control_event{std::nullopt};
    d20::Timeouts timeouts;
    din::Context ctx;
    fsm::v2::FSM<din::StateBase> fsm;
};

class D2SeccFsm {
public:
    explicit D2SeccFsm(d2::SessionConfig config, session::feedback::Callbacks callbacks = {}) :
        ctx(std::move(callbacks), std::move(config), pause_ctx, active_control_event, msg_exch, timeouts),
        fsm(ctx.create_state<d2::state::SessionSetup>()) {
    }

    d2::Context& context() {
        return ctx;
    }

    d2::StateID state() const {
        return fsm.get_current_state_id();
    }

    // See DinSeccFsm::drive(): serialized and decoded back so the states see the wire's SessionID.
    template <typename Request> void drive(const Request& request) {
        clear_response();
        const io::StreamOutputView view{request_buffer.data(), request_buffer.size()};
        const auto len = message_2::serialize(request, view);
        msg_exch.set_request(std::make_unique<message_2::Variant>(io::StreamInputView{request_buffer.data(), len}));
        fsm.feed(d2::Event::V2GTP_MESSAGE);
    }

    void control(const d20::ControlEvent& event) {
        active_control_event = event;
        fsm.feed(d2::Event::CONTROL_MESSAGE);
        active_control_event.reset();
    }

    void timeout(d20::TimeoutType type) {
        ctx.set_active_timeout(type);
        fsm.feed(d2::Event::TIMEOUT);
    }

    template <typename Response> std::optional<Response> response() {
        return msg_exch.get_response<Response>();
    }

    bool has_response() const {
        return msg_exch.has_response();
    }

    void clear_response() {
        msg_exch.check_and_clear_response();
    }

private:
    const bool logging_silenced{detail::silence_logging()};
    std::array<uint8_t, 4096> buffer{};
    std::array<uint8_t, 4096> request_buffer{};
    d2::MessageExchange msg_exch{io::StreamOutputView{buffer.data(), buffer.size()}};
    std::optional<d20::ControlEvent> active_control_event{std::nullopt};
    std::optional<d2::PauseContext> pause_ctx{std::nullopt};
    d20::Timeouts timeouts;
    d2::Context ctx;
    fsm::v2::FSM<d2::StateBase> fsm;
};

} // namespace iso15118::test
