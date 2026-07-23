// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#include <iso15118/d20/ev/control_event.hpp>
#include <iso15118/d20/timeout.hpp>
#include <iso15118/io/sdp.hpp>
#include <iso15118/io/stream_view.hpp>
#include <iso15118/message/type.hpp>
#include <iso15118/message/v2g_message_type.hpp>

namespace iso15118 {

// Protocol-generation-specific state machine driver. EvSession runs the (protocol-independent)
// SupportedAppProtocol handshake itself and then hands the session over to the engine selected for
// the negotiated protocol. The engine owns the message exchange, the decode-by-context and the FSM.
class EvEngine {
public:
    virtual ~EvEngine() = default;

    // An outgoing request staged by the engine, its payload sitting in the shared output buffer.
    struct Outgoing {
        size_t payload_size;
        io::v2gtp::PayloadType payload_type;
        V2gMessageType message_type;
    };

    // Feed SEND_REQUEST into the initial state so it emits its first request.
    virtual void kick_first_request() = 0;

    // Decode an incoming V2GTP payload and drive the FSM (V2GTP_MESSAGE, then SEND_REQUEST on a
    // transition). The payload type is passed alongside the bytes because pre-20 generations share a
    // single V2GTP payload type and are disambiguated by protocol context.
    virtual void on_packet(io::v2gtp::PayloadType, const io::StreamInputView&) = 0;

    virtual void on_control_event(const d20::ev::ControlEvent&) = 0;
    virtual void on_timeout(d20::TimeoutType) = 0;

    virtual bool has_outgoing() const = 0;
    virtual std::optional<Outgoing> take_outgoing() = 0;

    virtual bool is_finished() const = 0;
    virtual bool is_paused() const = 0;

    virtual std::optional<std::array<uint8_t, 8>> session_id() const = 0;
};

} // namespace iso15118
