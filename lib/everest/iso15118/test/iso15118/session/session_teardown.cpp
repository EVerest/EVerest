// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <memory>
#include <optional>

#include <catch2/catch_test_macros.hpp>

#include <iso15118/d20/config.hpp>
#include <iso15118/d20/context.hpp>
#include <iso15118/session/feedback.hpp>
#include <iso15118/session/iso.hpp>
#include <iso15118/session/logger.hpp>

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
    // The Session FSM logs a state transition on construction through the
    // process-global session log callback, which starts null.
    iso15118::session::logging::set_session_log_callback([](std::size_t, const auto&) {});

    iso15118::session::feedback::Callbacks callbacks;
    callbacks.signal = [](auto) {};

    const iso15118::d20::SessionConfig session_config{iso15118::d20::EvseSetupConfig{}};
    std::optional<iso15118::d20::PauseContext> pause_ctx{std::nullopt};

    auto connection = std::make_unique<MockConnection>();
    auto* conn = connection.get();

    iso15118::Session session{std::move(connection), session_config, callbacks, pause_ctx};

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
}
