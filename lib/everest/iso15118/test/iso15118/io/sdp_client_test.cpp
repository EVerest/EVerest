// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>

#include <ifaddrs.h>
#include <net/if.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <netinet/in.h>

#include <cbv2g/exi_v2gtp.h>

#include <iso15118/io/sdp_client.hpp>
#include <iso15118/io/sdp_server.hpp>

using namespace iso15118;

namespace {

// find an up, multicast capable interface with a link-local ipv6 address (needed for the real SDP path)
std::optional<std::string> find_multicast_interface() {
    struct ifaddrs* if_list_head;
    if (getifaddrs(&if_list_head) == -1) {
        return std::nullopt;
    }

    std::optional<std::string> result;
    for (auto current = if_list_head; current != nullptr; current = current->ifa_next) {
        if (current->ifa_addr == nullptr or current->ifa_addr->sa_family != AF_INET6) {
            continue;
        }
        if ((current->ifa_flags & IFF_UP) == 0 or (current->ifa_flags & IFF_MULTICAST) == 0) {
            continue;
        }
        const auto address = reinterpret_cast<const sockaddr_in6*>(current->ifa_addr);
        if (not IN6_IS_ADDR_LINKLOCAL(&address->sin6_addr)) {
            continue;
        }
        result = current->ifa_name;
        break;
    }

    freeifaddrs(if_list_head);
    return result;
}

bool wait_readable(int fd, int timeout_ms) {
    struct pollfd pfd {
        fd, POLLIN, 0
    };
    return ::poll(&pfd, 1, timeout_ms) > 0;
}

// The SDP server port (15118) may already be in use on a developer machine that runs a real SDP
// server. In that case the server socket cannot be bound and the test is skipped.
std::unique_ptr<io::SdpServer> try_make_server(const std::string& interface_name) {
    try {
        return std::make_unique<io::SdpServer>(interface_name);
    } catch (const std::exception& e) {
        WARN("Could not start SdpServer on " << interface_name << " (" << e.what()
                                             << ") - skipping test, the SDP server port is likely already in use");
        return nullptr;
    }
}

} // namespace

SCENARIO("SDP wire format round-trip against the real SdpServer over loopback") {

    GIVEN("A real SdpServer bound on lo and a plain UDP peer that speaks the SdpClient wire format") {
        auto server_ptr = try_make_server("lo");
        if (not server_ptr) {
            SUCCEED();
            return;
        }
        auto& server = *server_ptr;

        // plain UDP peer (loopback unicast, since lo does not support multicast)
        const auto peer_fd = socket(AF_INET6, SOCK_DGRAM, 0);
        REQUIRE(peer_fd != -1);

        sockaddr_in6 peer_address{};
        peer_address.sin6_family = AF_INET6;
        peer_address.sin6_port = 0;
        REQUIRE(inet_pton(AF_INET6, "::1", &peer_address.sin6_addr) == 1);
        REQUIRE(bind(peer_fd, reinterpret_cast<sockaddr*>(&peer_address), sizeof(peer_address)) == 0);

        sockaddr_in6 server_address{};
        server_address.sin6_family = AF_INET6;
        server_address.sin6_port = htons(io::v2gtp::SDP_SERVER_PORT);
        REQUIRE(inet_pton(AF_INET6, "::1", &server_address.sin6_addr) == 1);

        WHEN("The peer sends an SDP request using the same wire format as SdpClient::send_request") {
            uint8_t request[V2GTP_HEADER_LENGTH + 2];
            request[V2GTP_HEADER_LENGTH + 0] = static_cast<uint8_t>(io::v2gtp::Security::TLS);
            request[V2GTP_HEADER_LENGTH + 1] = static_cast<uint8_t>(io::v2gtp::TransportProtocol::TCP);
            V2GTP20_WriteHeader(request, 2, V2GTP20_SDP_REQUEST_PAYLOAD_ID);

            REQUIRE(sendto(peer_fd, request, sizeof(request), 0, reinterpret_cast<sockaddr*>(&server_address),
                           sizeof(server_address)) == sizeof(request));

            THEN("The real SdpServer parses the request and replies with a parseable SDP response") {
                REQUIRE(wait_readable(server.get_fd(), 2000));

                auto peer_request = server.get_peer_request();
                REQUIRE(static_cast<bool>(peer_request));
                REQUIRE(peer_request.security == io::v2gtp::Security::TLS);
                REQUIRE(peer_request.transport_protocol == io::v2gtp::TransportProtocol::TCP);

                io::Ipv6EndPoint advertised_endpoint{};
                advertised_endpoint.port = 50000;
                REQUIRE(inet_pton(AF_INET6, "fe80::1", advertised_endpoint.address) == 1);

                server.send_response(peer_request, advertised_endpoint);

                REQUIRE(wait_readable(peer_fd, 2000));

                uint8_t response[64];
                const auto received = recvfrom(peer_fd, response, sizeof(response), 0, nullptr, nullptr);
                REQUIRE(received == static_cast<ssize_t>(V2GTP_HEADER_LENGTH + 20));

                uint32_t payload_len{0};
                REQUIRE(V2GTP20_ReadHeader(response, &payload_len, V2GTP20_SDP_RESPONSE_PAYLOAD_ID) ==
                        V2GTP_ERROR__NO_ERROR);

                const uint8_t* sdp_response = response + V2GTP_HEADER_LENGTH;
                REQUIRE(std::memcmp(sdp_response, advertised_endpoint.address, 16) == 0);

                uint16_t port{0};
                std::memcpy(&port, sdp_response + 16, sizeof(port));
                REQUIRE(ntohs(port) == advertised_endpoint.port);
                REQUIRE(sdp_response[18] == static_cast<uint8_t>(io::v2gtp::Security::TLS));
                REQUIRE(sdp_response[19] == static_cast<uint8_t>(io::v2gtp::TransportProtocol::TCP));
            }
        }

        ::close(peer_fd);
    }
}

SCENARIO("SdpClient and SdpServer exchange discovery messages end-to-end") {

    const auto interface_name = find_multicast_interface();

    if (not interface_name.has_value()) {
        WARN("No multicast capable link-local IPv6 interface found - skipping end-to-end SDP test "
             "(lo does not support multicast in this environment)");
        SUCCEED();
        return;
    }

    GIVEN("A real SdpServer and a real SdpClient on a multicast capable interface") {
        auto server_ptr = try_make_server(interface_name.value());
        if (not server_ptr) {
            SUCCEED();
            return;
        }
        auto& server = *server_ptr;
        io::SdpClient client(interface_name.value());

        WHEN("The client sends an SDP request and the server responds") {
            client.send_request(io::v2gtp::Security::NO_TRANSPORT_SECURITY);

            REQUIRE(wait_readable(server.get_fd(), 2000));
            auto peer_request = server.get_peer_request();
            REQUIRE(static_cast<bool>(peer_request));
            REQUIRE(peer_request.security == io::v2gtp::Security::NO_TRANSPORT_SECURITY);
            REQUIRE(peer_request.transport_protocol == io::v2gtp::TransportProtocol::TCP);

            io::Ipv6EndPoint advertised_endpoint{};
            advertised_endpoint.port = 50000;
            REQUIRE(inet_pton(AF_INET6, "fe80::abcd", advertised_endpoint.address) == 1);

            server.send_response(peer_request, advertised_endpoint);

            THEN("The client parses the response") {
                REQUIRE(wait_readable(client.get_fd(), 2000));
                const auto response = client.handle_response();
                REQUIRE(response.has_value());
                REQUIRE(response->security == io::v2gtp::Security::NO_TRANSPORT_SECURITY);
                REQUIRE(response->transport_protocol == io::v2gtp::TransportProtocol::TCP);
                REQUIRE(response->endpoint.port == advertised_endpoint.port);
                REQUIRE(std::memcmp(response->endpoint.address, advertised_endpoint.address, 16) == 0);
            }
        }
    }
}
