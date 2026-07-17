// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <iso15118/d2/ev/config.hpp>
#include <iso15118/session/config.hpp>
#include <iso15118/din/ev/config.hpp>
#include <iso15118/d20/ev/control_event.hpp>
#include <iso15118/d20/timeout.hpp>

#include <iso15118/io/connection_abstract.hpp>
#include <iso15118/io/sdp_packet.hpp>
#include <iso15118/io/time.hpp>

#include <iso15118/message/type.hpp>

#include <iso15118/session/ev_feedback.hpp>
#include <iso15118/session/logger.hpp>
#include <iso15118/session/protocol.hpp>

namespace iso15118 {

class EvEngine;

// EVCC-side session driver, the client-role mirror of iso15118::Session. It runs the (protocol-
// independent) SupportedAppProtocol handshake and then delegates the negotiated protocol generation
// to a swappable EvEngine.
class EvSession {
public:
    EvSession(std::unique_ptr<io::IConnection>, session::EvSessionConfig, const session::ev::feedback::Callbacks&);
    ~EvSession();

    TimePoint const& poll();

    void push_control_event(const d20::ev::ControlEvent&);

    bool is_finished() const;

    // Pause/resume observation for the controller. Valid once the session is finished.
    bool is_paused() const;
    std::optional<std::array<uint8_t, 8>> session_id() const;
    std::optional<ProtocolId> selected_protocol() const;

    void close();

private:
    struct State {
        bool connected{false};
        bool new_data{false};
        bool initial_request_sent{false};
        bool teardown_signaled{false};
    };

    // A request staged in request_buffer and ready to be written to the wire.
    struct PendingOutgoing {
        size_t payload_size;
        io::v2gtp::PayloadType payload_type;
        message_20::Type message_type;
    };

    // Namespace an offered SupportedAppProtocol maps to, keyed by its schema id.
    struct OfferedProtocol {
        uint8_t schema_id;
        ProtocolId protocol;
    };

    std::unique_ptr<io::IConnection> connection;
    session::SessionLogger log;

    session::EvSessionConfig config;
    session::ev::feedback::Callbacks callbacks;
    session::ev::Feedback feedback;

    State state;

    // input buffer
    io::SdpPacket packet;

    // output buffer
    uint8_t request_buffer[1028];

    d20::ev::ControlEventQueue control_event_queue;

    // Shared with the engine (the timeouts are protocol-agnostic).
    d20::Timeouts timeouts;

    // SupportedAppProtocol phase (before an engine exists).
    std::vector<OfferedProtocol> offered_protocols;
    std::optional<PendingOutgoing> pending_sap_outgoing{std::nullopt};
    bool driver_stopped{false};

    std::unique_ptr<EvEngine> engine{nullptr};
    // Protocol generation the engine was created for (set once the SAP handshake succeeds).
    std::optional<ProtocolId> selected_protocol_id{std::nullopt};

    TimePoint next_session_event;

    void handle_connection_event(io::ConnectionEvent event);
    void start_supported_app_protocol();
    void handle_supported_app_protocol_response(io::v2gtp::PayloadType, const io::StreamInputView&);
    std::unique_ptr<EvEngine> create_engine(ProtocolId);
    void send_request();

    std::optional<TimePoint> last_request_tx_time; // timestamp of the last request message sent
    std::optional<TimePoint> request_send_after;   // time point at which the next request may be sent
};

} // namespace iso15118
