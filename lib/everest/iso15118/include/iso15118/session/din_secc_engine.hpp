// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <optional>

#include <everest/util/fsm/fsm.hpp>

#include <iso15118/d20/control_event.hpp>
#include <iso15118/d20/timeout.hpp>
#include <iso15118/din/context.hpp>
#include <iso15118/din/states.hpp>

#include <iso15118/session/config.hpp>
#include <iso15118/session/feedback.hpp>
#include <iso15118/session/secc_engine.hpp>

namespace iso15118 {

// DIN SPEC 70121 SECC engine: wraps the din server state machine starting at SessionSetup. It is the
// SECC counterpart of DinEvEngine and mirrors D20SeccEngine (DC only, plaintext, EIM).
class DinSeccEngine {
public:
    DinSeccEngine(io::StreamOutputView output_view, const session::SessionConfig& config,
                  session::feedback::Callbacks callbacks, d20::Timeouts& timeouts);

    void on_packet(io::v2gtp::PayloadType, const io::StreamInputView&);
    void on_control_event(const d20::ControlEvent&);
    void on_timeout(d20::TimeoutType);

    bool has_outgoing() const;
    std::optional<SeccOutgoing> take_outgoing();

    bool is_finished() const;
    bool is_paused() const;
    bool is_finished_with_error() const;
    std::optional<session::feedback::SessionStopAction> pop_session_stop_res_pending();

    void request_shutdown();

    // DIN 70121: pace responses to be sent a fixed delay after the request was received.
    bool delay_response_after_request() const {
        return true;
    }

private:
    din::MessageExchange message_exchange;
    std::optional<d20::ControlEvent> active_control_event{std::nullopt};
    din::Context ctx;
    fsm::v2::FSM<din::StateBase> fsm;
    // The STOP_CHARGING guard is armed (StopCharging{true} received, session not yet over): tracked
    // here so a repeated request does not restart the window and a StopCharging{false} after the guard
    // already fired does not stop an idle timeout slot.
    bool stop_charging_guard_armed{false};
};

} // namespace iso15118
