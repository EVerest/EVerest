// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <chrono>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <arpa/inet.h>

#include <catch2/catch_test_macros.hpp>

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

constexpr uint8_t VALID_VERSION = 0x01;
constexpr uint8_t VALID_INVERSE_VERSION = 0xFE;
constexpr uint16_t UNKNOWN_PAYLOAD_TYPE = 0x7001;

std::vector<uint8_t> make_frame(uint8_t version, uint8_t inverse_version, uint16_t payload_type,
                                uint32_t declared_length, const uint8_t* payload, size_t payload_len) {
    std::vector<uint8_t> frame(iso15118::io::SdpPacket::V2GTP_HEADER_SIZE + payload_len);
    frame[0] = version;
    frame[1] = inverse_version;

    const uint16_t type = htons(payload_type);
    std::memcpy(frame.data() + 2, &type, sizeof(type));

    const uint32_t length = htonl(declared_length);
    std::memcpy(frame.data() + 4, &length, sizeof(length));

    std::memcpy(frame.data() + iso15118::io::SdpPacket::V2GTP_HEADER_SIZE, payload, payload_len);
    return frame;
}

struct Defect {
    std::string name;
    std::vector<uint8_t> frame;
};

// The four defects the conformance tester injects that the SECC answers by dropping the frame,
// wrapped around whichever request the session is expecting.
std::vector<Defect> malformed_frames(uint16_t payload_type, const uint8_t* payload, size_t payload_len) {
    const auto len = static_cast<uint32_t>(payload_len);
    return {
        {"invalid protocol version", make_frame(0xFF, VALID_INVERSE_VERSION, payload_type, len, payload, payload_len)},
        {"invalid inverse protocol version", make_frame(VALID_VERSION, 0xFF, payload_type, len, payload, payload_len)},
        {"unknown payload type",
         make_frame(VALID_VERSION, VALID_INVERSE_VERSION, UNKNOWN_PAYLOAD_TYPE, len, payload, payload_len)},
        {"zero payload length",
         make_frame(VALID_VERSION, VALID_INVERSE_VERSION, payload_type, 0, payload, payload_len)},
    };
}

struct SessionFixture {
    iso15118::session::feedback::Callbacks callbacks;
    iso15118::session::SessionConfig session_config{iso15118::session::EvseSetupConfig{}};
    std::optional<iso15118::d20::PauseContext> pause_ctx{std::nullopt};
    std::optional<iso15118::d2::PauseContext> d2_pause_ctx{std::nullopt};
    MockConnection* conn{nullptr};
    std::unique_ptr<iso15118::Session> session;

    SessionFixture() {
        callbacks.signal = [](auto) {};
        auto connection = std::make_unique<MockConnection>();
        conn = connection.get();
        session = std::make_unique<iso15118::Session>(std::move(connection), session_config, callbacks, pause_ctx,
                                                      d2_pause_ctx);
        conn->fire(iso15118::io::ConnectionEvent::ACCEPTED);
    }

    // Run the SupportedAppProtocol handshake so the ISO 15118-20 engine owns the session.
    void complete_sap_handshake() {
        conn->queue_v2gtp_packet(iso15118::io::v2gtp::PayloadType::SAP, sap_req, sizeof(sap_req));
        conn->fire(iso15118::io::ConnectionEvent::NEW_DATA);
        // The SupportedAppProtocolRes is paced, so the second poll is the one that writes it.
        session->poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(120));
        session->poll();
    }

    void deliver(const std::vector<uint8_t>& frame, int polls = 10) {
        conn->queue_raw(frame.data(), frame.size());
        conn->fire(iso15118::io::ConnectionEvent::NEW_DATA);
        for (int i = 0; i < polls; ++i) {
            session->poll();
        }
    }
};

} // namespace

SCENARIO("Malformed V2GTP frames during the SupportedAppProtocol handshake") {
    GIVEN("a connected session") {
        WHEN("a malformed SupportedAppProtocolReq arrives") {
            THEN("the frame is dropped and the connection is kept") {
                const auto sap_type = static_cast<uint16_t>(iso15118::io::v2gtp::PayloadType::SAP);
                for (const auto& defect : malformed_frames(sap_type, sap_req, sizeof(sap_req))) {
                    INFO("defect: " << defect.name);
                    SessionFixture fixture;
                    fixture.deliver(defect.frame);

                    REQUIRE_FALSE(fixture.conn->closed);
                    REQUIRE_FALSE(fixture.session->is_finished());
                    REQUIRE(fixture.conn->written.empty());
                }
            }
        }

        WHEN("a well-formed SupportedAppProtocolReq arrives") {
            SessionFixture fixture;
            fixture.complete_sap_handshake();

            THEN("it is answered, so the drop path is not over-reaching") {
                REQUIRE_FALSE(fixture.conn->written.empty());
                REQUIRE_FALSE(fixture.conn->closed);
            }
        }
    }
}

SCENARIO("Malformed V2GTP frames after the SupportedAppProtocol handshake") {
    GIVEN("a completed SupportedAppProtocol handshake") {
        WHEN("a malformed SessionSetupReq arrives") {
            THEN("the frame is dropped, the connection is kept and no session is established") {
                const auto main_type = static_cast<uint16_t>(iso15118::io::v2gtp::PayloadType::Part20Main);
                for (const auto& defect : malformed_frames(main_type, session_setup_req, sizeof(session_setup_req))) {
                    INFO("defect: " << defect.name);
                    SessionFixture fixture;
                    fixture.complete_sap_handshake();
                    const auto written_after_handshake = fixture.conn->written.size();

                    fixture.deliver(defect.frame);

                    REQUIRE_FALSE(fixture.conn->closed);
                    REQUIRE_FALSE(fixture.session->is_finished());
                    REQUIRE(fixture.conn->written.size() == written_after_handshake);
                    REQUIRE_FALSE(fixture.session->is_v2g_session_established());
                }
            }
        }
    }
}

SCENARIO("A V2GTP payload length shorter than the payload is ignored") {
    GIVEN("a connected session") {
        SessionFixture fixture;

        WHEN("a SupportedAppProtocolReq declares one byte less than it carries") {
            fixture.deliver(make_frame(VALID_VERSION, VALID_INVERSE_VERSION,
                                       static_cast<uint16_t>(iso15118::io::v2gtp::PayloadType::SAP),
                                       sizeof(sap_req) - 1, sap_req, sizeof(sap_req)));

            THEN("it is neither answered nor a reason to close") {
                REQUIRE(fixture.conn->written.empty());
                REQUIRE_FALSE(fixture.conn->closed);
                REQUIRE_FALSE(fixture.session->is_finished());
            }
        }
    }

    GIVEN("a completed SupportedAppProtocol handshake") {
        SessionFixture fixture;
        fixture.complete_sap_handshake();
        const auto written_after_handshake = fixture.conn->written.size();

        WHEN("a SessionSetupReq declares one byte less than it carries") {
            fixture.deliver(make_frame(VALID_VERSION, VALID_INVERSE_VERSION,
                                       static_cast<uint16_t>(iso15118::io::v2gtp::PayloadType::Part20Main),
                                       sizeof(session_setup_req) - 1, session_setup_req, sizeof(session_setup_req)));

            THEN("it is neither answered nor a reason to close") {
                REQUIRE(fixture.conn->written.size() == written_after_handshake);
                REQUIRE_FALSE(fixture.conn->closed);
                REQUIRE_FALSE(fixture.session->is_finished());
            }
        }
    }
}

SCENARIO("A dropped unknown payload type leaves the stream in sync") {
    GIVEN("a completed SupportedAppProtocol handshake") {
        SessionFixture fixture;
        fixture.complete_sap_handshake();
        const auto written_after_handshake = fixture.conn->written.size();

        WHEN("an unknown payload type is followed by a valid SessionSetupReq") {
            fixture.deliver(make_frame(VALID_VERSION, VALID_INVERSE_VERSION, UNKNOWN_PAYLOAD_TYPE,
                                       sizeof(session_setup_req), session_setup_req, sizeof(session_setup_req)));

            REQUIRE(fixture.conn->written.size() == written_after_handshake);

            fixture.deliver(make_frame(VALID_VERSION, VALID_INVERSE_VERSION,
                                       static_cast<uint16_t>(iso15118::io::v2gtp::PayloadType::Part20Main),
                                       sizeof(session_setup_req), session_setup_req, sizeof(session_setup_req)));
            std::this_thread::sleep_for(std::chrono::milliseconds(120));
            fixture.session->poll();

            THEN("the following request is still answered") {
                REQUIRE(fixture.conn->written.size() > written_after_handshake);
                REQUIRE_FALSE(fixture.conn->closed);
            }
        }
    }
}

SCENARIO("The payload behind a dropped header is drained without spinning") {
    GIVEN("a connected session") {
        SessionFixture fixture;

        WHEN("a frame with an invalid header and a fully queued payload arrives") {
            fixture.deliver(make_frame(0xFF, VALID_INVERSE_VERSION,
                                       static_cast<uint16_t>(iso15118::io::v2gtp::PayloadType::SAP), sizeof(sap_req),
                                       sap_req, sizeof(sap_req)),
                            20);

            THEN("every queued byte is consumed and the connection stays open") {
                REQUIRE(fixture.conn->all_queued_data_read());
                REQUIRE_FALSE(fixture.conn->closed);
                REQUIRE(fixture.conn->written.empty());
            }
        }
    }
}
