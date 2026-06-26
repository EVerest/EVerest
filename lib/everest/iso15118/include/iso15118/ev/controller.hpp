// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <atomic>
#include <memory>
#include <optional>

#include <everest/io/event/fd_event_handler.hpp>
#include <everest/io/event/timer_fd.hpp>

#include <iso15118/io/ipv6_endpoint.hpp>
#include <iso15118/session/logger.hpp>

// Order is load-bearing. The EV session/context/feedback headers reference the
// V2G I/O types as unqualified `io::...`, which must resolve to
// `iso15118::io::...`. The `ev/io/*` client headers open the sibling namespace
// `iso15118::ev::io`; once it is visible, unqualified `io::` lookup from within
// `iso15118::ev::*` finds that namespace first and fails. Including the session
// stack (which transitively pulls config/context/feedback) before the io
// clients keeps `io::` resolving against `iso15118::io`.
#include <iso15118/ev/config.hpp>
#include <iso15118/ev/session.hpp>
#include <iso15118/ev/session/feedback.hpp>

#include <iso15118/ev/io/data_client.hpp>
#include <iso15118/ev/io/sdp_client.hpp>

namespace iso15118::ev {

/**
 * EV-side entry point, mirroring the SECC \ref iso15118::TbdController.
 *
 * Owns the libio \ref everest::lib::io::event::fd_event_handler reactor, the
 * \ref io::SdpClient, and the \ref Session. The data-path client is created at
 * runtime from the SDP response's transport security (see \ref establish_data_path)
 * so plain-TCP and a future TLS client are selected per session; once created it
 * is wired to the Session's outbound-send seam and its rx callback feeds
 * \ref Session::on_bytes_received.
 *
 * Unlike the SECC controller (whose loop() is blocking and infinite), \ref loop
 * drives the reactor's \c run loop and returns once the session finishes or a
 * single-shot timeout \ref everest::lib::io::event::timer_fd elapses (both clear
 * the \c online flag), so the integration tests terminate.
 */
class Controller {
public:
    Controller(EvConfig config, feedback::Callbacks callbacks);

    Controller(const Controller&) = delete;
    Controller& operator=(const Controller&) = delete;
    Controller(Controller&&) = delete;
    Controller& operator=(Controller&&) = delete;

    /**
     * @brief Resolve the SECC endpoint, connect, and pump the reactor.
     * @details If config.discover, runs SDP discovery to learn the endpoint;
     * otherwise uses config.fixed_endpoint. Then connects the data client,
     * starts the session on connect, and pumps the reactor until the session is
     * finished or a bounded deadline elapses. Fires feedback.connected on
     * endpoint resolution and feedback.stopped before returning.
     */
    void loop();

    /**
     * @brief Deliver a control event into the session from another thread.
     * @details Marshals onto the reactor thread (add_action): the event is fed to
     * the Session there, never touching pump/session state directly off-thread.
     * Safe to call before the session starts (the action runs once the reactor is
     * pumping; deliver_control_event is a no-op without an FSM).
     */
    void post_control_event(d20::ControlEvent event);

    /**
     * @brief Request the loop to stop.
     * @details Clears `online` and wakes the reactor (an empty action) so a
     * run() blocked in poll() returns on the next iteration. Single-session: this
     * is a flag + wake, not a teardown of the session.
     */
    void shutdown();

private:
    // Create the transport client matching @p security (plain TCP today; the TLS
    // branch is the single seam for a future libio TLS client), wire it to the
    // session, and connect to @p endpoint, starting the session on connect.
    void establish_data_path(const iso15118::io::Ipv6EndPoint& endpoint, iso15118::io::v2gtp::Security security);

    const EvConfig config;
    const Feedback feedback;

    // Cleared from the reactor to stop run(): by the session when it finishes, by
    // the setup timeout if discovery/connect never completes, by a connect failure,
    // or by shutdown().
    std::atomic_bool online{false};

    // Bounds the pre-session phase (SDP discovery + TCP connect) only; disarmed
    // once the session starts, after which the session's own response watchdog
    // governs. Single-shot.
    everest::lib::io::event::timer_fd setup_timeout;

    // Re-issues the SDP request on the standard retransmit interval until the SECC
    // responds (on_found) or the setup timeout elapses. Periodic; disarmed on both.
    everest::lib::io::event::timer_fd sdp_retry;

    // Declaration order is load-bearing: the reactor outlives the clients that
    // register fds with it, and the logger outlives the Session that references
    // it. The data client is created at runtime (in establish_data_path) once the
    // transport security is known, so the Session's outbound lambda dereferences
    // it lazily; both are held by unique_ptr and the Session is constructed last.
    everest::lib::io::event::fd_event_handler reactor;
    session::SessionLogger logger;
    std::optional<io::SdpClient> sdp_client;
    std::unique_ptr<io::DataClient> data_client;
    std::unique_ptr<Session> session;
};

} // namespace iso15118::ev
