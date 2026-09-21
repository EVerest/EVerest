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

#include <iso15118/ev/d20/context.hpp>
#include <iso15118/ev/d20/states.hpp>
#include <iso15118/ev/engine_outcome.hpp>

namespace iso15118::ev::d20 {

using FeedOutcome = ev::FeedOutcome<StateID>;

// ISO 15118-20 engine: MessageExchange + Context + FSM. Owned by the Session, one per protocol
// generation; the SupportedAppProtocol handshake runs here for every session.
class Engine {
public:
    Engine(feedback::Callbacks callbacks, message_20::datatypes::Identifier evcc_id,
           std::vector<message_20::SupportedAppProtocol> advertised_app_protocols,
           const std::optional<ControlEvent>& current_control_event,
           everest::lib::util::monitor<DcChargeParams>& dc_params,
           everest::lib::util::monitor<AcChargeParams>& ac_params,
           message_20::datatypes::ServiceCategory energy_service, DerControlFunctions der_control_functions,
           bool der_stop_on_unsupported_functions, SessionOptions options);

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    // Constructs the FSM; the initial state's enter() queues the SAP request.
    void start();
    bool started() const;

    // Decodes @p view by payload type and stages it as the pending response. Always true (-20 accepts every type).
    bool stage_response(io::v2gtp::PayloadType payload_type, const io::StreamInputView& view);
    V2gMessageType peek_response_type() const;

    FeedOutcome feed(Event ev);

    bool has_request() const;
    std::optional<std::pair<std::vector<uint8_t>, io::v2gtp::PayloadType>> take_request();
    void discard_request();

    // Response watchdog for a request sent from the current state.
    std::chrono::milliseconds response_timeout() const;
    // Bound for the current state's Ongoing polling loop, if it has one.
    std::optional<std::chrono::milliseconds> ongoing_timeout() const;

    std::chrono::milliseconds min_request_interval() const;
    std::optional<StateID> current_state() const;

    // Session-facing, engine-neutral surface (same on every generation's Engine).
    void latch(const ControlEvent& event);
    void stop();
    bool is_stopped() const;
    bool is_paused() const;
    std::optional<std::array<uint8_t, 8>> session_id() const;
    std::optional<ProtocolId> negotiated_protocol() const;
    ProtocolId protocol() const {
        return ProtocolId::ISO15118_20;
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

} // namespace iso15118::ev::d20
