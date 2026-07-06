// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <iso15118/ev/controller.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <utility>

#include <everest/io/event/timer_fd.hpp>

#include <iso15118/detail/helper.hpp>
#include <iso15118/detail/io/socket_helper.hpp>

namespace iso15118::ev {

namespace {

// Bounds the pre-session phase (SDP discovery + TCP connect). Once the session
// starts this is disarmed and the session's per-request response watchdog governs.
// Reconciled with the SECC's V2G_COMMUNICATION_SETUP_TIMEOUT_MS{18000} (the V2G
// communication setup timeout the EVSE applies): use the same 18000 ms here so
// both ends bound the pre-session phase identically.
constexpr auto SETUP_TIMEOUT = std::chrono::milliseconds(18000);

// SDP request retransmit interval: the EV resends the discovery request on this
// cadence until a response arrives or the setup timeout elapses.
constexpr auto SDP_RETRY_INTERVAL = std::chrono::milliseconds(250);

// Per-session identifier handed to the SessionLogger. The logger only uses it as
// an opaque tag (cast to std::uintptr_t), so a monotonic counter cast to void*
// gives each Controller a distinct, stable id without dangling at any address.
std::uintptr_t next_session_log_id() {
    static std::atomic<std::uintptr_t> counter{1};
    return counter.fetch_add(1);
}

} // namespace

Controller::Controller(EvConfig config_, feedback::Callbacks callbacks_, DcChargeParams initial_dc_params,
                       AcChargeParams initial_ac_params) :
    config(std::move(config_)),
    feedback(callbacks_),
    logger(reinterpret_cast<void*>(next_session_log_id())),
    dc_params(std::move(initial_dc_params)),
    ac_params(std::move(initial_ac_params)) {

    // Resolve the egress interface up front; an unusable interface is fatal.
    if (not io::check_and_update_interface(config.interface_name)) {
        throw std::runtime_error("Ethernet interface was not found: " + config.interface_name);
    }

    // The SDP client is only needed when discovering the endpoint.
    if (config.discover) {
        sdp_client.emplace(config.interface_name, config.advertised_security);
    }

    // Wire the Session's outbound seam. The data client does not exist yet (it is
    // created in establish_data_path once the transport security is known), so the
    // lambda dereferences it lazily; by the time the first frame is sent (from
    // session->start(), invoked on connect) the client is in place. The Session
    // keeps its own copy of the callbacks (for v2g_message); the Controller wraps a
    // separate copy in its own Feedback for connected / stopped.
    session = std::make_unique<Session>(
        callbacks_,
        [this](std::vector<uint8_t> frame) {
            // Report the send result so the Session can stop loudly. Both failure
            // modes (a missing data client for a frame produced before the connect
            // chain ran, and a refused transmit) return false; either would otherwise
            // only show up as a downstream watchdog timeout.
            if (not data_client) {
                logf_error("EV Controller: outbound frame before data client exists; dropping");
                return false;
            }
            if (not data_client->send(frame)) {
                logf_error("EV Controller: data client failed to send outbound frame");
                return false;
            }
            return true;
        },
        logger, reactor, SessionTiming{config.send_delay, config.response_timeout}, config.evcc_id,
        config.advertised_app_protocols, &dc_params, &ac_params, config.energy_service, config.der_control_functions,
        config.der_stop_on_unsupported_functions);

    // The session can finish inside a timer callback (the response watchdog), so
    // the run loop can't poll is_finished() between events; let the session clear
    // `online` directly when it ends. Disarm the request_stop grace fallback on the
    // same seam so a graceful finish leaves no stale timer armed.
    session->set_on_finished([this]() {
        online = false;
        stop_grace_timer.disarm();
    });
}

template <typename F> void Controller::guarded(const char* op, F&& f) {
    // The reactor's poll_impl has no try/catch, so an escaping throw would kill the
    // reactor thread and leave `online` set, hanging the loop with no stopped signal.
    // Clear `online` on any throw so run() returns; loop() fires stopped once after.
    try {
        f();
    } catch (const std::exception& e) {
        logf_error("EV Controller: %s failed (%s); stopping", op, e.what());
        online = false;
    } catch (...) {
        logf_error("EV Controller: %s failed (non-std exception); stopping", op);
        online = false;
    }
}

void Controller::abort_loop(const char* reason) {
    logf_error("EV Controller: %s; aborting", reason);
    feedback.stopped();
}

void Controller::establish_data_path(const iso15118::io::Ipv6EndPoint& endpoint,
                                     iso15118::io::v2gtp::Security security) {
    // The SDP rx handler fires on-found for every parseable response, and UDP can
    // duplicate or retransmit the SECC reply. Guard on the data client: a second
    // call must not recreate and reconnect it, tearing down an in-flight handshake.
    if (data_client) {
        logf_warning("EV Controller: ignoring additional SDP response; data path already established");
        return;
    }

    // This runs from the SDP on_found rx callback through the reactor's poll_impl,
    // which has no try/catch. A throw here (make_unique bad_alloc, or the
    // callbacks.connected consumer callback throwing) would kill the reactor thread
    // and leave `online` set, hanging the session with no stopped/timed_out signal.
    // guarded() clears `online` so run() returns; loop() fires stopped once after.
    guarded("establishing the data path", [&]() {
        feedback.connected(endpoint);

        // The advertised security only signals that the SECC is *capable* of TLS; it
        // is not a requirement, and the EV requests plain TCP in its SDP request. Until
        // libio gains a TLS client the EV always connects over plain TCP. This is the
        // seam where a future TLS client would be created for a TLS-capable endpoint.
        if (security == iso15118::io::v2gtp::Security::TLS) {
            logf_info(
                "EV Controller: SECC advertised TLS capability; connecting over plain TCP (TLS not yet supported)");
        }
        data_client = std::make_unique<transport::DataClient>(reactor);

        data_client->on_rx([this](const std::vector<uint8_t>& bytes) { session->on_bytes_received(bytes); });

        data_client->connect(
            endpoint, config.interface_name,
            [this]() {
                // The pre-session phase is complete; hand timeout duty to the session's
                // own response watchdog and stop retransmitting SDP requests.
                setup_timeout.disarm();
                sdp_retry.disarm();
                session->start();
            },
            [this]() {
                // Single-session: a connect/socket failure ends the attempt. Log and
                // clear `online` so run() returns; reconnect is deferred.
                logf_error("EV Controller: data client connect failed; stopping");
                online = false;
            });
    });
}

void Controller::loop() {
    online = true;

    // Honor a stop requested before loop() ran.
    if (stop_requested) {
        online = false;
        feedback.stopped();
        return;
    }

    // Timeout as a reactor event: a single-shot timer clears `online` so run()
    // returns even if no socket traffic ever arrives. Its fd wakes the poll, so
    // the loop exits on the same iteration the timer fires.
    setup_timeout.set_single_shot(true);
    if (not reactor.register_event_handler(&setup_timeout, [this]() {
            logf_warning("EV Controller: SDP discovery / connect did not complete in time");
            sdp_retry.disarm();
            online = false;
        })) {
        abort_loop("failed to register the setup timeout timer");
        return;
    }
    if (not setup_timeout.set_timeout(SETUP_TIMEOUT)) {
        // The setup timeout bounds the whole pre-session phase; without it a stalled
        // discovery/connect would never time out. Treat a failed arm as fatal.
        abort_loop("failed to arm the setup timeout timer");
        return;
    }

    // Register the request_stop grace fallback here, on the reactor thread, before
    // run(). request_stop() (called from the module command thread) only ARMS it from
    // a marshaled action, keeping all reactor state on the reactor thread. Single-shot.
    stop_grace_timer.set_single_shot(true);
    if (not reactor.register_event_handler(&stop_grace_timer, [this]() {
            logf_warning("EV Controller: graceful stop did not finish in time; hard-stopping");
            online = false;
        })) {
        abort_loop("failed to register the stop-grace timer");
        return;
    }

    // Resolve the SECC endpoint and kick off the async connect chain; the single
    // reactor.run() below drives discovery -> connect -> session start.
    if (config.discover) {
        if (not sdp_client->register_events(reactor)) {
            abort_loop("failed to register the SDP client");
            return;
        }

        // Periodic SDP retransmit: a UDP request can be lost, so re-issue it on the
        // standard interval until the SECC responds (establish_data_path disarms it)
        // or the setup timeout elapses (its handler disarms it).
        sdp_retry.set_single_shot(false);
        if (not reactor.register_event_handler(&sdp_retry, [this]() {
                if (sdp_client) {
                    sdp_client->send_request();
                }
            })) {
            abort_loop("failed to register the SDP retry timer");
            return;
        }
        if (not sdp_retry.set_timeout(SDP_RETRY_INTERVAL)) {
            // A failed arm silently disables SDP retransmit, leaving discovery to hang
            // on a single lost request until the setup timeout. Treat it as fatal.
            abort_loop("failed to arm the SDP retry timer");
            return;
        }

        // The SDP response carries the SECC endpoint AND the transport security;
        // both drive the runtime data-client creation in establish_data_path. The
        // SECC has answered, so stop retransmitting the discovery request.
        sdp_client->discover([this](transport::SdpResponse response) {
            sdp_retry.disarm();
            establish_data_path(response.endpoint, response.security);
        });
    } else {
        if (not config.fixed_endpoint.has_value()) {
            abort_loop("discover is disabled but no fixed_endpoint configured");
            return;
        }
        // A fixed endpoint skips the SDP exchange; use the configured security.
        establish_data_path(*config.fixed_endpoint, config.advertised_security);
    }

    reactor.run(online);

    if (session and not session->is_finished()) {
        logf_warning("EV Controller: session did not finish before the deadline");
    }

    feedback.stopped();
}

void Controller::post_control_event(d20::ControlEvent event) {
    // Marshal onto the reactor thread: never touch session state from the
    // caller's thread. The action runs inside reactor.run_actions().
    reactor.add_action([this, event]() {
        if (session) {
            session->deliver_control_event(event);
        }
    });
}

void Controller::request_stop() {
    // Marshal onto the reactor thread: never touch session or timer state from the
    // caller's thread. The action runs inside reactor.run_actions().
    reactor.add_action([this]() {
        if (session) {
            session->deliver_control_event(d20::ControlEvent{d20::StopCharging{true}});
        }
        // Grace fallback: the graceful walk is PowerDelivery(Stop) ->
        // DC_WeldingDetection -> SessionStop, three response round trips worst case, so
        // bound the wait at 3x the response timeout before hard-stopping the loop.
        if (not stop_grace_timer.set_timeout(3 * config.response_timeout)) {
            // A failed arm removes the only bound on a stalled graceful stop. Fall back
            // to the hard-stop this timer exists to provide rather than risk a hang.
            logf_error("EV Controller: failed to arm the stop-grace timer; hard-stopping");
            online = false;
        }
    });
}

void Controller::shutdown() {
    // Flag + wake: run() may be blocked in poll(); the empty action wakes it so it
    // re-checks `online`. stop_requested covers a shutdown() racing ahead of loop().
    stop_requested = true;
    online = false;
    reactor.add_action([]() {});
}

void Controller::update_present_soc(double present_soc) {
    auto h = dc_params.handle();
    (*h).present_soc = present_soc;
}

void Controller::update_present_voltage(float present_voltage) {
    auto h = dc_params.handle();
    (*h).present_voltage = present_voltage;
}

void Controller::update_present_active_power(float present_active_power) {
    auto h = ac_params.handle();
    (*h).present_active_power = present_active_power;
}

} // namespace iso15118::ev
