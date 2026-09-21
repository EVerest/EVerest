// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <cstdint>
#include <cstring>

#include <arpa/inet.h>

#include <catch2/catch_test_macros.hpp>

#include <iso15118/io/sdp.hpp>
#include <iso15118/io/sdp_packet.hpp>

using iso15118::io::SdpPacket;
using iso15118::io::v2gtp::PayloadType;

namespace {

void feed_header(SdpPacket& packet, uint8_t version, uint8_t inverse_version, uint16_t payload_type,
                 uint32_t payload_length) {
    uint8_t header[SdpPacket::V2GTP_HEADER_SIZE];
    header[0] = version;
    header[1] = inverse_version;

    const uint16_t type = htons(payload_type);
    std::memcpy(header + 2, &type, sizeof(type));

    const uint32_t length = htonl(payload_length);
    std::memcpy(header + 4, &length, sizeof(length));

    std::memcpy(packet.get_current_buffer_pos(), header, sizeof(header));
    packet.update_read_bytes(sizeof(header));
}

constexpr uint8_t VALID_VERSION = 0x01;
constexpr uint8_t VALID_INVERSE_VERSION = 0xFE;
constexpr auto SAP = static_cast<uint16_t>(PayloadType::SAP);

} // namespace

SCENARIO("V2GTP header parsing") {
    SdpPacket packet;

    GIVEN("a well-formed header") {
        feed_header(packet, VALID_VERSION, VALID_INVERSE_VERSION, SAP, 37);

        THEN("the payload is awaited") {
            REQUIRE(packet.get_state() == SdpPacket::State::HEADER_READ);
            REQUIRE(packet.get_remaining_bytes_to_read() == 37);
        }

        WHEN("the payload arrives") {
            const uint8_t payload[37] = {};
            std::memcpy(packet.get_current_buffer_pos(), payload, sizeof(payload));
            packet.update_read_bytes(sizeof(payload));

            THEN("the packet is complete") {
                REQUIRE(packet.is_complete());
                REQUIRE(packet.get_payload_length() == 37);
            }
        }
    }

    GIVEN("a header with an invalid protocol version") {
        feed_header(packet, 0xFF, VALID_INVERSE_VERSION, SAP, 37);

        THEN("the header is rejected") {
            REQUIRE(packet.get_state() == SdpPacket::State::INVALID_HEADER);
        }
    }

    GIVEN("a header with an invalid inverse protocol version") {
        feed_header(packet, VALID_VERSION, 0xFF, SAP, 37);

        THEN("the header is rejected") {
            REQUIRE(packet.get_state() == SdpPacket::State::INVALID_HEADER);
        }
    }

    GIVEN("a header declaring a zero payload length") {
        feed_header(packet, VALID_VERSION, VALID_INVERSE_VERSION, SAP, 0);

        THEN("the header is rejected rather than completing an empty packet") {
            REQUIRE(packet.get_state() == SdpPacket::State::INVALID_HEADER);
            REQUIRE_FALSE(packet.is_complete());
        }
    }

    GIVEN("a header declaring a payload larger than the buffer") {
        feed_header(packet, VALID_VERSION, VALID_INVERSE_VERSION, SAP, iso15118::io::MAX_V2G_PACKET_SIZE);

        THEN("the payload is reported as too long") {
            REQUIRE(packet.get_state() == SdpPacket::State::PAYLOAD_TOO_LONG);
        }
    }

    GIVEN("a header declaring a payload length that would overflow") {
        feed_header(packet, VALID_VERSION, VALID_INVERSE_VERSION, SAP, 0xFFFFFFFF);

        THEN("the header is rejected") {
            REQUIRE(packet.get_state() == SdpPacket::State::INVALID_HEADER);
        }
    }

    GIVEN("a well-formed header carrying an unknown payload type") {
        feed_header(packet, VALID_VERSION, VALID_INVERSE_VERSION, 0x7001, 37);

        THEN("the transport still frames it, the type is judged one layer up") {
            REQUIRE(packet.get_state() == SdpPacket::State::HEADER_READ);
            REQUIRE(packet.get_payload_type() == static_cast<PayloadType>(0x7001));
        }
    }
}

SCENARIO("V2GTP payload types") {
    GIVEN("the payload types the stack handles") {
        THEN("they are recognised") {
            REQUIRE(is_known_payload_type(PayloadType::SAP));
            REQUIRE(is_known_payload_type(PayloadType::Part20Main));
            REQUIRE(is_known_payload_type(PayloadType::Part20AC));
            REQUIRE(is_known_payload_type(PayloadType::Part20DC));
            REQUIRE(is_known_payload_type(PayloadType::Part20DerIec));
            REQUIRE(is_known_payload_type(PayloadType::Part20DerSae));
        }
    }

    GIVEN("payload types outside the enum") {
        THEN("they are not recognised") {
            REQUIRE_FALSE(is_known_payload_type(static_cast<PayloadType>(0x0000)));
            REQUIRE_FALSE(is_known_payload_type(static_cast<PayloadType>(0x7001)));
            REQUIRE_FALSE(is_known_payload_type(static_cast<PayloadType>(0x8000)));
            REQUIRE_FALSE(is_known_payload_type(static_cast<PayloadType>(0x8005)));
            // The SDP server has its own reader, so the discovery types never reach this path.
            REQUIRE_FALSE(is_known_payload_type(static_cast<PayloadType>(0x9000)));
        }
    }
}
