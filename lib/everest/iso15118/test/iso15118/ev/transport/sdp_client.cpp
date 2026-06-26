// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <cstdint>
#include <cstring>

#include <arpa/inet.h>

#include <catch2/catch_test_macros.hpp>

#include <cbv2g/exi_v2gtp.h>

#include <iso15118/ev/io/sdp_client.hpp>
#include <iso15118/io/sdp.hpp>

using iso15118::ev::io::SdpClient;
using namespace iso15118::io;

SCENARIO("SdpClient builds an SDP request") {
    GIVEN("a request for no-security TCP") {
        const auto request =
            SdpClient::build_request(v2gtp::Security::NO_TRANSPORT_SECURITY, v2gtp::TransportProtocol::TCP);

        THEN("the buffer is 10 bytes") {
            REQUIRE(request.size() == 10);
        }

        THEN("the V2GTP header carries the SDP request payload id and length 2") {
            uint32_t payload_len = 0;
            const auto read_result = V2GTP20_ReadHeader(request.data(), &payload_len, V2GTP20_SDP_REQUEST_PAYLOAD_ID);
            REQUIRE(read_result == V2GTP_ERROR__NO_ERROR);
            REQUIRE(payload_len == 2);
        }

        THEN("byte[8] is the security value and byte[9] is the transport value") {
            REQUIRE(request[8] == static_cast<uint8_t>(v2gtp::Security::NO_TRANSPORT_SECURITY));
            REQUIRE(request[9] == static_cast<uint8_t>(v2gtp::TransportProtocol::TCP));
        }
    }

    GIVEN("a request for TLS UDP") {
        const auto request = SdpClient::build_request(v2gtp::Security::TLS, v2gtp::TransportProtocol::RESERVED_FOR_UDP);

        THEN("byte[8] and byte[9] reflect the chosen security and transport") {
            REQUIRE(request[8] == static_cast<uint8_t>(v2gtp::Security::TLS));
            REQUIRE(request[9] == static_cast<uint8_t>(v2gtp::TransportProtocol::RESERVED_FOR_UDP));
        }
    }
}

SCENARIO("SdpClient parses an SDP response") {
    GIVEN("a 28-byte response advertising an address and port") {
        uint8_t buffer[28] = {0};
        V2GTP20_WriteHeader(buffer, 20, V2GTP20_SDP_RESPONSE_PAYLOAD_ID);

        // 16-byte IPv6 address fd00::1 (raw wire bytes)
        const uint8_t wire_addr[16] = {0xfd, 0x00, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x01};
        std::memcpy(buffer + 8, wire_addr, sizeof(wire_addr));

        const uint16_t port_host = 61341;
        const uint16_t port_net = htons(port_host);
        std::memcpy(buffer + 24, &port_net, sizeof(port_net));

        buffer[26] = static_cast<uint8_t>(v2gtp::Security::NO_TRANSPORT_SECURITY);
        buffer[27] = static_cast<uint8_t>(v2gtp::TransportProtocol::TCP);

        const auto response = SdpClient::parse_response(buffer, sizeof(buffer));

        THEN("a response is returned") {
            REQUIRE(response.has_value());
        }

        THEN("the port matches the host-order value") {
            REQUIRE(response->endpoint.port == port_host);
        }

        THEN("the address bytes match the wire bytes verbatim") {
            REQUIRE(std::memcmp(response->endpoint.address, wire_addr, sizeof(wire_addr)) == 0);
        }

        THEN("the advertised security and transport are decoded") {
            REQUIRE(response->security == v2gtp::Security::NO_TRANSPORT_SECURITY);
            REQUIRE(response->transport == v2gtp::TransportProtocol::TCP);
        }
    }

    GIVEN("a response advertising a TLS endpoint") {
        uint8_t buffer[28] = {0};
        V2GTP20_WriteHeader(buffer, 20, V2GTP20_SDP_RESPONSE_PAYLOAD_ID);
        buffer[26] = static_cast<uint8_t>(v2gtp::Security::TLS);
        buffer[27] = static_cast<uint8_t>(v2gtp::TransportProtocol::TCP);

        const auto response = SdpClient::parse_response(buffer, sizeof(buffer));

        THEN("the decoded security reflects TLS") {
            REQUIRE(response.has_value());
            REQUIRE(response->security == v2gtp::Security::TLS);
        }
    }

    GIVEN("a response with the wrong payload id") {
        uint8_t buffer[28] = {0};
        V2GTP20_WriteHeader(buffer, 20, V2GTP20_SDP_REQUEST_PAYLOAD_ID);

        const auto response = SdpClient::parse_response(buffer, sizeof(buffer));

        THEN("no response is returned") {
            REQUIRE_FALSE(response.has_value());
        }
    }

    GIVEN("a buffer that is too short") {
        uint8_t buffer[10] = {0};
        V2GTP20_WriteHeader(buffer, 20, V2GTP20_SDP_RESPONSE_PAYLOAD_ID);

        const auto response = SdpClient::parse_response(buffer, sizeof(buffer));

        THEN("no response is returned") {
            REQUIRE_FALSE(response.has_value());
        }
    }
}
