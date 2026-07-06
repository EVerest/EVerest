// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <atomic>
#include <memory>
#include <optional>

#include <everest/io/event/fd_event_handler.hpp>
#include <everest/io/event/timer_fd.hpp>
#include <everest/util/async/monitor.hpp>

#include <iso15118/io/ipv6_endpoint.hpp>
#include <iso15118/session/logger.hpp>

#include <iso15118/ev/config.hpp>
#include <iso15118/ev/dc_charge_params.hpp>
#include <iso15118/ev/session.hpp>
#include <iso15118/ev/session/feedback.hpp>
#include <iso15118/ev/transport/data_client.hpp>
#include <iso15118/ev/transport/sdp_client.hpp>

namespace iso15118::ev {

/**
 * EV-side entry point, mirroring the SECC \ref iso15118::TbdController.
 *
 * Owns the libio \ref everest::lib::io::event::fd_event_handler reactor, the
 * \ref transport::SdpClient, and the \ref Session. The data-path client is created at
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
    Controller(EvConfig config, feedback::Callbacks callbacks, DcChargeParams initial_dc_params = {});

    Controller(const Controller&) = delete;
    Controller& operator=(const Controller&) = delete;
    Controller(Controller&&) = delete;
    Controller& operator=(Controller&&) = delete;

    /**
     * @brief Resolve the SECC endpoint, connect, and run the reactor.
     * @details If config.discover, runs SDP discovery to learn the endpoint;
     * otherwise uses config.fixed_endpoint. Then connects the data client,
     * starts the session on connect, and runs the reactor until the session is
     * finished or a bounded deadline elapses. Fires feedback.connected on
     * endpoint resolution and feedback.stopped before returning.
     */
    void loop();

    /**
     * @brief Deliver a control event into the session from another thread.
     * @details Marshals onto the reactor thread (add_action): the event is fed to
     * the Session there, never touching session state directly off-thread.
     * Safe to call before the session starts (the action runs once the reactor is
     * running; deliver_control_event is a no-op without an FSM).
     */
    void post_control_event(d20::ControlEvent event);

    /**
     * @brief Request a graceful EV-initiated stop of the charging session.
     * @details Marshals a StopCharging control event onto the reactor thread (the
     * FSM walks PowerDelivery(Stop) -> DC_WeldingDetection -> SessionStop) and arms
     * a single-shot grace-period fallback that hard-stops the loop if the session
     * has not finished in time. Unlike shutdown() this lets the session close down
     * gracefully; the fallback bounds the wait.
     */
    void request_stop();

    /**
     * @brief Request the loop to stop.
     * @details Records a stop request, clears `online`, and wakes the reactor (an
     * empty action) so a run() blocked in poll() returns on the next iteration.
     * Valid at ANY point in the object lifetime: called before loop(), the recorded stop
     * makes loop() return without arming the reactor; called during, the wake ends
     * run(). Single-session: this is a flag + wake, not a teardown of the session.
     */
    void shutdown();

    /**
     * @brief Update the live present-SoC exposed to the FSM (module thread).
     * @details Locks the DC-params monitor and mutates only the live field; the
     * static params are left untouched. Safe to call while the FSM reads snapshots.
     */
    void update_present_soc(double present_soc);

    /**
     * @brief Update the live present-voltage exposed to the FSM (module thread).
     * @details Locks the DC-params monitor and mutates only the live field; the
     * static params are left untouched. Safe to call while the FSM reads snapshots.
     */
    void update_present_voltage(float present_voltage);

private:
    // Create the transport client matching @p security (plain TCP today; the TLS
    // branch is the single seam for a future libio TLS client), wire it to the
    // session, and connect to @p endpoint, starting the session on connect.
    void establish_data_path(const iso15118::io::Ipv6EndPoint& endpoint, iso15118::io::v2gtp::Security security);

    // Reactor exception boundary: run @p f, on any throw log against @p op and clear
    // `online` so run() returns (poll_impl has no try/catch). Defined in the .cpp.
    template <typename F> void guarded(const char* op, F&& f);

    // Fold the loop() setup-failure blocks: log @p reason and fire stopped once.
    void abort_loop(const char* reason);

    EvConfig config;
    const Feedback feedback;

    // Cleared from the reactor to stop run(): by the session when it finishes, by
    // the setup timeout if discovery/connect never completes, by a connect failure,
    // or by shutdown().
    std::atomic_bool online{false};

    // Set by shutdown(); honored by loop() so a stop issued before loop() runs
    // is not clobbered by loop()'s `online = true`. Makes shutdown() valid at any
    // point in the object lifetime.
    std::atomic_bool stop_requested{false};

    // Bounds the pre-session phase (SDP discovery + TCP connect) only; disarmed
    // once the session starts, after which the session's own response watchdog
    // governs. Single-shot.
    everest::lib::io::event::timer_fd setup_timeout;

    // Re-issues the SDP request on the standard retransmit interval until the SECC
    // responds (on_found) or the setup timeout elapses. Periodic; disarmed on both.
    everest::lib::io::event::timer_fd sdp_retry;

    // Grace-period fallback for request_stop(): registered on the reactor in loop(),
    // armed only from request_stop()'s marshaled action. Hard-stops the loop if the
    // graceful stop walk does not finish in time. Single-shot; disarmed on finish.
    everest::lib::io::event::timer_fd stop_grace_timer;

    // Declaration order is load-bearing: the reactor outlives the clients that
    // register fds with it, and the logger outlives the Session that references
    // it. The data client is created at runtime (in establish_data_path) once the
    // transport security is known, so the Session's outbound lambda dereferences
    // it lazily; both are held by unique_ptr and the Session is constructed last.
    everest::lib::io::event::fd_event_handler reactor;
    session::SessionLogger logger;
    std::optional<transport::SdpClient> sdp_client;
    std::unique_ptr<transport::DataClient> data_client;

    // Module -> FSM DC-params channel. Declared before `session`: the Session's
    // Context holds a reference to it (passed as &dc_params at construction), so it
    // must outlive the Session.
    everest::lib::util::monitor<DcChargeParams> dc_params;

    std::unique_ptr<Session> session;
};

} // namespace iso15118::ev
