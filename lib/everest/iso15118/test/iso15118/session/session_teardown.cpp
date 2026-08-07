// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <chrono>
#include <memory>
#include <optional>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <iso15118/d20/config.hpp>
#include <iso15118/d20/context.hpp>
#include <iso15118/session/feedback.hpp>
#include <iso15118/session/iso.hpp>

#include "mock_connection.hpp"

using iso15118::test::MockConnection;

namespace {

// Captured SupportedAppProtocolReq offering the -20:AC namespace.
constexpr uint8_t sap_req[] = {0x80, 0x00, 0xf3, 0xab, 0x93, 0x71, 0xd3, 0x4b, 0x9b, 0x79, 0xd3, 0x9b, 0xa3,
                               0x21, 0xd3, 0x4b, 0x9b, 0x79, 0xd1, 0x89, 0xa9, 0x89, 0x89, 0xc1, 0xd1, 0x69,
                               0x91, 0x81, 0xd2, 0x0a, 0x18, 0x01, 0x00, 0x00, 0x04, 0x00, 0x40};

// Captured SessionSetupReq with a zeroed session id (starts a new session).
constexpr uint8_t session_setup_req[] = {0x80, 0x8c, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0c, 0x9f,
                                         0x9c, 0x2b, 0xd0, 0x62, 0x0b, 0x2b, 0xa6, 0xa4, 0xab, 0x18, 0x99, 0x19, 0x9a,
                                         0x1a, 0x9b, 0x1b, 0x9c, 0x1c, 0x98, 0x20, 0xa1, 0x21, 0xa2, 0x22, 0xac, 0x00};

} // namespace

SCENARIO("Session teardown primitives") {
    iso15118::session::feedback::Callbacks callbacks;
    callbacks.signal = [](auto) {};

    const iso15118::session::SessionConfig session_config{iso15118::session::EvseSetupConfig{}};
    std::optional<iso15118::d20::PauseContext> pause_ctx{std::nullopt};
    std::optional<iso15118::d2::PauseContext> d2_pause_ctx{std::nullopt};

    auto connection = std::make_unique<MockConnection>();
    auto* conn = connection.get();

    iso15118::Session session{std::move(connection), session_config, callbacks, pause_ctx, d2_pause_ctx};

    GIVEN("a never-connected session") {
        WHEN("close() is called") {
            session.close();

            THEN("the session is finished") {
                REQUIRE(session.is_finished());
            }
        }
    }

    GIVEN("a connected session with a peer-closed read") {
        conn->fire(iso15118::io::ConnectionEvent::ACCEPTED);
        conn->next_read_result = {false, 0, true};
        conn->fire(iso15118::io::ConnectionEvent::NEW_DATA);

        WHEN("poll() reads the closed connection") {
            session.poll();

            THEN("the session is finished") {
                REQUIRE(session.is_finished());
            }
        }
    }

    GIVEN("a connected session with a rate-limiter-deferred response") {
        conn->fire(iso15118::io::ConnectionEvent::ACCEPTED);

        // First response goes out immediately and latches the tx timestamp.
        conn->queue_v2gtp_packet(iso15118::io::v2gtp::PayloadType::SAP, sap_req, sizeof(sap_req));
        conn->fire(iso15118::io::ConnectionEvent::NEW_DATA);
        session.poll();

        // Second response is generated but held back by the 100 ms rate limiter.
        conn->queue_v2gtp_packet(iso15118::io::v2gtp::PayloadType::Part20Main, session_setup_req,
                                 sizeof(session_setup_req));
        conn->fire(iso15118::io::ConnectionEvent::NEW_DATA);
        session.poll();

        REQUIRE_FALSE(session.is_finished());

        WHEN("close() is called with a response still pending") {
            session.close();

            THEN("the session is finished") {
                REQUIRE(session.is_finished());
            }
        }
    }

    GIVEN("a connected session the EV drops before saying anything") {
        std::vector<iso15118::session::feedback::Signal> seen;
        iso15118::session::feedback::Callbacks recording;
        recording.signal = [&seen](iso15118::session::feedback::Signal signal) { seen.push_back(signal); };

        auto dropped_connection = std::make_unique<MockConnection>();
        auto* dropped = dropped_connection.get();
        iso15118::Session dropped_session{std::move(dropped_connection), session_config, recording, pause_ctx,
                                          d2_pause_ctx};

        dropped->fire(iso15118::io::ConnectionEvent::ACCEPTED);
        dropped->next_read_result = {false, 0, true};
        dropped->fire(iso15118::io::ConnectionEvent::NEW_DATA);

        WHEN("poll() reads the closed connection") {
            dropped_session.poll();

            THEN("the data link is released with D-LINK_ERROR, not D-LINK_TERMINATE") {
                // [V2G2-727] / [V2G20-727]: any error the SECC identifies is reported as a data-link
                // error, which restarts matching (ISO 15118-3 Table 6) so the EV can try again.
                REQUIRE(seen == std::vector<iso15118::session::feedback::Signal>{
                                    iso15118::session::feedback::Signal::DLINK_ERROR});
            }
        }
    }

    GIVEN("an established session the EV drops mid-sequence") {
        std::vector<iso15118::session::feedback::Signal> seen;
        iso15118::session::feedback::Callbacks recording;
        recording.signal = [&seen](iso15118::session::feedback::Signal signal) { seen.push_back(signal); };

        auto mid_connection = std::make_unique<MockConnection>();
        auto* mid = mid_connection.get();
        iso15118::Session mid_session{std::move(mid_connection), session_config, recording, pause_ctx, d2_pause_ctx};

        mid->fire(iso15118::io::ConnectionEvent::ACCEPTED);
        mid->queue_v2gtp_packet(iso15118::io::v2gtp::PayloadType::SAP, sap_req, sizeof(sap_req));
        mid->fire(iso15118::io::ConnectionEvent::NEW_DATA);
        // The first poll consumes the request and stages the SupportedAppProtocolRes, which is paced
        // RESPONSE_DELAY_AFTER_REQUEST_MS after the request; wait that out so the second poll writes it
        // and hands the session over to the ISO 15118-20 engine.
        mid_session.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(120));
        mid_session.poll();

        REQUIRE_FALSE(mid_session.is_finished());

        WHEN("the EV closes the connection without a SessionStopReq") {
            mid->next_read_result = {false, 0, true};
            mid->fire(iso15118::io::ConnectionEvent::NEW_DATA);
            mid_session.poll();

            THEN("the data link is released with D-LINK_ERROR") {
                REQUIRE(mid_session.is_finished());
                REQUIRE(seen.back() == iso15118::session::feedback::Signal::DLINK_ERROR);
            }
        }
    }

    GIVEN("a session that never charged") {
        std::vector<iso15118::session::feedback::Signal> seen;
        iso15118::session::feedback::Callbacks recording;
        recording.signal = [&seen](iso15118::session::feedback::Signal signal) { seen.push_back(signal); };

        auto idle_connection = std::make_unique<MockConnection>();
        iso15118::Session idle_session{std::move(idle_connection), session_config, recording, pause_ctx, d2_pause_ctx};

        WHEN("close() tears it down") {
            idle_session.close();

            THEN("only the D-LINK signal is sent, no power path is opened") {
                // close() is the plug-out / session-kill path: the link is already gone or is being
                // taken down from outside, so it stays D-LINK_TERMINATE ([V2G2-726]) rather than
                // running the D-LINK_ERROR matching restart at a connector with no EV on it.
                REQUIRE(seen == std::vector<iso15118::session::feedback::Signal>{
                                    iso15118::session::feedback::Signal::DLINK_TERMINATE});
            }
        }
    }
}

// The teardown state machine behind Session::open_power_path(). Driving a real charge loop through the
// Session needs a captured EXI recording of every message up to CurrentDemand, so the latch itself is
// covered here and the Session only wires it into finish_session()/close().
SCENARIO("Power path teardown signals") {
    using Signal = iso15118::session::feedback::Signal;
    iso15118::PowerPath power_path;

    GIVEN("a DC charge loop that was started") {
        power_path.observe(Signal::CHARGE_LOOP_STARTED);

        WHEN("the session is torn down") {
            const auto signals = power_path.take_teardown_signals();

            THEN("the loop is finished and the DC contactor opened") {
                REQUIRE(signals == std::vector<Signal>{Signal::CHARGE_LOOP_FINISHED, Signal::DC_OPEN_CONTACTOR});
            }
        }

        WHEN("the EV stopped charging first") {
            power_path.observe(Signal::CHARGE_LOOP_FINISHED);

            THEN("the teardown has nothing left to undo") {
                REQUIRE(power_path.take_teardown_signals().empty());
            }
        }

        WHEN("the teardown runs twice (finish_session() then close())") {
            (void)power_path.take_teardown_signals();

            THEN("the second run is a no-op") {
                REQUIRE(power_path.take_teardown_signals().empty());
            }
        }
    }

    GIVEN("an AC session with the contactor closed") {
        power_path.observe(Signal::AC_CLOSE_CONTACTOR);

        WHEN("the session is torn down") {
            const auto signals = power_path.take_teardown_signals();

            THEN("only the AC contactor is opened -- current_demand_finished is DC-only") {
                REQUIRE(signals == std::vector<Signal>{Signal::AC_OPEN_CONTACTOR});
            }
        }

        WHEN("the EV stopped charging first") {
            power_path.observe(Signal::AC_OPEN_CONTACTOR);

            THEN("the teardown has nothing left to undo") {
                REQUIRE(power_path.take_teardown_signals().empty());
            }
        }
    }

    GIVEN("an AC session whose charge loop was signalled too (ISO 15118-20)") {
        power_path.observe(Signal::AC_CLOSE_CONTACTOR);
        power_path.observe(Signal::CHARGE_LOOP_STARTED);

        WHEN("the session is torn down") {
            const auto signals = power_path.take_teardown_signals();

            THEN("the loop is finished and the AC -- not the DC -- contactor is opened") {
                REQUIRE(signals == std::vector<Signal>{Signal::CHARGE_LOOP_FINISHED, Signal::AC_OPEN_CONTACTOR});
            }
        }
    }

    GIVEN("a session that never reached a charge loop") {
        power_path.observe(Signal::SETUP_FINISHED);
        power_path.observe(Signal::START_CABLE_CHECK);
        power_path.observe(Signal::PRE_CHARGE_STARTED);

        THEN("the teardown signals nothing") {
            REQUIRE(power_path.take_teardown_signals().empty());
        }
    }
}
