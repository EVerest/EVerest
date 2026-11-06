// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstring>
#include <optional>
#include <thread>

#include <poll.h>
#include <unistd.h>

#include <iso15118/io/sdp_server.hpp>
#include <iso15118/io/time.hpp>
#include <iso15118/v2gtp/sdp.hpp>

SCENARIO("V2G Communication Setup Timeout Pattern") {

    GIVEN("A timeout starts when dlink becomes ready") {
        std::optional<iso15118::Timeout> timeout;
        timeout.emplace(100);

        REQUIRE(timeout.has_value());
        REQUIRE(timeout->is_reached() == false);
    }

    GIVEN("A timeout fires after duration expires") {
        std::optional<iso15118::Timeout> timeout;
        timeout.emplace(15);

        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        REQUIRE(timeout->is_reached() == true);
    }

    GIVEN("A timeout is cancelled when session is established") {
        std::optional<iso15118::Timeout> timeout;
        timeout.emplace(100);

        REQUIRE(timeout.has_value());
        // Session established -> cancel timeout
        timeout.reset();
        REQUIRE_FALSE(timeout.has_value());
    }

    GIVEN("A timeout is cancelled when dlink becomes not ready") {
        std::optional<iso15118::Timeout> timeout;
        timeout.emplace(100);

        REQUIRE(timeout.has_value());
        // dlink not ready -> cancel timeout
        timeout.reset();
        REQUIRE_FALSE(timeout.has_value());
    }
}

namespace {
struct SdpExchange {
    iso15118::io::SdpServer server{"lo"};
    int client = socket(AF_INET6, SOCK_DGRAM, 0);

    SdpExchange() {
        REQUIRE(client >= 0);
    }

    ~SdpExchange() {
        ::close(client);
    }

    iso15118::io::PeerRequestContext request(const std::array<uint8_t, 10>& packet) {
        sockaddr_in6 address{};
        address.sin6_family = AF_INET6;
        address.sin6_addr = in6addr_loopback;
        address.sin6_port = htons(iso15118::v2gtp::SDP_SERVER_PORT);
        REQUIRE(sendto(client, packet.data(), packet.size(), 0, reinterpret_cast<sockaddr*>(&address),
                       sizeof(address)) == static_cast<ssize_t>(packet.size()));
        pollfd readable{server.get_fd(), POLLIN, 0};
        REQUIRE(poll(&readable, 1, 1000) == 1);
        return server.get_peer_request();
    }
};
} // namespace

TEST_CASE("SDP server answers valid requests") {
    using namespace iso15118;
    SdpExchange exchange;
    auto request = exchange.request({0x01, 0xfe, 0x90, 0x00, 0, 0, 0, 2, 0x00, 0x00});
    REQUIRE(request);
    REQUIRE(request.security == v2gtp::Security::TLS);
    REQUIRE(request.transport_protocol == v2gtp::TransportProtocol::TCP);

    SECTION("client security is retained") {
    }
    SECTION("controller security override is retained") {
        request.security = v2gtp::Security::NO_TRANSPORT_SECURITY;
    }

    io::Ipv6EndPoint endpoint{49123, {}};
    const std::array<uint8_t, 16> address{0xfe, 0x80, 0, 0, 0, 0, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8};
    std::memcpy(endpoint.address, address.data(), address.size());
    exchange.server.send_response(request, endpoint);
    pollfd readable{exchange.client, POLLIN, 0};
    REQUIRE(poll(&readable, 1, 1000) == 1);
    std::array<uint8_t, 64> packet{};
    const auto size = recv(exchange.client, packet.data(), packet.size(), 0);
    REQUIRE(size == 28);
    const auto response = v2gtp::decode_sdp_response(packet.data(), size);
    REQUIRE(response);
    CHECK(response->address == address);
    CHECK(response->port == endpoint.port);
    CHECK(response->security == request.security);
    CHECK(response->transport == v2gtp::TransportProtocol::TCP);
}

TEST_CASE("SDP server drops unsupported requests") {
    SdpExchange exchange;
    std::array<uint8_t, 10> packet{0x01, 0xfe, 0x90, 0x00, 0, 0, 0, 2, 0x00, 0x00};
    SECTION("unknown security") {
        packet[8] = 0xff;
    }
    SECTION("UDP transport") {
        packet[9] = 0x10;
    }
    REQUIRE_FALSE(exchange.request(packet));
}
