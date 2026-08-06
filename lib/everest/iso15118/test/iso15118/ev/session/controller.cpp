// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <stdexcept>
#include <thread>

#include <iso15118/io/sdp.hpp>

#include <iso15118/ev/config.hpp>
#include <iso15118/ev/controller.hpp>
#include <iso15118/ev/session.hpp>
#include <iso15118/ev/session/feedback.hpp>

#include "test_support.hpp"

using namespace iso15118;
using namespace std::chrono_literals;
using iso15118::ev::test::ControllerRun;
using iso15118::ev::test::poll_until;

SCENARIO("ISO15118-20 EV Controller config defaults") {
    GIVEN("A default-constructed EvConfig") {
        ev::EvConfig config{};

        THEN("It defaults to no transport security") {
            REQUIRE(config.advertised_security == io::v2gtp::Security::NO_TRANSPORT_SECURITY);
        }

        THEN("It paces re-poll sends with a non-zero default send delay") {
            REQUIRE(config.send_delay > std::chrono::milliseconds{0});
        }

        THEN("It advertises exactly the single ISO 15118-20 DC app protocol") {
            REQUIRE(config.advertised_app_protocols.size() == 1);
            REQUIRE(config.advertised_app_protocols.front().protocol_namespace == "urn:iso:std:iso:15118:-20:DC");
        }
    }
}

SCENARIO("ISO15118-20 EV Controller shutdown stops the loop") {
    // No SECC responding keeps the reactor in the pre-session phase, far from the
    // 18 s setup timeout, so an early stop can only be shutdown() itself. Does not
    // isolate that from the periodic SDP retry timer also waking poll(); a
    // socket-level walk is deferred to the Session-level FSM-walk test.
    GIVEN("A Controller running SDP discovery with no SECC present") {
        ev::EvConfig config{};
        config.interface_name = "lo";
        config.send_delay = 5ms;
        config.response_timeout = 100ms;

        ev::feedback::Callbacks callbacks{};
        std::atomic_bool stopped{false};
        callbacks.stopped = [&stopped]() { stopped = true; };

        ev::Controller controller{config, callbacks};

        WHEN("loop() is run on a worker thread and shutdown() is called") {
            std::thread worker([&controller]() { controller.loop(); });

            THEN("loop() returns promptly and fires the stopped callback") {
                // A shutdown() racing ahead of `online = true` would be lost, so re-issue
                // it (idempotent flag+wake) until stopped or a deadline elapses.
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
    // on_finished runs on the reactor thread inside check_finished(); a throwing
    // owner callback must be swallowed (logged), not propagate, and fire only once.
    GIVEN("A Session whose on_finished throws on first invocation") {
        ev::test::SessionFixture fx{"EVTESTID01", ev::SessionTiming{5ms, 50ms}};

        int finished_count = 0;
        fx.session.set_on_finished([&finished_count]() {
            ++finished_count;
            throw std::runtime_error("consumer on_finished callback failure");
        });

        WHEN("the session finishes (watchdog timeout) and the callback throws") {
            fx.session.start();

            // A throw escaping the guard would propagate out of reactor.poll() and fail
            // this test by exception.
            const auto deadline = std::chrono::steady_clock::now() + 2s;
            while (not fx.session.is_finished() and std::chrono::steady_clock::now() < deadline) {
                fx.reactor.poll(1ms);
                fx.reactor.run_actions();
            }

            THEN("the throw is swallowed, the callback fired once, and the session is finished") {
                REQUIRE(fx.session.is_finished());
                REQUIRE(finished_count == 1);
            }
        }
    }
}

SCENARIO("ISO15118-20 EV Controller request_stop marshals onto the reactor before the loop runs") {
    // request_stop must be safe to call off the reactor thread before loop() runs:
    // it only queues an action, never touching session/timer state synchronously.
    GIVEN("A Controller that has never run its loop") {
        ev::EvConfig config{};
        config.interface_name = "lo";
        config.send_delay = 5ms;
        config.response_timeout = 100ms;

        ev::feedback::Callbacks callbacks{};
        ev::Controller controller{config, callbacks};

        WHEN("request_stop is called without ever running the loop") {
            THEN("the call marshals (no crash, no synchronous session mutation) and returns") {
                REQUIRE_NOTHROW(controller.request_stop());
            }
        }
    }
}

SCENARIO("ISO15118-20 EV Controller request_stop grace fallback hard-stops a stuck session") {
    // With no SECC responding the session never reaches the FSM, so StopCharging has
    // nothing to walk gracefully; request_stop's grace fallback (3x response_timeout)
    // must hard-stop instead.
    GIVEN("A Controller running SDP discovery with no SECC present") {
        ev::EvConfig config{};
        config.interface_name = "lo";
        config.send_delay = 5ms;
        config.response_timeout = 50ms; // 3x = 150ms grace, well under the deadline

        ev::feedback::Callbacks callbacks{};
        std::atomic_int stopped_count{0};
        callbacks.stopped = [&stopped_count]() { ++stopped_count; };

        ev::Controller controller{config, callbacks};

        WHEN("loop() is run on a worker thread and request_stop() is called once") {
            std::thread worker([&controller]() { controller.loop(); });

            THEN("the grace fallback hard-stops the loop and fires stopped exactly once") {
                // Request the graceful stop exactly once: re-issuing would re-arm the
                // single-shot grace timer and defer the fallback indefinitely.
                std::this_thread::sleep_for(50ms);
                controller.request_stop();

                const auto deadline = std::chrono::steady_clock::now() + 5s;
                while (stopped_count == 0 and std::chrono::steady_clock::now() < deadline) {
                    std::this_thread::sleep_for(5ms);
                }
                worker.join();
                REQUIRE(stopped_count == 1);
            }
        }
    }
}

SCENARIO("ISO15118-20 EV Controller loop releases its reactor timers before returning") {
    // loop() registers three timers on the reactor it owns; epoll rejects
    // EPOLL_CTL_ADD on an fd it already holds, so leaving them registered would
    // abort the NEXT loop() synchronously on its first register_event_handler.
    // Registration isn't observable directly, so this pins the consequence: the
    // second run must still be alive 50 ms in. Uses request_stop, not shutdown,
    // since shutdown latches stop_requested and the second loop() would
    // early-return on it.
    GIVEN("A Controller whose loop is run twice, ended by request_stop each time") {
        ev::EvConfig config{};
        config.interface_name = "lo";
        config.send_delay = 5ms;
        config.response_timeout = 50ms; // 3x = 150 ms grace

        ev::feedback::Callbacks callbacks{};
        std::atomic_int stopped_count{0};
        callbacks.stopped = [&stopped_count]() { ++stopped_count; };

        ev::Controller controller{config, callbacks};

        // Runs loop() once, returning whether it was still alive just before the
        // graceful stop was requested.
        const auto run_and_stop = [&controller, &stopped_count]() {
            const auto before = stopped_count.load();
            std::thread worker([&controller]() { controller.loop(); });
            std::this_thread::sleep_for(50ms);
            const auto still_running = stopped_count.load();
            controller.request_stop();

            const auto deadline = std::chrono::steady_clock::now() + 5s;
            while (stopped_count == before and std::chrono::steady_clock::now() < deadline) {
                std::this_thread::sleep_for(5ms);
            }
            worker.join();
            return still_running;
        };

        WHEN("loop() runs, stops gracefully, and runs again") {
            const auto alive_during_first = run_and_stop();
            const auto alive_during_second = run_and_stop();

            THEN("both runs stayed alive past registration and each fired stopped once") {
                REQUIRE(alive_during_first == 0);
                REQUIRE(alive_during_second == 1);
                REQUIRE(stopped_count == 2);
            }
        }
    }
}

SCENARIO("ISO15118-20 EV Controller aborts the pre-session phase on the setup timeout") {
    // The setup timeout is the only bound on a discovery/connect that never
    // completes; every other scenario keeps it far from elapsing, so this is the one
    // place that actually pins it firing. Fixed 18 s production constant, no
    // injection seam, so this scenario pays the wall time.
    GIVEN("A Controller running SDP discovery on lo with no SECC present") {
        ev::EvConfig config{};
        config.interface_name = "lo";
        config.send_delay = 5ms;
        config.response_timeout = 100ms;

        ev::feedback::Callbacks callbacks{};
        std::atomic_int stopped_count{0};
        std::atomic_int connected_count{0};
        callbacks.stopped = [&stopped_count]() { ++stopped_count; };
        callbacks.connected = [&connected_count](const io::Ipv6EndPoint&) { ++connected_count; };

        ev::Controller controller{config, callbacks};

        WHEN("loop() runs untouched: neither shutdown() nor request_stop() is called") {
            ControllerRun run{controller};

            THEN("the setup timeout ends the run and fires stopped exactly once") {
                // The grace timer is never armed, so nothing but the setup timeout can
                // end this run; it must still be alive five seconds in.
                const auto alive_at_five_seconds = not poll_until([&]() { return stopped_count > 0; }, 5s);

                const auto ended = poll_until([&]() { return stopped_count > 0; }, 20s);

                REQUIRE(alive_at_five_seconds);
                REQUIRE(ended);
                REQUIRE(stopped_count == 1);
                // No SECC answered, so the data path was never established.
                REQUIRE(connected_count == 0);
            }
        }
    }
}
