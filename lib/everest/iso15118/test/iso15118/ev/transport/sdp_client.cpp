// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <cstdint>

#include <catch2/catch_test_macros.hpp>

#include <cbv2g/exi_v2gtp.h>

#include <iso15118/ev/transport/sdp_client.hpp>
#include <iso15118/v2gtp/sdp.hpp>

using iso15118::ev::transport::SdpClient;
using namespace iso15118;

SCENARIO("ISO15118-20 EV SdpClient builds an SDP request") {
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
