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
    // loop() runs SDP discovery on `lo`; with no SECC responding, the reactor stays
    // in the pre-session phase (SDP retry + setup timeout, both far from elapsing).
    // shutdown() must terminate run() promptly, well before the 18 s setup timeout,
    // and fire the stopped callback.
    //
    // Note: this does not isolate shutdown's add_action wake from the periodic SDP
    // retry timer that also wakes poll() (the wake only bounds the worst-case stop
    // latency, which is not separately observable through loop()). A socket-level
    // walk is deferred to the Session-level FSM-walk test.
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
                // Nothing marks the moment loop() enters reactor.run(), and a shutdown()
                // racing ahead of `online = true` would be lost, so re-issue it on a short
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
        ev::test::SessionFixture fx{"EVTESTID01", ev::SessionTiming{5ms, 50ms}};

        int finished_count = 0;
        fx.session.set_on_finished([&finished_count]() {
            ++finished_count;
            throw std::runtime_error("consumer on_finished callback failure");
        });

        WHEN("the session finishes (watchdog timeout) and the callback throws") {
            fx.session.start();

            // Run the reactor through the SAP send and the watchdog expiry. A throw
            // escaping the guard would propagate out of reactor.poll(); the loop below
            // would then see it, so an unguarded callback fails the test by exception.
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
    // it only queues an action (deliver StopCharging + arm the grace timer), never
    // touching session or timer state synchronously. The action itself runs in loop();
    // that deferred deliver is exercised by the grace-fallback scenario below and by
    // the Session-level FSM walk.
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
    // With no SECC responding, the session never reaches the FSM, so a StopCharging
    // control event has nothing to walk gracefully. request_stop arms a grace-period
    // fallback (3x response_timeout) that must hard-stop the loop and fire stopped
    // exactly once, well before the 18 s setup timeout.
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
                // Give loop() a moment to enter reactor.run() and register the grace
                // timer, then request a graceful stop exactly once. Re-issuing would
                // re-arm the single-shot timer and defer the fallback indefinitely.
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
    // loop() registers three timers (setup timeout, SDP retry, stop grace) on the
    // reactor it owns; epoll rejects EPOLL_CTL_ADD on an fd it already holds. Leaving
    // them registered therefore breaks the NEXT loop(): its very first
    // register_event_handler fails and it aborts synchronously, long before the grace
    // period it would otherwise wait out.
    //
    // Registration is not observable from outside the Controller, so this pins the
    // consequence: 50 ms into the second run (the abort path is synchronous and
    // sub-millisecond, while the only things that can end a healthy run are the not-yet
    // armed grace timer and the 18 s setup timeout) the second run must still be
    // running. request_stop, not shutdown, ends each run: shutdown latches
    // stop_requested and the second loop() would early-return on it.
    GIVEN("A Controller whose loop is run twice, ended by request_stop each time") {
        ev::EvConfig config{};
        config.interface_name = "lo";
        config.send_delay = 5ms;
        config.response_timeout = 50ms; // 3x = 150 ms grace

        ev::feedback::Callbacks callbacks{};
        std::atomic_int stopped_count{0};
        callbacks.stopped = [&stopped_count]() { ++stopped_count; };

        ev::Controller controller{config, callbacks};

        // Runs loop() to completion, returning the stopped count observed just before
        // the graceful stop was requested (i.e. whether the run was still alive then).
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
