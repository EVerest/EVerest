// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
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

// SupportedAppProtocolReq offering only urn:din:70121:2012:MsgDef 2.0 (SchemaID 1, Priority 1).
constexpr uint8_t din_sap_req[] = {0x80, 0x00, 0xdb, 0xab, 0x93, 0x71, 0xd3, 0x23, 0x4b, 0x71, 0xd1, 0xb9,
                                   0x81, 0x89, 0x91, 0x89, 0xd1, 0x91, 0x81, 0x89, 0x91, 0xd2, 0x6b, 0x9b,
                                   0x3a, 0x23, 0x2b, 0x30, 0x02, 0x00, 0x00, 0x04, 0x00, 0x40};

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

iso15118::session::EvseSetupConfig make_setup_config(std::vector<iso15118::ProtocolId> supported_protocols) {
    iso15118::session::EvseSetupConfig config;
    config.supported_protocols = std::move(supported_protocols);
    return config;
}

struct SessionFixture {
    iso15118::session::feedback::Callbacks callbacks;
    iso15118::session::SessionConfig session_config;
    std::optional<iso15118::d20::PauseContext> pause_ctx{std::nullopt};
    std::optional<iso15118::d2::PauseContext> d2_pause_ctx{std::nullopt};
    MockConnection* conn{nullptr};
    std::unique_ptr<iso15118::Session> session;

    explicit SessionFixture(
        std::vector<iso15118::ProtocolId> supported_protocols = {iso15118::ProtocolId::ISO15118_20}) :
        session_config(make_setup_config(std::move(supported_protocols))) {
        callbacks.signal = [](auto) {};
        auto connection = std::make_unique<MockConnection>();
        conn = connection.get();
        session = std::make_unique<iso15118::Session>(std::move(connection), session_config, callbacks, pause_ctx,
                                                      d2_pause_ctx);
        conn->fire(iso15118::io::ConnectionEvent::ACCEPTED);
    }

    // Run the SupportedAppProtocol handshake so the negotiated protocol's engine owns the session.
    void complete_sap_handshake(const uint8_t* request = sap_req, size_t request_len = sizeof(sap_req)) {
        conn->queue_v2gtp_packet(iso15118::io::v2gtp::PayloadType::SAP, request, request_len);
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

SCENARIO("Malformed V2GTP headers in a DIN SPEC 70121 session") {
    GIVEN("a completed SupportedAppProtocol handshake that negotiated DIN SPEC 70121") {
        // DIN messages share payload type 0x8001 with the SupportedAppProtocol messages.
        const auto din_type = static_cast<uint16_t>(iso15118::io::v2gtp::PayloadType::SAP);
        const auto len = static_cast<uint32_t>(sizeof(session_setup_req));

        WHEN("the version, inverse version or payload length of the SessionSetupReq header is invalid") {
            THEN("the SECC closes the TCP connection ([V2G-DC-169], [V2G-DC-173])") {
                const std::vector<Defect> defects{
                    {"invalid protocol version", make_frame(0xFF, VALID_INVERSE_VERSION, din_type, len,
                                                            session_setup_req, sizeof(session_setup_req))},
                    {"invalid inverse protocol version",
                     make_frame(VALID_VERSION, 0xFF, din_type, len, session_setup_req, sizeof(session_setup_req))},
                    {"zero payload length", make_frame(VALID_VERSION, VALID_INVERSE_VERSION, din_type, 0,
                                                       session_setup_req, sizeof(session_setup_req))},
                };
                for (const auto& defect : defects) {
                    INFO("defect: " << defect.name);
                    SessionFixture fixture({iso15118::ProtocolId::DIN70121});
                    fixture.complete_sap_handshake(din_sap_req, sizeof(din_sap_req));
                    REQUIRE_FALSE(fixture.conn->written.empty());
                    const auto written_after_handshake = fixture.conn->written.size();

                    fixture.deliver(defect.frame);

                    REQUIRE(fixture.conn->closed);
                    REQUIRE(fixture.conn->written.size() == written_after_handshake);
                    REQUIRE_FALSE(fixture.session->is_v2g_session_established());
                }
            }
        }

        WHEN("the SessionSetupReq carries an unknown payload type") {
            SessionFixture fixture({iso15118::ProtocolId::DIN70121});
            fixture.complete_sap_handshake(din_sap_req, sizeof(din_sap_req));
            const auto written_after_handshake = fixture.conn->written.size();

            fixture.deliver(make_frame(VALID_VERSION, VALID_INVERSE_VERSION, UNKNOWN_PAYLOAD_TYPE, len,
                                       session_setup_req, sizeof(session_setup_req)));

            THEN("the payload is skipped and the connection is kept ([V2G-DC-171])") {
                REQUIRE_FALSE(fixture.conn->closed);
                REQUIRE(fixture.conn->written.size() == written_after_handshake);
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
