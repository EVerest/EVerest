// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>
#include <iso15118/v2gtp/sdp.hpp>

using namespace iso15118::v2gtp;

TEST_CASE("SDP request round trip preserves security and TCP", "[sdp-codec]") {
    for (auto security : {Security::TLS, Security::NO_TRANSPORT_SECURITY}) {
        const auto bytes = encode_sdp_request({security, TransportProtocol::TCP});
        const std::vector<uint8_t> expected{0x01, 0xfe, 0x90, 0x00, 0, 0, 0, 2, static_cast<uint8_t>(security), 0};
        REQUIRE(bytes == expected);
        const auto request = decode_sdp_request(bytes.data(), bytes.size());
        REQUIRE(request);
        CHECK(request->security == security);
        CHECK(request->transport == TransportProtocol::TCP);
    }
}

TEST_CASE("SDP request decoder rejects malformed datagrams", "[sdp-codec]") {
    const std::vector<uint8_t> valid{1, 0xfe, 0x90, 0, 0, 0, 0, 2, 0x10, 0};
    REQUIRE(decode_sdp_request(valid.data(), valid.size()));
    CHECK_FALSE(decode_sdp_request(nullptr, valid.size()));
    for (size_t size = 0; size < valid.size(); ++size) {
        CHECK_FALSE(decode_sdp_request(valid.data(), size));
    }
    for (size_t offset : {0, 1, 2, 3, 4, 5, 6, 7, 8, 9}) {
        auto bytes = valid;
        bytes[offset] = 0x42;
        CHECK_FALSE(decode_sdp_request(bytes.data(), bytes.size()));
    }
}

TEST_CASE("SDP request encoding preserves reserved UDP but decoding rejects it", "[sdp-codec]") {
    const auto bytes = encode_sdp_request({Security::TLS, TransportProtocol::RESERVED_FOR_UDP});
    const std::vector<uint8_t> expected{1, 0xfe, 0x90, 0, 0, 0, 0, 2, 0, 0x10};
    REQUIRE(bytes == expected);
    CHECK_FALSE(decode_sdp_request(bytes.data(), bytes.size()));
}

TEST_CASE("SDP response round trip preserves address order and network port bytes", "[sdp-codec]") {
    for (auto security : {Security::TLS, Security::NO_TRANSPORT_SECURITY}) {
        const SdpResponse input{
            {0xfd, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15}, 61341, security, TransportProtocol::TCP};
        const auto bytes = encode_sdp_response(input);
        const std::vector<uint8_t> expected{1,  0xfe, 0x90, 1,  0,  0,  0,    20,   0xfd,
                                            1,  2,    3,    4,  5,  6,  7,    8,    9,
                                            10, 11,   12,   13, 14, 15, 0xef, 0x9d, static_cast<uint8_t>(security),
                                            0};
        REQUIRE(bytes == expected);
        const auto response = decode_sdp_response(bytes.data(), bytes.size());
        REQUIRE(response);
        CHECK(response->address == input.address);
        CHECK(response->port == 61341);
        CHECK(response->security == security);
        CHECK(response->transport == TransportProtocol::TCP);
    }
}

TEST_CASE("SDP response decoder rejects malformed datagrams", "[sdp-codec]") {
    const std::vector<uint8_t> valid{1, 0xfe, 0x90, 1, 0,  0,  0,  20, 0xfd, 1,  2,    3,    4,    5,
                                     6, 7,    8,    9, 10, 11, 12, 13, 14,   15, 0xef, 0x9d, 0x10, 0};
    REQUIRE(decode_sdp_response(valid.data(), valid.size()));
    CHECK_FALSE(decode_sdp_response(nullptr, valid.size()));
    for (size_t size = 0; size < valid.size(); ++size) {
        CHECK_FALSE(decode_sdp_response(valid.data(), size));
    }
    for (size_t offset : {0, 1, 2, 3, 4, 5, 6, 7, 26, 27}) {
        auto bytes = valid;
        bytes[offset] = 0x42;
        CHECK_FALSE(decode_sdp_response(bytes.data(), bytes.size()));
    }
    auto udp = valid;
    udp[27] = 0x10;
    CHECK_FALSE(decode_sdp_response(udp.data(), udp.size()));
}

TEST_CASE("SDP response decoder accepts trailing datagram bytes", "[sdp-codec]") {
    const std::vector<uint8_t> bytes{1, 0xfe, 0x90, 1,  0,  0,  0,  20, 0xfd, 1,    2,    3,    4, 5,    6,
                                     7, 8,    9,    10, 11, 12, 13, 14, 15,   0xef, 0x9d, 0x10, 0, 0xff, 0x42};
    const auto response = decode_sdp_response(bytes.data(), bytes.size());
    REQUIRE(response);
    CHECK(response->port == 61341);
    CHECK(response->security == Security::NO_TRANSPORT_SECURITY);
    const std::array<uint8_t, 16> address{0xfd, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
    CHECK(response->address == address);
}
