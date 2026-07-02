// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

#include <everest/io/event/fd_event_handler.hpp>
#include <everest/io/event/timer_fd.hpp>
#include <everest/util/fsm/fsm.hpp>

#include <iso15118/io/sdp_packet.hpp>
#include <iso15118/message/common_types.hpp>
#include <iso15118/message/supported_app_protocol.hpp>

#include <iso15118/ev/d20/context.hpp>
#include <iso15118/ev/d20/control_event.hpp>
#include <iso15118/ev/d20/states.hpp>
#include <iso15118/ev/dc_charge_params.hpp>
#include <iso15118/ev/session/feedback.hpp>
#include <iso15118/session/logger.hpp>

namespace iso15118::ev {

/**
 * Timer durations for the EV session.
 */
struct SessionTiming {
    // Delay between a request becoming ready and it being transmitted (the hold
    // window in which events may replace the pending request).
    std::chrono::milliseconds send_delay;
    // How long to wait for a response after a request is sent before failing.
    std::chrono::milliseconds response_timeout;
};

/**
 * EV-side V2GTP frame pump with timer-driven, decoupled transmission.
 *
 * Bridges the raw byte data path (\ref io::DataClient) and the d20 FSM. It owns
 * the \ref d20::MessageExchange, the \ref d20::Context, the FSM, and two
 * reactor-registered timers:
 *  - send-delay: when a request becomes pending (FSM-produced, or the first one
 *    on \ref start), it is HELD; the timer fires after \c send_delay and the
 *    request is serialized and transmitted then. Events arriving during the hold
 *    may replace the pending request before it is encoded.
 *  - response watchdog: armed when a request is transmitted; if no response
 *    arrives before \c response_timeout it feeds the FSM \ref d20::Event::FAILED,
 *    stops the session and fires the \c timed_out feedback.
 *
 * Both the outbound-send seam (a \c std::function) and the reactor injection
 * keep the pump unit-testable: a test pumps a real reactor with short timers,
 * captures emitted frames and injects responses via \ref on_bytes_received.
 */
class Session {
public:
    // Returns true if the frame was accepted for transmission, false on a send
    // failure (e.g. the data path is gone). transmit_pending stops the session
    // loudly on false rather than leaving the response watchdog armed for a frame
    // that never left.
    using OutboundSend = std::function<bool(std::vector<uint8_t>)>;

    Session(feedback::Callbacks callbacks, OutboundSend outbound_send, session::SessionLogger& logger,
            everest::lib::io::event::fd_event_handler& reactor, SessionTiming timing,
            message_20::datatypes::Identifier evcc_id = "EVCCID00000",
            std::vector<message_20::SupportedAppProtocol> advertised_app_protocols = {{"urn:iso:std:iso:15118:-20:DC",
                                                                                       1, 0, 1, 1}},
            everest::lib::util::monitor<DcChargeParams>* dc_params = nullptr);

    ~Session();

    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    Session(Session&&) = delete;
    Session& operator=(Session&&) = delete;

    /**
     * @brief Start the session: construct the FSM (whose initial state queues the
     * SAP request) and arm the send-delay timer so the request transmits after
     * the configured delay.
     */
    void start();

    /**
     * @brief Feed received bytes into the frame accumulator.
     * @details May be handed partial or multiple frames. On each complete V2GTP
     * frame the response watchdog is disarmed, the payload is decoded and fed to
     * the FSM, and any resulting request is held for the send delay.
     */
    void on_bytes_received(const std::vector<uint8_t>& bytes);

    /**
     * @brief Deliver a control event from the owner (the Controller) into the FSM.
     * @details Apply-before-feed: sets the active event so states can read it via
     * Context::get_control_event<T>(), feeds CONTROL_MESSAGE, then clears it. Any
     * resulting request is held for the send delay.
     */
    void deliver_control_event(const d20::ControlEvent& event);

    bool is_finished() const;

    /**
     * @brief Register a callback invoked once when the session becomes finished.
     * @details Finishing can happen inside a timer callback (the watchdog), so a
     * reactor-driven owner cannot poll is_finished() between events; this hook
     * lets it stop its run loop promptly.
     */
    void set_on_finished(std::function<void()> on_finished);

private:
    void handle_complete_frame();
    void arm_send_delay();
    void transmit_pending();
    void on_send_delay_expired();
    void on_watchdog_expired();
    void check_finished();

    // Declaration order matters: the MessageExchange, logger, and the control-event
    // optional are referenced by the Context, so they must outlive it (declared
    // before it). The pump OWNS the control-event optional the Context holds by
    // reference (default-initialized to std::nullopt).
    d20::MessageExchange message_exchange;
    session::SessionLogger& log;
    std::optional<d20::ControlEvent> active_control_event;
    d20::Context context;

    OutboundSend outbound_send;

    everest::lib::io::event::fd_event_handler& reactor;
    SessionTiming timing;
    everest::lib::io::event::timer_fd send_delay_timer;
    everest::lib::io::event::timer_fd watchdog_timer;

    std::function<void()> on_finished;
    bool finished_signalled{false};

    // The FSM constructs by calling its initial state's enter() in its
    // constructor (fsm.hpp). Holding it lazily defers the SupportedAppProtocol
    // entry (which queues the SAP request) until start(), so a constructed but
    // not-yet-started Session emits nothing.
    std::optional<fsm::v2::FSM<d20::StateBase>> fsm;

    io::SdpPacket packet{};
};

} // namespace iso15118::ev
