// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <array>
#include <chrono>
#include <memory>
#include <optional>

#include <catch2/catch_test_macros.hpp>

#include <iso15118/d20/config.hpp>
#include <iso15118/d20/context.hpp>
#include <iso15118/io/sdp_packet.hpp>
#include <iso15118/io/stream_view.hpp>
#include <iso15118/io/time.hpp>
#include <iso15118/message/authorization.hpp>
#include <iso15118/message/authorization_setup.hpp>
#include <iso15118/message/session_setup.hpp>
#include <iso15118/message/type.hpp>
#include <iso15118/message/variant.hpp>
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

namespace dt = iso15118::message_20::datatypes;

iso15118::d20::SessionConfig make_eim_session_config() {
    iso15118::d20::EvseSetupConfig evse_setup{};
    evse_setup.authorization_services = {dt::Authorization::EIM};
    return iso15118::d20::SessionConfig{evse_setup};
}

template <typename Request> void queue_request(MockConnection& conn, const Request& req) {
    std::array<uint8_t, 256> buffer{};
    const auto size =
        iso15118::message_20::serialize(req, iso15118::io::StreamOutputView{buffer.data(), buffer.size()});
    conn.queue_v2gtp_packet(iso15118::io::v2gtp::PayloadType::Part20Main, buffer.data(), size);
    conn.fire(iso15118::io::ConnectionEvent::NEW_DATA);
}

// The SECC checks this id on every request after SessionSetup.
dt::SessionId session_id_from_last_write(const MockConnection& conn) {
    const auto header_size = iso15118::io::SdpPacket::V2GTP_HEADER_SIZE;
    const iso15118::message_20::Variant response{
        iso15118::io::v2gtp::PayloadType::Part20Main,
        iso15118::io::StreamInputView{conn.last_write.data() + header_size, conn.last_write.size() - header_size}};
    return response.get<iso15118::message_20::SessionSetupResponse>().header.session_id;
}

// Connects and answers SupportedAppProtocolReq, SessionSetupReq and AuthorizationSetupReq, returning the session id.
dt::SessionId start_session(iso15118::Session& session, MockConnection& conn) {
    conn.fire(iso15118::io::ConnectionEvent::ACCEPTED);
    conn.queue_v2gtp_packet(iso15118::io::v2gtp::PayloadType::SAP, sap_req, sizeof(sap_req));
    conn.fire(iso15118::io::ConnectionEvent::NEW_DATA);
    session.poll();
    conn.queue_v2gtp_packet(iso15118::io::v2gtp::PayloadType::Part20Main, session_setup_req, sizeof(session_setup_req));
    conn.fire(iso15118::io::ConnectionEvent::NEW_DATA);
    session.poll();
    const auto session_id = session_id_from_last_write(conn);
    queue_request(conn, iso15118::message_20::AuthorizationSetupRequest{{session_id, 0}});
    session.poll();
    REQUIRE(conn.writes == 3);
    return session_id;
}

void queue_eim_authorization_req(MockConnection& conn, const dt::SessionId& session_id) {
    iso15118::message_20::AuthorizationRequest req{};
    req.header = {session_id, 0};
    req.selected_authorization_service = dt::Authorization::EIM;
    req.authorization_mode = dt::EIM_ASReqAuthorizationMode{};
    queue_request(conn, req);
}

} // namespace

SCENARIO("Session teardown primitives") {
    iso15118::session::feedback::Callbacks callbacks;
    callbacks.signal = [](auto) {};

    const auto session_config = make_eim_session_config();
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
        const auto session_id = start_session(session, *conn);

        // AuthorizationRes is a type change, so it goes out at once.
        queue_eim_authorization_req(*conn, session_id);
        session.poll();

        // A conforming EV repeats AuthorizationReq while EIM is pending. The second AuthorizationRes
        // (Ongoing) repeats the type, so the 100 ms rate limiter holds it back.
        queue_eim_authorization_req(*conn, session_id);
        session.poll();

        REQUIRE(conn->writes == 4);
        REQUIRE_FALSE(session.is_finished());

        WHEN("close() is called with a response still pending") {
            session.close();

            THEN("the session is finished") {
                REQUIRE(session.is_finished());
            }
        }
    }
}

// Each step runs well within the 100 ms minimum response interval.
SCENARIO("Session response interval") {
    iso15118::session::feedback::Callbacks callbacks;
    callbacks.signal = [](auto) {};

    const auto session_config = make_eim_session_config();
    std::optional<iso15118::d20::PauseContext> pause_ctx{std::nullopt};

    auto connection = std::make_unique<MockConnection>();
    auto* conn = connection.get();

    iso15118::Session session{std::move(connection), session_config, callbacks, pause_ctx};

    GIVEN("a connected session that has sent its AuthorizationSetupRes") {
        const auto session_id = start_session(session, *conn);

        WHEN("the first AuthorizationReq arrives") {
            queue_eim_authorization_req(*conn, session_id);
            const auto before = iso15118::get_current_time_point();
            session.poll();
            const auto after = iso15118::get_current_time_point();

            THEN("its AuthorizationRes is sent at once, since its type differs") {
                REQUIRE(conn->writes == 4);
            }

            // A conforming EV repeats AuthorizationReq while EIM is pending. The session stays open,
            // so it is never polled again here: a poll past the deadline would send the held
            // response and make the assertions stale.
            AND_WHEN("a second AuthorizationReq arrives within the response interval") {
                queue_eim_authorization_req(*conn, session_id);
                const auto next_event = session.poll();

                THEN("its AuthorizationRes is held, since its type repeats") {
                    REQUIRE(conn->writes == 4);
                }

                THEN("it is held until 100 ms after the first AuthorizationRes was sent") {
                    // 100 ms is MIN_RESPONSE_INTERVAL_MS in iso.cpp.
                    REQUIRE(next_event >= before + std::chrono::milliseconds(100));
                    REQUIRE(next_event <= after + std::chrono::milliseconds(100));
                }
            }
        }
    }
}
