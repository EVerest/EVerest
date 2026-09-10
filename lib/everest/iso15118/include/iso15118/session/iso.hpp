// SPDX-License-Identifier: Apache-2.0
// Copyright 2023 Pionix GmbH and Contributors to EVerest
#pragma once

#include <memory>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

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

// The power-path state the module currently holds. A session that ends the regular way clears it
// with PowerDelivery(Stop); one that is torn down instead -- mid-loop TCP drop, plug-out, kill --
// never gets there, so it has to be undone explicitly, or only EvseManager's CP-event fallback is
// left. EvseV2G does the same from connection_teardown() off its session.is_charging flag.
struct PowerPath {
    void observe(session::feedback::Signal);

    // In the order they have to be sent. Clears the state, so a second call yields nothing.
    std::vector<session::feedback::Signal> take_teardown_signals();

    bool charge_loop_running{false};
    bool ac_contactor_closed{false};
};

// Owns the transport, the shared buffers and the timing rules, and delegates the protocol to the
// engine it currently runs on. See secc_engine.hpp for the engine contract.
class Session {
public:
    Session(std::unique_ptr<io::IConnection>, session::SessionConfig, const session::feedback::Callbacks&,
            std::optional<d20::PauseContext>&, std::optional<d2::PauseContext>&);
    // Skip the SupportedAppProtocol handshake: the caller has already negotiated the protocol, so the
    // session starts directly on the -20 engine and expects a SessionSetupReq first.
    Session(std::unique_ptr<io::IConnection>, session::SessionConfig, const session::feedback::Callbacks&,
            std::optional<d20::PauseContext>&, std::optional<d2::PauseContext>&, bool skip_app_protocol_negotiation);
    ~Session();

    TimePoint const& poll();
    void push_control_event(const d20::ControlEvent&);

    // True once the end-of-session handling completed and the controller can reap the session.
    bool is_finished() const;

    // True once the first application request has reached the engine. Until then the controller keeps
    // V2G_SECC_CommunicationSetup_Timeout armed, which spans SLAC -> SDP -> TCP -> SAP -> SessionSetupReq.
    bool is_v2g_session_established() const {
        return v2g_session_established;
    }

    void close();

    void request_shutdown();

private:
    // The TCP connection may still be open while we wait for the EV to close it first.
    bool session_over() const;
    void finish_session();
    session::feedback::Signal teardown_signal() const;
    void open_power_path();

    std::unique_ptr<io::IConnection> connection;

    session::SessionConfig config;
    session::feedback::Callbacks callbacks;
    session::Feedback feedback;

    SessionState state;
    // input buffer
    io::SdpPacket packet;

    // Shared with the engine; see MAX_V2G_PACKET_SIZE for the sizing rationale.
    uint8_t response_buffer[io::MAX_V2G_PACKET_SIZE];

    everest::lib::util::thread_safe_queue<d20::ControlEvent> control_event_queue;

    d20::Timeouts timeouts;

    std::optional<d20::PauseContext>& pause_ctx;
    // Owned by the controller so it survives the engine teardown on pause (mirrors the d20 pause_ctx).
    std::optional<d2::PauseContext>& d2_pause_ctx;

    std::optional<io::sha512_hash_t> vehicle_cert_hash{std::nullopt};

    // Latest StopCharging seen in poll(). The handshake engine ignores control events, so a pending
    // stop is re-delivered to the protocol engine right after the handover (see create_engine()).
    bool pending_stop_charging{false};

    PowerPath power_path;

    // Only set for the duration of the on_packet() call; the packet buffer is reused afterwards.
    io::StreamInputView current_request_frame{};

    bool driver_stopped{false};
    // The controller then drops the communication-setup timeout (is_v2g_session_established()).
    bool v2g_session_established{false};
    bool finished_reported{false};
    // Deadline for the EV-first TCP close, after which we close the connection ourselves.
    std::optional<TimePoint> connection_close_deadline{std::nullopt};
    // The SECC closes the TCP connection itself without the EV-first linger ([V2G-DC-940]).
    bool error_termination{false};
    // One of the two regular ends, so the link is released with D-LINK_TERMINATE / D-LINK_PAUSE.
    // Anything else is an error and releases it with D-LINK_ERROR instead -- see teardown_signal().
    bool clean_session_end{false};
    // The TCP connection is already closed; the D-LINK signal fires when this FIN-flush grace passes.
    std::optional<TimePoint> dlink_signal_deadline{std::nullopt};

    // Held by value: there is exactly one at any time, swapped in place at the handover, and the
    // alternatives have nothing in common but the contract, so the variant replaces both the allocation
    // and the virtual dispatch of a base-class pointer.
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

    bool in_sap_phase() const {
        return std::holds_alternative<SapEngine>(engine);
    }

    // Staged in response_buffer, behind the V2GTP header the Session fills in on send.
    io::StreamOutputView engine_output_view();

    TimePoint next_session_event;

    void handle_connection_event(io::ConnectionEvent event);
    void advance_sap_handover();
    bool create_engine(const SapEngine::Negotiated&);
    void send_response();

    std::optional<TimePoint> last_response_tx_time; // timestamp of the last response message sent
    std::optional<TimePoint> last_request_rx_time;  // timestamp of the last request handed to the engine
    std::optional<TimePoint> response_send_after;   // time point when the next response message can be sent
};

} // namespace iso15118
