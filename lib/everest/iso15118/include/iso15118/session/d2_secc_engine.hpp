// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <optional>

#include <everest/util/fsm/fsm.hpp>

#include <iso15118/d2/config.hpp>
#include <iso15118/d2/context.hpp>
#include <iso15118/d2/states.hpp>

#include <iso15118/d20/control_event.hpp>
#include <iso15118/d20/timeout.hpp>

#include <iso15118/session/config.hpp>
#include <iso15118/session/feedback.hpp>
#include <iso15118/session/logger.hpp>
#include <iso15118/session/secc_engine.hpp>

namespace iso15118 {

// ISO 15118-2 SECC engine: wraps the d2 SECC state machine starting at SessionSetup. Mirrors
// D20SeccEngine but over the message_2 types; the module-facing feedback and control events remain the
// reused d20 ones.
class D2SeccEngine : public SeccEngine {
public:
    D2SeccEngine(io::StreamOutputView output_view, const session::SessionConfig& config,
                 std::optional<d2::PauseContext>& pause_ctx, session::feedback::Callbacks callbacks,
                 session::SessionLogger& log, d20::Timeouts& timeouts, bool tls_active);

    void on_packet(io::v2gtp::PayloadType, const io::StreamInputView&) override;
    void on_control_event(const d20::ControlEvent&) override;
    void on_timeout(d20::TimeoutType) override;

    bool has_outgoing() const override;
    std::optional<Outgoing> take_outgoing() override;

    bool is_finished() const override;
    bool is_paused() const override;
    bool is_finished_with_error() const override;
    std::optional<session::feedback::SessionStopAction> pop_session_stop_res_pending() override;

    void request_shutdown() override;

    // ISO 15118-2: pace responses to be sent a fixed delay after the request was received.
    bool delay_response_after_request() const override {
        return true;
    }

private:
    d2::MessageExchange message_exchange;
    std::optional<d20::ControlEvent> active_control_event{std::nullopt};
    d2::Context ctx;
    fsm::v2::FSM<d2::StateBase> fsm;
};

} // namespace iso15118
