// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include <everest/util/fsm/fsm.hpp>

#include <iso15118/io/sdp.hpp>
#include <iso15118/io/stream_view.hpp>
#include <iso15118/message/v2g_message_type.hpp>
#include <iso15118/session/protocol.hpp>

#include <iso15118/ev/d2/context.hpp>
#include <iso15118/ev/d2/states.hpp>
#include <iso15118/ev/engine_outcome.hpp>

namespace iso15118::ev::d2 {

// ISO 15118-2 engine: MessageExchange + Context + FSM starting at SessionSetup. Same method set as
// ev::d20::Engine (the Session dispatches over a std::variant of engines).
class Engine {
public:
    Engine(feedback::Callbacks callbacks, EvSessionParams params,
           const std::optional<ControlEvent>& current_control_event,
           everest::lib::util::monitor<DcChargeParams>& dc_params, bool has_cp_state_feedback,
           std::optional<std::array<uint8_t, 8>> resumed_session_id);

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    void start();
    bool started() const;

    // False when the frame is not an ISO 15118-2 payload (type 0x8001) and was dropped [V2G2-086].
    bool stage_response(io::v2gtp::PayloadType payload_type, const io::StreamInputView& view);
    V2gMessageType peek_response_type() const;

    FeedOutcome<StateID> feed(Event ev);

    bool has_request() const;
    std::optional<std::pair<std::vector<uint8_t>, io::v2gtp::PayloadType>> take_request();
    void discard_request();

    std::chrono::milliseconds response_timeout() const;
    std::optional<std::chrono::milliseconds> ongoing_timeout() const;
    std::chrono::milliseconds min_request_interval() const;
    std::optional<StateID> current_state() const;

    // Session-facing, engine-neutral surface.
    void latch(const ControlEvent& event);
    void stop();
    bool is_stopped() const;
    bool is_paused() const;
    std::optional<std::array<uint8_t, 8>> session_id() const;
    std::optional<ProtocolId> negotiated_protocol() const;
    ProtocolId protocol() const {
        return ProtocolId::ISO15118_2;
    }

    Context& context() {
        return ctx;
    }
    const Context& context() const {
        return ctx;
    }

private:
    MessageExchange message_exchange;
    Context ctx;
    std::optional<fsm::v2::FSM<StateBase>> fsm;
};

} // namespace iso15118::ev::d2
