// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <optional>

#include <everest/util/fsm/fsm.hpp>

#include <iso15118/d20/context.hpp>
#include <iso15118/d20/control_event.hpp>
#include <iso15118/d20/ev_information.hpp>
#include <iso15118/d20/states.hpp>
#include <iso15118/d20/timeout.hpp>

#include <iso15118/io/sha_hash.hpp>
#include <iso15118/message/supported_app_protocol.hpp>

#include <iso15118/session/config.hpp>
#include <iso15118/session/feedback.hpp>
#include <iso15118/session/logger.hpp>
#include <iso15118/session/secc_engine.hpp>

namespace iso15118 {

// ISO 15118-20 SECC engine: wraps the d20 server state machine starting at SessionSetup.
class D20SeccEngine : public SeccEngine {
public:
    D20SeccEngine(io::StreamOutputView output_view, session::SessionConfig config,
                  std::optional<d20::PauseContext>& pause_ctx, session::feedback::Callbacks callbacks,
                  session::SessionLogger& log, d20::Timeouts& timeouts,
                  const d20::EVSupportedAppProtocols& offered_protocols,
                  const message_20::SupportedAppProtocol& selected_protocol,
                  std::optional<io::sha512_hash_t> vehicle_cert_hash);

    void on_packet(io::v2gtp::PayloadType, const io::StreamInputView&) override;
    void on_control_event(const d20::ControlEvent&) override;
    void on_timeout(d20::TimeoutType) override;

    bool has_outgoing() const override;
    std::optional<Outgoing> take_outgoing() override;

    bool is_finished() const override;
    bool is_paused() const override;
    std::optional<session::feedback::SessionStopAction> pop_session_stop_res_pending() override;

    void request_shutdown() override;

private:
    d20::MessageExchange message_exchange;
    std::optional<d20::ControlEvent> active_control_event{std::nullopt};
    d20::Context ctx;
    fsm::v2::FSM<d20::StateBase> fsm;
};

} // namespace iso15118
