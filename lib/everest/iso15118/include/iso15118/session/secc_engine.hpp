// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstddef>
#include <optional>

#include <iso15118/d20/control_event.hpp>
#include <iso15118/d20/timeout.hpp>
#include <iso15118/io/sdp.hpp>
#include <iso15118/io/stream_view.hpp>
#include <iso15118/message/type.hpp>
#include <iso15118/message/v2g_message_type.hpp>

namespace iso15118 {

// Protocol-generation-specific state machine driver, the SECC-side (wire-normal) mirror of EvEngine.
// The Session runs the (protocol-independent) SupportedAppProtocol handshake itself and then hands the
// session over to the engine selected for the negotiated protocol. The engine owns the message
// exchange, the decode-by-context and the FSM. Incoming packets are requests; the FSM produces the
// responses.
class SeccEngine {
public:
    virtual ~SeccEngine() = default;

    // An outgoing response staged by the engine, its payload sitting in the shared output buffer.
    struct Outgoing {
        size_t payload_size;
        io::v2gtp::PayloadType payload_type;
        V2gMessageType message_type;
    };

    // Decode an incoming V2GTP request payload and drive the FSM (V2GTP_MESSAGE). The payload type is
    // passed alongside the bytes because pre-20 generations share a single V2GTP payload type and are
    // disambiguated by protocol context.
    virtual void on_packet(io::v2gtp::PayloadType, const io::StreamInputView&) = 0;

    virtual void on_control_event(const d20::ControlEvent&) = 0;
    virtual void on_timeout(d20::TimeoutType) = 0;

    virtual bool has_outgoing() const = 0;
    virtual std::optional<Outgoing> take_outgoing() = 0;

    virtual bool is_finished() const = 0;
    virtual bool is_paused() const = 0;

    // Ask the running state machine to shut the session down gracefully.
    virtual void request_shutdown() = 0;

    // ISO 15118-2 / DIN 70121 only: some EVs are not ready to receive the response immediately after
    // sending their request and their controller may crash if the SECC answers too fast. When true, the
    // Session delays each response so it is sent a fixed time after the request was received (deducting
    // the internal processing time). ISO 15118-20 does not need this and returns false (the default).
    virtual bool delay_response_after_request() const {
        return false;
    }
};

namespace detail {
// Upper bound on the number of successive state transitions triggered by a single decoded request.
constexpr int SECC_REFEED_BOUND = 8;
} // namespace detail

// Feed a decoded request into the FSM and re-feed it while a resting state defers the still-pending
// request to the state it transitioned to (peek-and-divert without consuming, e.g. PreCharge ->
// PowerDelivery or any resting state -> SessionStop). Shared by the d2, din and d20 SECC engines. The
// loop exits as soon as a response has been staged (the safe exit condition) and is bounded to guard
// against an accidental transition ping-pong.
template <typename Fsm, typename Exchange, typename EventType>
void drive_request(Fsm& fsm, Exchange& exchange, EventType message_event) {
    auto res = fsm.feed(message_event);
    for (int guard = 0; guard < detail::SECC_REFEED_BOUND and res.transitioned() and exchange.has_request() and
                        not exchange.has_response();
         ++guard) {
        res = fsm.feed(message_event);
    }
}

} // namespace iso15118
