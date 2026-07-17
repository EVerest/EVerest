// SPDX-License-Identifier: Apache-2.0
// Copyright 2023 Pionix GmbH and Contributors to EVerest
#pragma once

#include <memory>
#include <optional>

#include <iso15118/config.hpp>

#include <iso15118/d2/config.hpp>
#include <iso15118/d20/control_event.hpp>
#include <iso15118/d20/control_event_queue.hpp>
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
#include <iso15118/session/feedback.hpp>
#include <iso15118/session/logger.hpp>
#include <iso15118/session/protocol.hpp>

namespace iso15118 {

class SeccEngine;

struct SessionState {
    bool connected{false};
    bool new_data{false};
    bool fsm_needs_call{false};
};

// SECC-side session driver. It runs the (protocol-independent) SupportedAppProtocol handshake itself
// and then delegates the negotiated protocol generation to a swappable SeccEngine.
class Session {
public:
    Session(std::unique_ptr<io::IConnection>, session::SessionConfig, const session::feedback::Callbacks&,
            std::optional<d20::PauseContext>&, std::optional<d2::PauseContext>&);
    ~Session();

    TimePoint const& poll();
    void push_control_event(const d20::ControlEvent&);

    // True once the end-of-session handling completed (TCP closed, D-LINK signal sent) and the
    // controller can reap the session.
    bool is_finished() const;

    void close();

    void request_shutdown();

private:
    // The V2G session is logically over (engine finished / driver stopped); the TCP connection may
    // still be open while we wait for the EV to close it first.
    bool session_over() const;
    // Close the connection (if still open) and send the D-LINK signal; marks the session reapable.
    void finish_session();
    // A response staged in response_buffer and ready to be written to the wire.
    struct PendingOutgoing {
        size_t payload_size;
        io::v2gtp::PayloadType payload_type;
        message_20::Type message_type;
    };

    std::unique_ptr<io::IConnection> connection;
    session::SessionLogger log;

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

    // control event buffer
    d20::ControlEventQueue control_event_queue;

    // Shared with the engine (the timeouts are protocol-agnostic).
    d20::Timeouts timeouts;

    std::optional<d20::PauseContext>& pause_ctx;
    // d2 pause context, owned by the controller so it survives the engine teardown on pause (mirrors the
    // d20 pause_ctx); a returning EV re-joins the retained ISO-2 session with OK_OldSessionJoined.
    std::optional<d2::PauseContext>& d2_pause_ctx;

    // Vehicle certificate hash captured on connection OPEN, handed to the engine on creation.
    std::optional<io::sha512_hash_t> vehicle_cert_hash{std::nullopt};

    // SupportedAppProtocol phase (before an engine exists).
    std::optional<PendingOutgoing> pending_sap_outgoing{std::nullopt};
    d20::EVSupportedAppProtocols sap_offered_protocols;
    message_20::SupportedAppProtocol sap_selected_protocol{};
    bool driver_stopped{false};
    // End-of-session handling done (finish_session()/close() ran); controller-facing via is_finished().
    bool finished_reported{false};
    // Armed when the session ends while the EV is still connected: deadline for the EV-first TCP
    // close (CONNECTION_CLOSE_LINGER_MS), after which we close the connection ourselves.
    std::optional<TimePoint> connection_close_deadline{std::nullopt};

    std::unique_ptr<SeccEngine> engine{nullptr};

    TimePoint next_session_event;

    void handle_connection_event(io::ConnectionEvent event);
    void handle_supported_app_protocol_request(io::v2gtp::PayloadType, const io::StreamInputView&);
    std::unique_ptr<SeccEngine> create_engine(ProtocolId);
    void send_response();

    std::optional<TimePoint> last_response_tx_time; // timestamp of the last response message sent
    std::optional<TimePoint> last_request_rx_time;  // timestamp of the last request handed to the engine
    std::optional<TimePoint> response_send_after;   // time point when the next response message can be sent
};

} // namespace iso15118
