// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <string_view>
#include <variant>
#include <vector>

#include <everest/io/event/fd_event_handler.hpp>
#include <everest/io/event/timer_fd.hpp>

#include <iso15118/io/log_levels.hpp>
#include <iso15118/io/sdp_packet.hpp>
#include <iso15118/message/common_types.hpp>
#include <iso15118/message/supported_app_protocol.hpp>
#include <iso15118/session/protocol.hpp>
#include <iso15118/v2gtp/sdp.hpp>

#include <iso15118/ev/d2/engine.hpp>
#include <iso15118/ev/d20/context.hpp>
#include <iso15118/ev/d20/control_event.hpp>
#include <iso15118/ev/d20/engine.hpp>
#include <iso15118/ev/din/engine.hpp>
#include <iso15118/ev/feedback.hpp>
#include <iso15118/ev/params/ac.hpp>
#include <iso15118/ev/params/dc.hpp>
#include <iso15118/ev/params/iso2_ac.hpp>
#include <iso15118/ev/session/latches.hpp>

namespace iso15118::ev::session {

struct SessionTiming {
    // Hold window between a request becoming ready and its transmission; events arriving in it may
    // replace the pending request.
    std::chrono::milliseconds send_delay;
    // Response watchdog per request. Zero: per-message table of the running engine.
    std::chrono::milliseconds response_timeout;
};

/**
 * EV-side V2GTP frame engine between the byte data path (\ref transport::Connection) and the
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
            everest::lib::util::monitor<params::DcChargeParams>* dc_params = nullptr,
            everest::lib::util::monitor<params::AcChargeParams>* ac_params = nullptr,
            message_20::datatypes::ServiceCategory energy_service = message_20::datatypes::ServiceCategory::DC,
            d20::SessionOptions options = {}, params::EvSessionParams params = {});

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

    // Latched before the feed, so it also survives delivery before start() and a handover.
    void deliver_control_event(const d20::ControlEvent& event);

    // Record whether the data path is TLS. Called once, after the SDP response names the security
    // and before any engine reads it: the Session exists before SDP has run, so this cannot be a
    // constructor argument.
    void set_transport_security(v2gtp::Security security);

    // Replace the SAP offer before start(): the TLS handshake can narrow it once it names the version.
    void set_offer(std::vector<OfferedProtocol> offer);

    // Immediate teardown without SessionStop: drop the connection instead of waiting for a
    // SessionStopRes. ISO 15118-20 carries an analogue of the ISO 15118-2 rule this follows,
    // but its requirement number could not be pinned, so none is cited here.
    void terminate();

    bool is_finished() const;
    bool is_paused() const;
    std::optional<std::array<uint8_t, 8>> session_id() const;
    std::optional<ProtocolId> selected_protocol() const;
    // The -20 engine's clock; empty for the other generations.
    d20::SeccClock::State secc_clock_state() const;
    // The -20 engine's service selection; empty before ServiceDetail and for the other generations.
    std::optional<d20::SelectedService> service_selection() const;

    // Invoked once when the session becomes finished (may happen inside a timer callback).
    void set_on_finished(std::function<void()> on_finished);

private:
    void handle_complete_frame();
    // Returns the protocol generation SAP negotiated when the feed asked for a handover. Reported
    // rather than performed: the caller holds a reference to the engine that would be replaced.
    std::optional<ProtocolId> feed_fsm(d20::Event ev);
    // Take over from the -20 engine after SAP negotiated another generation.
    void switch_engine(ProtocolId protocol);
    void arm_send_delay();
    void transmit_pending();
    void on_send_delay_expired();
    void on_watchdog_expired();
    void on_ongoing_expired();
    void update_ongoing_guard(bool transitioned);
    void check_finished();
    // Give up on the session: log @p reason, stop, discard the pending request, disarm every timer,
    // then check_finished(). Never throws: it also runs from guarded's exception handlers.
    void abort(std::string_view reason, LogLevel level = LogLevel::Error) noexcept;
    // abort() for guarded's handlers: the reason names @p op and @p what, without letting a failed
    // allocation escape the handler.
    void abort_after_throw(const char* op, const char* what) noexcept;
    // Watchdog window for the request in flight: the configured override, else the engine's table.
    std::chrono::milliseconds effective_response_timeout() const;
    bool in_welding_detection() const;

    // Reactor exception boundary: on any throw abort against @p op, then check_finished().
    // poll_impl has no try/catch.
    template <typename F> void guarded(const char* op, F&& f);

    // Referenced by the engine's Context; declared before it.
    Latches latches;
    everest::lib::util::monitor<params::DcChargeParams> owned_dc_params{params::DcChargeParams{}};
    everest::lib::util::monitor<params::AcChargeParams> owned_ac_params{params::AcChargeParams{}};

    const feedback::Callbacks callbacks;
    const Feedback feedback;
    everest::lib::util::monitor<params::DcChargeParams>& dc_params;
    params::EvSessionParams params;
    const bool has_cp_state_feedback;
    const config::WeldingDetectionConfig welding_detection;
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

    io::SdpPacket packet{};
};

} // namespace iso15118::ev::session
