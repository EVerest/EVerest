// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <memory>
#include <optional>

#include <catch2/catch_test_macros.hpp>

#include <iso15118/d20/config.hpp>
#include <iso15118/session/feedback.hpp>
#include <iso15118/session/iso.hpp>
#include <iso15118/session/logger.hpp>
#include <iso15118/tbd_controller.hpp>

#include "mock_connection.hpp"

using iso15118::test::MockConnection;

namespace {

// Tracks every connection the controller creates so tests can drive the
// current session's transport and observe session recreation.
struct MockConnectionFactory {
    MockConnection* last{nullptr};
    int calls{0};

    iso15118::TbdController::ConnectionFactory fn() {
        return [this](iso15118::io::PollManager&, const std::string&) -> std::unique_ptr<iso15118::io::IConnection> {
            auto connection = std::make_unique<MockConnection>();
            last = connection.get();
            ++calls;
            return connection;
        };
    }
};

iso15118::TbdController make_controller(const iso15118::session::feedback::Callbacks& callbacks,
                                        MockConnectionFactory& factory) {
    return iso15118::TbdController{iso15118::TbdConfig{{},
                                                       "lo",
                                                       iso15118::config::TlsNegotiationStrategy::ACCEPT_CLIENT_OFFER,
                                                       /*enable_sdp_server=*/false},
                                   callbacks, iso15118::d20::EvseSetupConfig{}, factory.fn()};
}

// Captured SupportedAppProtocolReq offering the -20:AC namespace.
constexpr uint8_t sap_req[] = {0x80, 0x00, 0xf3, 0xab, 0x93, 0x71, 0xd3, 0x4b, 0x9b, 0x79, 0xd3, 0x9b, 0xa3,
                               0x21, 0xd3, 0x4b, 0x9b, 0x79, 0xd1, 0x89, 0xa9, 0x89, 0x89, 0xc1, 0xd1, 0x69,
                               0x91, 0x81, 0xd2, 0x0a, 0x18, 0x01, 0x00, 0x00, 0x04, 0x00, 0x40};

// Captured SessionSetupReq with a zeroed session id (starts a new session).
constexpr uint8_t session_setup_req[] = {0x80, 0x8c, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0c, 0x9f,
                                         0x9c, 0x2b, 0xd0, 0x62, 0x0b, 0x2b, 0xa6, 0xa4, 0xab, 0x18, 0x99, 0x19, 0x9a,
                                         0x1a, 0x9b, 0x1b, 0x9c, 0x1c, 0x98, 0x20, 0xa1, 0x21, 0xa2, 0x22, 0xac, 0x00};

// Drive the current session until a response is deferred by the 100 ms rate limiter.
void arm_deferred_response(iso15118::TbdController& controller, MockConnection& conn) {
    conn.queue_v2gtp_packet(iso15118::io::v2gtp::PayloadType::SAP, sap_req, sizeof(sap_req));
    conn.fire(iso15118::io::ConnectionEvent::NEW_DATA);
    controller.tick();

    conn.queue_v2gtp_packet(iso15118::io::v2gtp::PayloadType::Part20Main, session_setup_req, sizeof(session_setup_req));
    conn.fire(iso15118::io::ConnectionEvent::NEW_DATA);
    controller.tick();
}

} // namespace

SCENARIO("Data-link loss tears down the active session") {
    iso15118::session::logging::set_session_log_callback([](std::size_t, const auto&) {});

    bool terminate_signaled = false;
    iso15118::session::feedback::Callbacks callbacks;
    callbacks.signal = [&](iso15118::session::feedback::Signal s) {
        if (s == iso15118::session::feedback::Signal::DLINK_TERMINATE) {
            terminate_signaled = true;
        }
    };

    MockConnectionFactory factory;
    auto controller = make_controller(callbacks, factory);

    controller.tick();
    REQUIRE(factory.last != nullptr);
    factory.last->fire(iso15118::io::ConnectionEvent::ACCEPTED);

    terminate_signaled = false;

    WHEN("the data link drops and the loop ticks") {
        controller.set_dlink_ready(false);
        controller.tick();

        THEN("the session is torn down") {
            REQUIRE(terminate_signaled);
        }
    }
}

SCENARIO("Data-link loss tears down a session with a deferred response") {
    iso15118::session::logging::set_session_log_callback([](std::size_t, const auto&) {});

    iso15118::session::feedback::Callbacks callbacks;
    callbacks.signal = [](auto) {};

    MockConnectionFactory factory;
    auto controller = make_controller(callbacks, factory);

    controller.tick();
    REQUIRE(factory.calls == 1);
    factory.last->fire(iso15118::io::ConnectionEvent::ACCEPTED);
    arm_deferred_response(controller, *factory.last);

    WHEN("the data link drops and the loop ticks") {
        controller.set_dlink_ready(false);
        controller.tick();

        THEN("the session with the pending response is reaped and replaced") {
            REQUIRE(factory.calls == 2);
        }
    }
}

SCENARIO("Data-link loss reaps a never-connected session") {
    iso15118::session::logging::set_session_log_callback([](std::size_t, const auto&) {});

    bool terminate_signaled = false;
    iso15118::session::feedback::Callbacks callbacks;
    callbacks.signal = [&](iso15118::session::feedback::Signal s) {
        if (s == iso15118::session::feedback::Signal::DLINK_TERMINATE) {
            terminate_signaled = true;
        }
    };

    MockConnectionFactory factory;
    auto controller = make_controller(callbacks, factory);

    controller.tick();
    REQUIRE(factory.last != nullptr);

    terminate_signaled = false;

    WHEN("the data link drops and the loop ticks") {
        controller.set_dlink_ready(false);
        controller.tick();

        THEN("the never-connected session is torn down") {
            REQUIRE(terminate_signaled);
        }
    }
}

SCENARIO("A terminate racing session creation wins over the fresh session") {
    iso15118::session::logging::set_session_log_callback([](std::size_t, const auto&) {});

    bool terminate_signaled = false;
    iso15118::session::feedback::Callbacks callbacks;
    callbacks.signal = [&](iso15118::session::feedback::Signal s) {
        if (s == iso15118::session::feedback::Signal::DLINK_TERMINATE) {
            terminate_signaled = true;
        }
    };

    MockConnectionFactory factory;
    auto controller = make_controller(callbacks, factory);

    GIVEN("a session that exists when a data-link terminate is raised") {
        controller.tick();
        REQUIRE(factory.calls == 1);
        factory.last->fire(iso15118::io::ConnectionEvent::ACCEPTED);

        // A terminate raised while the session is live (as a dlink drop racing an
        // SDP-created session would) must not be swallowed.
        controller.set_dlink_ready(false);
        terminate_signaled = false;

        WHEN("the loop ticks") {
            controller.tick();

            THEN("the session is reaped and a subsequent request creates a fresh one") {
                REQUIRE(terminate_signaled);
                REQUIRE(factory.calls == 2);

                AND_WHEN("the fresh session is accepted and the loop ticks again") {
                    factory.last->fire(iso15118::io::ConnectionEvent::ACCEPTED);
                    terminate_signaled = false;
                    controller.tick();

                    THEN("the recovered session survives") {
                        REQUIRE_FALSE(terminate_signaled);
                        REQUIRE(controller.has_active_session());
                        REQUIRE(factory.calls == 2);
                    }
                }
            }
        }
    }
}
