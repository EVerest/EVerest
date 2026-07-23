// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <optional>

#include <everest/util/fsm/fsm.hpp>

#include <iso15118/d20/context.hpp>
#include <iso15118/d20/ev/context.hpp>
#include <iso15118/d20/ev/control_event.hpp>
#include <iso15118/d20/ev/states.hpp>
#include <iso15118/d20/timeout.hpp>
#include <iso15118/session/config.hpp>

#include <iso15118/session/ev_engine.hpp>
#include <iso15118/session/ev_feedback.hpp>
#include <iso15118/session/logger.hpp>

namespace iso15118 {

// ISO 15118-20 engine: wraps the d20 EV state machine starting at SessionSetup.
class D20EvEngine : public EvEngine {
public:
    D20EvEngine(io::StreamOutputView output_view, session::EvSessionConfig config,
                session::ev::feedback::Callbacks callbacks, session::SessionLogger& log, d20::Timeouts& timeouts);

    void kick_first_request() override;
    void on_packet(io::v2gtp::PayloadType, const io::StreamInputView&) override;
    void on_control_event(const d20::ev::ControlEvent&) override;
    void on_timeout(d20::TimeoutType) override;

    bool has_outgoing() const override;
    std::optional<Outgoing> take_outgoing() override;

    bool is_finished() const override;
    bool is_paused() const override;

    std::optional<std::array<uint8_t, 8>> session_id() const override;

private:
    d20::MessageExchange message_exchange;
    std::optional<d20::ev::ControlEvent> active_control_event{std::nullopt};
    d20::ev::Context ctx;
    fsm::v2::FSM<d20::ev::StateBase> fsm;
};

} // namespace iso15118
