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

namespace session::feedback {
enum class SessionStopAction;
} // namespace session::feedback

// An outgoing response staged by an engine, its payload sitting in the Session's output buffer.
struct SeccOutgoing {
    size_t payload_size;
    io::v2gtp::PayloadType payload_type;
    V2gMessageType message_type;
};

// The SECC-side engine contract -- the wire-normal mirror of the EvEngine interface.
//
// A Session runs on exactly one engine at a time: the SapEngine drives the (protocol-independent)
// SupportedAppProtocol handshake, and once a protocol has been negotiated the Session swaps in the
// engine for that generation (SapEngine -> DinSeccEngine / D2SeccEngine / D20SeccEngine). An engine
// owns the message exchange, the decode-by-context and the FSM; incoming packets are requests and the
// FSM produces the responses. All engines write their responses into the Session's shared output
// buffer, so only the engine currently held by the Session may stage a response.
//
// The Session holds the engines by value in a std::variant and dispatches with std::visit -- there is
// no virtual base class, so this list is the contract every alternative has to satisfy (a missing or
// mistyped member is a compile error at the visiting call site in the Session):
//
//   // Decode an incoming V2GTP request payload and drive the FSM (V2GTP_MESSAGE). The payload type is
//   // passed alongside the bytes because pre-20 generations share a single V2GTP payload type and are
//   // disambiguated by protocol context.
//   void on_packet(io::v2gtp::PayloadType, const io::StreamInputView&);
//
//   void on_control_event(const d20::ControlEvent&);
//   void on_timeout(d20::TimeoutType);
//
//   bool has_outgoing() const;
//   std::optional<SeccOutgoing> take_outgoing();
//
//   bool is_finished() const;   // must stay false while a response is still staged
//   bool is_paused() const;
//
//   // Return-and-clear the stop action armed by the SessionStop state when a positive SessionStopRes
//   // was staged. The Session drains this right after the response was written to the socket and
//   // reports it via feedback.session_stop_res_sent -- the anchor of the CP-oscillator retain time
//   // (DIN 70121 [V2G-DC-968]).
//   std::optional<session::feedback::SessionStopAction> pop_session_stop_res_pending();
//
//   // The session ended on an error condition detected outside a response (e.g. CP State A / unplug,
//   // [V2G-DC-962]): the Session closes the TCP connection immediately instead of granting the
//   // EV-first close linger.
//   bool is_finished_with_error() const;
//
//   // Ask the running state machine to shut the session down gracefully.
//   void request_shutdown();
//
//   // ISO 15118-2 / DIN 70121 (and the SupportedAppProtocol handshake): some EVs are not ready to
//   // receive the response immediately after sending their request and their controller may crash if
//   // the SECC answers too fast. When true, the Session delays each response so it is sent a fixed
//   // time after the request was received (deducting the internal processing time). ISO 15118-20 does
//   // not need this and returns false.
//   bool delay_response_after_request() const;

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
