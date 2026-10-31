// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_exception.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <stdexcept>
#include <thread>

#include <iso15118/v2gtp/sdp.hpp>

#include <iso15118/ev/config/config.hpp>
#include <iso15118/ev/controller.hpp>
#include <iso15118/ev/detail/session/session.hpp>
#include <iso15118/ev/detail/session_options.hpp>
#include <iso15118/ev/feedback.hpp>

#include "test_support.hpp"

using namespace iso15118;
using namespace std::chrono_literals;
using iso15118::ev::test::ControllerRun;
using iso15118::ev::test::poll_until;

SCENARIO("ISO15118-20 EV Controller config defaults") {
    GIVEN("A default-constructed EvConfig") {
        ev::config::EvConfig config{};

        THEN("It defaults to no transport security") {
            REQUIRE(config.sdp_security() == v2gtp::Security::NO_TRANSPORT_SECURITY);
        }

        THEN("It paces re-poll sends with a non-zero default send delay") {
            REQUIRE(config.send_delay > std::chrono::milliseconds{0});
        }

        THEN("It offers ISO 15118-20 only and derives the SAP list") {
            REQUIRE(config.supported_protocols == std::vector<ProtocolId>{ProtocolId::ISO15118_20});
            REQUIRE(config.advertised_app_protocols.empty());
            REQUIRE(config.response_timeout == std::chrono::milliseconds{0});
        }
    }
}

SCENARIO("ISO15118-20 EV Controller copies the AC_DER_SAE config into the session options") {
    GIVEN("An EvConfig with non-default SAE settings") {
        ev::config::EvConfig config{};
        config.d20.sae_profile.inverter_serial_number = "SN-TEST";
        config.d20.sae_profile.supported_modes = 0x40U;
        config.d20.cpd_rounds = 4;
        config.d20.der_stop_on_invalid_control = true;

        WHEN("the session options are built from it") {
            const auto options = ev::make_session_options(config, {});

            THEN("the three SAE fields arrive unchanged") {
                REQUIRE(options.d20.sae_profile.inverter_serial_number == "SN-TEST");
                REQUIRE(options.d20.sae_profile.supported_modes == 0x40U);
                REQUIRE(options.d20.cpd_rounds == 4);
                REQUIRE(options.d20.der_stop_on_invalid_control == true);
            }
        }
    }
}

SCENARIO("ISO15118-20 EV Controller copies start_charging's energy request into the session options") {
    ev::config::EvConfig config{};
    const auto departure = std::chrono::steady_clock::now() + std::chrono::hours(1);
    config.params.energy_request.energy_wh = 20000.0f;
    config.params.energy_request.departure = departure;

    const auto options = ev::make_session_options(config, {});

    REQUIRE(options.energy_request.energy_wh == 20000.0f);
    REQUIRE(options.energy_request.departure == departure);
}

SCENARIO("ISO15118-20 EV Controller resumes a paused session in its SECC time and selection") {
    GIVEN("An EvConfig resuming a paused -20 session") {
        ev::d20::SeccClock::State clock{};
        clock.reference = ev::d20::SeccClock::Reference{5'000'000, std::chrono::steady_clock::now()};
        clock.last_stamp = 5'000'100;

        ev::config::EvConfig config{};
        config.resume =
            ev::config::PausedSession{{0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08},
                                      ProtocolId::ISO15118_20,
                                      v2gtp::Security::NO_TRANSPORT_SECURITY,
                                      clock,
                                      ev::d20::SelectedService{message_20::datatypes::ServiceCategory::AC,
                                                               message_20::datatypes::ControlMode::Scheduled,
                                                               message_20::datatypes::AcConnector::ThreePhase}};

        WHEN("the session options are built from it") {
            const auto options = ev::make_session_options(config, {});
            REQUIRE(options.resume.has_value());

            THEN("the clock state arrives unchanged") {
                REQUIRE(options.resume->secc_clock.reference.has_value());
                REQUIRE(options.resume->secc_clock.reference->secc_time == 5'000'000);
                REQUIRE(options.resume->secc_clock.reference->taken_at == clock.reference->taken_at);
                REQUIRE(options.resume->secc_clock.last_stamp == 5'000'100);
            }

            THEN("the retained service selection arrives unchanged") {
                REQUIRE(options.resume->selected_service.has_value());
                REQUIRE(options.resume->selected_service->service == message_20::datatypes::ServiceCategory::AC);
                REQUIRE(options.resume->selected_service->control_mode ==
                        message_20::datatypes::ControlMode::Scheduled);
                REQUIRE(options.resume->selected_service->ac_connector ==
                        message_20::datatypes::AcConnector::ThreePhase);
            }

            THEN("the paused session id arrives unchanged") {
                REQUIRE(options.resume->session_id ==
                        std::array<uint8_t, 8>{0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08});
            }
        }
    }
}

SCENARIO("ISO15118-20 EV Controller shutdown stops the loop") {
    // No SECC responding keeps the reactor in the pre-session phase, far from the
    // 18 s setup timeout, so an early stop can only be shutdown() itself. Does not
    // isolate that from the periodic SDP retry timer also waking poll(); a
    // socket-level walk is deferred to the Session-level FSM-walk test.
    GIVEN("A Controller running SDP discovery with no SECC present") {
        ev::config::EvConfig config{};
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
        ev::test::SessionFixture fx{"EVTESTID01", ev::session::SessionTiming{5ms, 50ms}};

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
        ev::config::EvConfig config{};
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

SCENARIO("ISO15118-20 EV Controller stop grace is never shorter than the graceful stop walk") {
    // A short response_timeout must not shrink the grace below the worst-case
    // PowerDelivery(Stop) -> DC_WeldingDetection -> SessionStop walk, which takes far longer than
    // 3x the per-message timeout. Pinned via the consequence: the loop is still alive well past
    // 3x response_timeout after request_stop().
    GIVEN("A Controller with a 50 ms response timeout and no SECC present") {
        ev::config::EvConfig config{};
        config.interface_name = "lo";
        config.send_delay = 5ms;
        config.response_timeout = 50ms; // 3x = 150 ms, far below the stop walk

        ev::feedback::Callbacks callbacks{};
        std::atomic_int stopped_count{0};
        callbacks.stopped = [&stopped_count]() { ++stopped_count; };

        ev::Controller controller{config, callbacks};

        WHEN("loop() is run on a worker thread and request_stop() is called once") {
            std::thread worker([&controller]() { controller.loop(); });

            THEN("the loop outlives 3x the response timeout and terminate() then ends it once") {
                std::this_thread::sleep_for(50ms);
                controller.request_stop();

                std::this_thread::sleep_for(500ms);
                const auto alive_past_the_old_grace = stopped_count.load();

                controller.terminate();
                const auto deadline = std::chrono::steady_clock::now() + 5s;
                while (stopped_count == 0 and std::chrono::steady_clock::now() < deadline) {
                    std::this_thread::sleep_for(5ms);
                }
                worker.join();

                REQUIRE(alive_past_the_old_grace == 0);
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
    // second run must still be alive 50 ms in. Uses terminate, not shutdown, since
    // shutdown latches stop_requested and the second loop() would early-return on it.
    GIVEN("A Controller whose loop is run twice, ended by request_stop each time") {
        ev::config::EvConfig config{};
        config.interface_name = "lo";
        config.send_delay = 5ms;
        config.response_timeout = 50ms; // 3x = 150 ms grace

        ev::feedback::Callbacks callbacks{};
        std::atomic_int stopped_count{0};
        callbacks.stopped = [&stopped_count]() { ++stopped_count; };

        ev::Controller controller{config, callbacks};

        // Runs loop() once, returning whether it was still alive just before the
        // teardown was requested.
        const auto run_and_stop = [&controller, &stopped_count]() {
            const auto before = stopped_count.load();
            std::thread worker([&controller]() { controller.loop(); });
            std::this_thread::sleep_for(50ms);
            const auto still_running = stopped_count.load();
            controller.terminate();

            const auto deadline = std::chrono::steady_clock::now() + 5s;
            while (stopped_count == before and std::chrono::steady_clock::now() < deadline) {
                std::this_thread::sleep_for(5ms);
            }
            worker.join();
            return still_running;
        };

        WHEN("loop() runs, is terminated, and runs again") {
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
        ev::config::EvConfig config{};
        config.interface_name = "lo";
        config.send_delay = 5ms;
        config.response_timeout = 100ms;

        ev::feedback::Callbacks callbacks{};
        std::atomic_int stopped_count{0};
        std::atomic_int connected_count{0};
        std::atomic_int dlink_error_count{0};
        callbacks.stopped = [&stopped_count]() { ++stopped_count; };
        callbacks.connected = [&connected_count](const io::Ipv6EndPoint&) { ++connected_count; };
        callbacks.signal = [&dlink_error_count](ev::feedback::Signal signal) {
            if (signal == ev::feedback::Signal::DLINK_ERROR) {
                ++dlink_error_count;
            }
        };

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
                // A loop that ends without a session reports DLINK_ERROR exactly once.
                REQUIRE(dlink_error_count == 1);
            }
        }
    }
}

SCENARIO("ISO15118-20 EV Controller reports no paused session for a session that never paused") {
    GIVEN("A Controller whose loop never ran") {
        ev::config::EvConfig config{};
        config.interface_name = "lo";
        config.send_delay = 5ms;

        ev::feedback::Callbacks callbacks{};
        ev::Controller controller{config, callbacks};

        THEN("paused_session() is empty") {
            REQUIRE_FALSE(controller.paused_session().has_value());
        }
    }
}

SCENARIO("EV Controller rejects a missing explicit interface before connection setup") {
    ev::config::EvConfig config{};
    config.interface_name = "missing-ev-interface";

    SECTION("SDP discovery enabled") {
        config.enable_sdp = true;
    }
    SECTION("Direct endpoint configured") {
        config.enable_sdp = false;
        config.direct_secc_endpoint = io::Ipv6EndPoint{1, {}};
    }

    REQUIRE_THROWS_MATCHES((ev::Controller{config, ev::feedback::Callbacks{}}), std::runtime_error,
                           Catch::Matchers::Message("Ethernet interface was not found: missing-ev-interface"));
}

SCENARIO("EV stop grace budgets configured welding and response bounds") {
    ev::config::EvConfig config;
    config.welding_detection.timeout = 70s;
    config.response_timeout = 50ms;
    REQUIRE(ev::stop_grace(config) >= 70s + 2s + 5s + 2s + 2s + 400ms);
    config.welding_detection.timeout = 120s;
    config.response_timeout = 30s;
    config.send_delay = 2s;
    REQUIRE(ev::stop_grace(config) >= 278s);
}

SCENARIO("EV stop grace covers the -2 PowerDelivery bound at a long welding timeout") {
    ev::config::EvConfig config;
    config.welding_detection.timeout = 60s;
    // Charge-loop response, PowerDelivery(Stop) at its 5 s -2 bound, the outstanding welding response,
    // -20 Finished and SessionStop at 2 s each, and four sends.
    REQUIRE(ev::stop_grace(config) >= 60s + 500ms + 5s + 3 * 2s + 4 * 100ms);
}
