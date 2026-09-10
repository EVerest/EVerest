// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <variant>
#include <vector>

#include <everest/io/event/fd_event_handler.hpp>
#include <everest/io/event/timer_fd.hpp>

#include <iso15118/io/sdp_packet.hpp>
#include <iso15118/message/common_types.hpp>
#include <iso15118/message/supported_app_protocol.hpp>
#include <iso15118/session/protocol.hpp>

#include <iso15118/ev/ac_charge_params.hpp>
#include <iso15118/ev/d2/engine.hpp>
#include <iso15118/ev/d20/context.hpp>
#include <iso15118/ev/d20/control_event.hpp>
#include <iso15118/ev/d20/engine.hpp>
#include <iso15118/ev/dc_charge_params.hpp>
#include <iso15118/ev/der_control_functions.hpp>
#include <iso15118/ev/din/engine.hpp>
#include <iso15118/ev/session/feedback.hpp>
#include <iso15118/ev/session_params.hpp>

namespace iso15118::ev {

struct SessionTiming {
    // Hold window between a request becoming ready and its transmission; events arriving in it may
    // replace the pending request.
    std::chrono::milliseconds send_delay;
    // Response watchdog per request. Zero: per-message table of the running engine.
    std::chrono::milliseconds response_timeout;
};

/**
 * EV-side V2GTP frame engine between the byte data path (\ref transport::DataClient) and the
 * protocol engine (\ref d20::Engine). Three reactor-registered timers: send delay, response
 * watchdog, ongoing guard.
 */
class Session {
public:
    // Returns false when the frame could not be handed to the data path; the session then stops.
    using OutboundSend = std::function<bool(std::vector<uint8_t>)>;

    Session(feedback::Callbacks callbacks, OutboundSend outbound_send,
            everest::lib::io::event::fd_event_handler& reactor, SessionTiming timing,
            message_20::datatypes::Identifier evcc_id,
            std::vector<message_20::SupportedAppProtocol> advertised_app_protocols,
            everest::lib::util::monitor<DcChargeParams>* dc_params = nullptr,
            everest::lib::util::monitor<AcChargeParams>* ac_params = nullptr,
            message_20::datatypes::ServiceCategory energy_service = message_20::datatypes::ServiceCategory::DC,
            DerControlFunctions der_control_functions = {}, bool der_stop_on_unsupported_functions = true,
            d20::SessionOptions options = {}, EvSessionParams params = {});

    ~Session();

    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    Session(Session&&) = delete;
    Session& operator=(Session&&) = delete;

    // Constructs the FSM (its initial state queues the SAP request) and arms the send delay.
    void start();

    // Feed received bytes; partial and multiple frames are accepted.
    void on_bytes_received(const std::vector<uint8_t>& bytes);

    // Peer closed the connection. Regular after SessionStopRes; a mid-session close stops the session.
    void on_peer_closed();

    // Apply-before-feed: the event is visible via Context::get_control_event<T>() during the feed.
    // Stop/pause/CP state are also latched on the Context so they survive delivery before start().
    void deliver_control_event(const d20::ControlEvent& event);

    // Immediate teardown without SessionStop ([V2G2-025], [V2G2-728]: CP state E/F, unplug).
    void terminate();

    bool is_finished() const;
    bool is_paused() const;
    std::optional<std::array<uint8_t, 8>> session_id() const;
    std::optional<ProtocolId> selected_protocol() const;

    // Invoked once when the session becomes finished (may happen inside a timer callback).
    void set_on_finished(std::function<void()> on_finished);

private:
    void handle_complete_frame();
    void feed_fsm(d20::Event ev);
    // Replace the -20 engine (which ran SAP) by the engine of the negotiated generation and start it.
    void switch_engine(ProtocolId protocol);
    void arm_send_delay();
    void transmit_pending();
    void on_send_delay_expired();
    void on_watchdog_expired();
    void on_ongoing_expired();
    void update_ongoing_guard(bool transitioned);
    void check_finished();
    // Watchdog window for the request in flight: the configured override, else the engine's table.
    std::chrono::milliseconds effective_response_timeout() const;

    // Reactor exception boundary: on any throw log against @p op, stop the session, then
    // check_finished(). poll_impl has no try/catch.
    template <typename F> void guarded(const char* op, F&& f);

    // Referenced by the engine's Context; declared before it.
    std::optional<d20::ControlEvent> active_control_event;
    everest::lib::util::monitor<DcChargeParams> owned_dc_params{DcChargeParams{}};
    everest::lib::util::monitor<AcChargeParams> owned_ac_params{AcChargeParams{}};

    const feedback::Callbacks callbacks;
    const Feedback feedback;
    everest::lib::util::monitor<DcChargeParams>& dc_params;
    const EvSessionParams params;
    const bool has_cp_state_feedback;
    const std::optional<std::array<uint8_t, 8>> resumed_session_id;

    // Engine of the running protocol generation; never moved, only emplaced.
    std::variant<std::monostate, d20::Engine, d2::Engine, din::Engine> engine;

    OutboundSend outbound_send;

    everest::lib::io::event::fd_event_handler& reactor;
    SessionTiming timing;
    everest::lib::io::event::timer_fd send_delay_timer;
    everest::lib::io::event::timer_fd watchdog_timer;
    everest::lib::io::event::timer_fd ongoing_timer;
    bool ongoing_armed{false};

    std::function<void()> on_finished;
    bool finished_signalled{false};

    // Set when the send-delay timer could not be armed: the pending request can never transmit.
    bool pending_request_unsendable{false};

    io::SdpPacket packet{};
};

} // namespace iso15118::ev
