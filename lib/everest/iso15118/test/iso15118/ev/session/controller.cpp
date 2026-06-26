// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <stdexcept>
#include <thread>

#include <arpa/inet.h>

#include <everest/io/event/fd_event_handler.hpp>

#include <iso15118/io/ipv6_endpoint.hpp>
#include <iso15118/io/sdp.hpp>

#include <iso15118/ev/config.hpp>
#include <iso15118/ev/controller.hpp>
#include <iso15118/ev/d20/control_event.hpp>
#include <iso15118/ev/session.hpp>
#include <iso15118/ev/session/feedback.hpp>
#include <iso15118/session/logger.hpp>

using namespace iso15118;
using namespace std::chrono_literals;

SCENARIO("ISO15118-20 EV Controller config defaults") {
    GIVEN("A default-constructed EvConfig") {
        ev::EvConfig config{};

        THEN("It defaults to SDP discovery with no transport security") {
            REQUIRE(config.discover);
            REQUIRE(config.advertised_security == io::v2gtp::Security::NO_TRANSPORT_SECURITY);
        }

        THEN("It advertises exactly the single ISO 15118-20 DC app protocol") {
            REQUIRE(config.advertised_app_protocols.size() == 1);
            REQUIRE(config.advertised_app_protocols.front().protocol_namespace == "urn:iso:std:iso:15118:-20:DC");
        }
    }
}

SCENARIO("ISO15118-20 EV Controller shutdown stops the loop") {
    // loop() runs SDP discovery on `lo`; with no SECC responding, the reactor stays
    // in the pre-session phase (SDP retry + setup timeout, both far from elapsing).
    // shutdown() must terminate run() promptly, well before the 18 s setup timeout,
    // and fire the stopped callback.
    //
    // Note: this does not isolate shutdown's add_action wake from the periodic SDP
    // retry timer that also wakes poll() (the wake only bounds the worst-case stop
    // latency, which is not separately observable through loop()). A socket-level
    // walk is deferred to the pump-level FSM-walk test.
    GIVEN("A Controller running SDP discovery with no SECC present") {
        iso15118::session::logging::set_session_log_callback(
            [](std::size_t, const iso15118::session::logging::Event&) {});

        ev::EvConfig config{};
        config.interface_name = "lo";
        config.discover = true;
        config.send_delay = 5ms;
        config.response_timeout = 100ms;

        ev::feedback::Callbacks callbacks{};
        std::atomic_bool stopped{false};
        callbacks.stopped = [&stopped]() { stopped = true; };

        ev::Controller controller{config, callbacks};

        WHEN("loop() is run on a worker thread and shutdown() is called") {
            std::thread worker([&controller]() { controller.loop(); });

            THEN("loop() returns promptly and fires the stopped callback") {
                // No observable signal marks the moment loop() enters reactor.run(), and a
                // shutdown() racing ahead of loop()'s `online = true` would be lost. Rather
                // than sleep a fixed interval and hope, re-issue shutdown() on a short
                // cadence until the worker reports stopped (or a generous deadline elapses).
                // Each shutdown() is an idempotent flag+wake, so repeating it is harmless and
                // robust against the startup ordering race.
                const auto deadline = std::chrono::steady_clock::now() + 5s;
                while (not stopped and std::chrono::steady_clock::now() < deadline) {
                    controller.shutdown();
                    std::this_thread::sleep_for(5ms);
                }
                worker.join();
                REQUIRE(stopped);
            }
        }
    }
}

SCENARIO("ISO15118-20 EV Session on_finished callback throwing does not escape") {
    // The Controller (and any owner) clears its `online` flag from on_finished, which
    // runs on the reactor thread inside check_finished(). A throwing owner callback
    // must be swallowed (logged), fired exactly once, and must not re-enter once the
    // session is already signalled.
    GIVEN("A Session whose on_finished throws on first invocation") {
        iso15118::session::logging::set_session_log_callback(
            [](std::size_t, const iso15118::session::logging::Event&) {});

        everest::lib::io::event::fd_event_handler reactor;

        std::vector<std::vector<uint8_t>> captured;
        int finished_count = 0;

        ev::feedback::Callbacks callbacks{};

        session::SessionLogger logger{nullptr};

        const ev::SessionTiming timing{5ms, 50ms};

        ev::Session session{callbacks,
                            [&captured](std::vector<uint8_t> frame) {
                                captured.push_back(std::move(frame));
                                return true;
                            },
                            logger,
                            reactor,
                            timing,
                            "EVTESTID01"};

        session.set_on_finished([&finished_count]() {
            ++finished_count;
            throw std::runtime_error("consumer on_finished callback failure");
        });

        WHEN("the session finishes (watchdog timeout) and the callback throws") {
            session.start();

            // Pump the reactor through the SAP send and the watchdog expiry. A throw
            // escaping the guard would propagate out of reactor.poll(); the loop below
            // would then see it, so an unguarded callback fails the test by exception.
            const auto deadline = std::chrono::steady_clock::now() + 2s;
            while (not session.is_finished() and std::chrono::steady_clock::now() < deadline) {
                reactor.poll(1ms);
                reactor.run_actions();
            }

            THEN("the throw is swallowed, the callback fired once, and the session is finished") {
                REQUIRE(session.is_finished());
                REQUIRE(finished_count == 1);
            }
        }
    }
}

SCENARIO("ISO15118-20 EV Controller post_control_event marshals onto the reactor") {
    // Smoke assertion (documented): post_control_event must NOT touch the session/pump
    // from the caller's thread. The Controller owns its reactor privately, so the
    // queued action cannot be pumped here without opening a socket (a fixed_endpoint
    // run would connect); the deferred deliver runs in loop(), exercised at the
    // pump-level FSM walk. What this pins: post_control_event is safe to call before
    // the reactor runs and returns without crashing or synchronously mutating the
    // pump (deliver_control_event is a no-op before start() anyway, so any synchronous
    // call would be observable only as a crash on the absent FSM). It does not assert
    // the action ran (that needs the reactor pumped, i.e. a socket).
    GIVEN("A Controller built with discover=false and a fixed endpoint (no SdpClient)") {
        iso15118::session::logging::set_session_log_callback(
            [](std::size_t, const iso15118::session::logging::Event&) {});

        ev::EvConfig config{};
        config.interface_name = "lo";
        config.discover = false;
        config.send_delay = 5ms;
        config.response_timeout = 100ms;

        io::Ipv6EndPoint endpoint{};
        endpoint.port = 15119;
        endpoint.address[0] = htons(0x0001); // ::1-ish; never connected in this test
        config.fixed_endpoint = endpoint;

        ev::feedback::Callbacks callbacks{};
        ev::Controller controller{config, callbacks};

        WHEN("a control event is posted without ever running the loop") {
            THEN("the call marshals (no crash, no synchronous pump mutation) and returns") {
                REQUIRE_NOTHROW(controller.post_control_event(ev::d20::StopCharging{true}));
            }
        }
    }
}

SCENARIO("ISO15118-20 EV Controller loop stops when discover is off and no endpoint is set") {
    // Fully synchronous path: with discover=false and no fixed_endpoint, loop()
    // registers its timers, hits the configuration-error early return, fires stopped,
    // and returns immediately (no socket, no reactor.run). Red->green: without the
    // early return loop() would fall through to reactor.run() and block.
    GIVEN("A Controller with discover=false and no fixed_endpoint") {
        iso15118::session::logging::set_session_log_callback(
            [](std::size_t, const iso15118::session::logging::Event&) {});

        ev::EvConfig config{};
        config.interface_name = "lo";
        config.discover = false;
        // fixed_endpoint deliberately left unset.

        ev::feedback::Callbacks callbacks{};
        bool stopped = false;
        callbacks.stopped = [&stopped]() { stopped = true; };

        ev::Controller controller{config, callbacks};

        WHEN("loop() is called") {
            const auto start = std::chrono::steady_clock::now();
            controller.loop();
            const auto elapsed = std::chrono::steady_clock::now() - start;

            THEN("it returns promptly and fires the stopped callback") {
                REQUIRE(stopped);
                REQUIRE(elapsed < 1s);
            }
        }
    }
}

SCENARIO("ISO15118-20 EV Controller fires stopped exactly once when establish_data_path throws") {
    // Regression guard for the establish_data_path double-fire: a throw inside
    // establish_data_path (here, the connected consumer callback) is caught and only
    // clears `online`; it must NOT also fire stopped from the catch. The single
    // stopped fire is loop()'s tail after run() returns. A second fire would mean the
    // catch re-introduced the double-fire.
    //
    // discover=false + a fixed_endpoint makes loop() call establish_data_path
    // synchronously, and connected() is the first statement in its try-block, so the
    // throw is reached before any socket work: no listener, no connect, fully
    // synchronous (run() returns at once because the catch cleared `online`).
    GIVEN("A Controller with a fixed endpoint whose connected callback throws") {
        iso15118::session::logging::set_session_log_callback(
            [](std::size_t, const iso15118::session::logging::Event&) {});

        ev::EvConfig config{};
        config.interface_name = "lo";
        config.discover = false;
        config.send_delay = 5ms;
        config.response_timeout = 100ms;

        io::Ipv6EndPoint endpoint{};
        endpoint.port = 15119;
        endpoint.address[0] = htons(0x0001); // ::1-ish; never connected (connected() throws first)
        config.fixed_endpoint = endpoint;

        int connected_count = 0;
        int stopped_count = 0;

        ev::feedback::Callbacks callbacks{};
        callbacks.connected = [&connected_count](const io::Ipv6EndPoint&) {
            ++connected_count;
            throw std::runtime_error("connected consumer failure");
        };
        callbacks.stopped = [&stopped_count]() { ++stopped_count; };

        ev::Controller controller{config, callbacks};

        WHEN("loop() runs and the connected callback throws inside establish_data_path") {
            const auto start = std::chrono::steady_clock::now();
            controller.loop();
            const auto elapsed = std::chrono::steady_clock::now() - start;

            THEN("connected fired once, the throw was caught, and stopped fired exactly once") {
                REQUIRE(connected_count == 1);
                REQUIRE(stopped_count == 1);
                REQUIRE(elapsed < 1s);
            }
        }
    }
}
