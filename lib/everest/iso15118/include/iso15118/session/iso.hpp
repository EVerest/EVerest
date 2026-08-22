// SPDX-License-Identifier: Apache-2.0
// Copyright 2023 Pionix GmbH and Contributors to EVerest
#pragma once

#include <memory>
#include <optional>
#include <utility>
#include <variant>

#include <iso15118/config.hpp>

#include <everest/util/queue/thread_safe_queue.hpp>

#include <iso15118/d2/config.hpp>
#include <iso15118/d20/control_event.hpp>
#include <iso15118/d20/ev_information.hpp>
#include <iso15118/d20/session.hpp>
#include <iso15118/d20/timeout.hpp>

#include <iso15118/io/connection_abstract.hpp>
#include <iso15118/io/poll_manager.hpp>
#include <iso15118/io/sdp_packet.hpp>
#include <iso15118/io/sha_hash.hpp>
#include <iso15118/io/time.hpp>

#include <iso15118/message/supported_app_protocol.hpp>
#include <iso15118/message/type.hpp>

#include <iso15118/session/config.hpp>
#include <iso15118/session/d20_secc_engine.hpp>
#include <iso15118/session/d2_secc_engine.hpp>
#include <iso15118/session/din_secc_engine.hpp>
#include <iso15118/session/feedback.hpp>
#include <iso15118/session/protocol.hpp>
#include <iso15118/session/sap_engine.hpp>
#include <iso15118/session/secc_engine.hpp>

#include <iso15118/d20/timeout.hpp>

namespace iso15118 {

struct SessionState {
    bool connected{false};
    bool new_data{false};
    bool fsm_needs_call{false};
};

// SECC-side session driver. It owns the transport, the shared in/out buffers and the timing rules, and
// delegates the protocol itself to the engine it currently runs on: every session starts on the
// SapEngine (SupportedAppProtocol handshake) and is handed over to the engine of the negotiated
// generation once that handshake succeeded. See secc_engine.hpp for the engine contract.
class Session {
public:
    Session(std::unique_ptr<io::IConnection>, session::SessionConfig, const session::feedback::Callbacks&,
            std::optional<d20::PauseContext>&, std::optional<d2::PauseContext>&);
    // Skip the SupportedAppProtocol handshake: the caller has already negotiated the protocol (external
    // SAP, e.g. start_session() on a handed-over socket) and the session starts directly on the
    // ISO 15118-20 engine, expecting a SessionSetupReq as the first message.
    Session(std::unique_ptr<io::IConnection>, session::SessionConfig, const session::feedback::Callbacks&,
            std::optional<d20::PauseContext>&, std::optional<d2::PauseContext>&, bool skip_app_protocol_negotiation);
    ~Session();

    TimePoint const& poll();
    void push_control_event(const d20::ControlEvent&);

    // True once the end-of-session handling completed (TCP closed, D-LINK signal sent) and the
    // controller can reap the session.
    bool is_finished() const;

    // True once the V2G communication session is established, i.e. the first application request
    // (SessionSetupReq) has reached the engine after the SupportedAppProtocol handshake. Until then the
    // controller keeps V2G_SECC_CommunicationSetup_Timeout armed (it spans SLAC -> SDP -> TCP -> SAP ->
    // SessionSetupReq); it is cancelled here and the per-message V2G_SECC_Sequence_Timeout takes over.
    bool is_v2g_session_established() const {
        return v2g_session_established;
    }

    void close();

    void request_shutdown();

private:
    // The V2G session is logically over (engine finished / driver stopped); the TCP connection may
    // still be open while we wait for the EV to close it first.
    bool session_over() const;
    // Close the connection (if still open) and send the D-LINK signal; marks the session reapable.
    void finish_session();

    std::unique_ptr<io::IConnection> connection;

    session::SessionConfig config;
    session::feedback::Callbacks callbacks;
    session::Feedback feedback;

    SessionState state;
    // input buffer
    io::SdpPacket packet;

    // output buffer, shared with the engine (the SupportedAppProtocol response is staged here too).
    // Sized to hold a full ISO 15118-2 Plug-and-Charge CertificateInstallationRes (contract certificate
    // chains push the EXI well past 1 kB; ~4.2 kB observed). Matches EvseV2G's DEFAULT_BUFFER_SIZE.
    uint8_t response_buffer[8192];

    // control event buffer, filled from the module's command threads and drained in poll()
    everest::lib::util::thread_safe_queue<d20::ControlEvent> control_event_queue;

    // Shared with the engine (the timeouts are protocol-agnostic).
    d20::Timeouts timeouts;

    std::optional<d20::PauseContext>& pause_ctx;
    // d2 pause context, owned by the controller so it survives the engine teardown on pause (mirrors the
    // d20 pause_ctx); a returning EV re-joins the retained ISO-2 session with OK_OldSessionJoined.
    std::optional<d2::PauseContext>& d2_pause_ctx;

    // Vehicle certificate hash captured on connection OPEN, handed to the engine on creation.
    std::optional<io::sha512_hash_t> vehicle_cert_hash{std::nullopt};

    bool driver_stopped{false};
    // Set once the first application request (SessionSetupReq) has reached the engine; the V2G session
    // is then established and the controller drops the communication-setup timeout (is_v2g_session_established()).
    bool v2g_session_established{false};
    // End-of-session handling done (finish_session()/close() ran); controller-facing via is_finished().
    bool finished_reported{false};
    // Armed when the session ends while the EV is still connected: deadline for the EV-first TCP
    // close (CONNECTION_CLOSE_LINGER_MS), after which we close the connection ourselves.
    std::optional<TimePoint> connection_close_deadline{std::nullopt};
    // The session ended with a FAILED_* response (sequence error, unknown session): the SECC closes
    // the TCP connection itself without the EV-first linger ([V2G-DC-940]).
    bool error_termination{false};
    // Armed on an SECC-initiated error close: the TCP connection is already closed (FIN out), the
    // DLINK_TERMINATE signal fires when this deadline passes (FIN-flush grace, DLINK_SIGNAL_GRACE_MS).
    std::optional<TimePoint> dlink_signal_deadline{std::nullopt};

    // The engine the session currently runs on. Held by value: there is exactly one at any time, it is
    // swapped in place at the SupportedAppProtocol handover (SapEngine -> protocol engine) and the
    // alternatives have nothing in common but the contract in secc_engine.hpp, so the variant replaces
    // both the allocation and the virtual dispatch of a base-class pointer.
    using Engine = std::variant<SapEngine, DinSeccEngine, D2SeccEngine, D20SeccEngine>;
    Engine engine;

    // Call \p f on the active engine. Every member of the engine contract is reached through this, so a
    // contract violation in any alternative is a compile error here rather than a missing override.
    template <typename Function> decltype(auto) visit_engine(Function&& f) {
        return std::visit(std::forward<Function>(f), engine);
    }
    template <typename Function> decltype(auto) visit_engine(Function&& f) const {
        return std::visit(std::forward<Function>(f), engine);
    }

    // True while the session still runs on the SapEngine, i.e. no protocol has been negotiated yet.
    bool in_sap_phase() const {
        return std::holds_alternative<SapEngine>(engine);
    }

    // The engines stage their responses in response_buffer, behind the V2GTP header the Session fills
    // in on send.
    io::StreamOutputView engine_output_view();

    TimePoint next_session_event;

    void handle_connection_event(io::ConnectionEvent event);
    // Swap the SapEngine for the engine of the negotiated protocol once the handshake response has been
    // sent, or stop the driver if the handshake failed.
    void advance_sap_handover();
    // Replace the active engine with the one for the negotiated protocol. False if there is none.
    bool create_engine(const SapEngine::Negotiated&);
    void send_response();

    std::optional<TimePoint> last_response_tx_time; // timestamp of the last response message sent
    std::optional<TimePoint> last_request_rx_time;  // timestamp of the last request handed to the engine
    std::optional<TimePoint> response_send_after;   // time point when the next response message can be sent
};

} // namespace iso15118
