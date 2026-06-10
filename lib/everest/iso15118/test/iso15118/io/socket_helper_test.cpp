// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <cstring>

#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <iso15118/detail/io/socket_helper.hpp>

TEST_CASE("socket_helper: create_tcp_listen_socket binds and listens") {
    sockaddr_in6 address{};
    address.sin6_family = AF_INET6;
    address.sin6_addr = in6addr_loopback;

    const auto fd = iso15118::io::create_tcp_listen_socket(address, /*port=*/0, /*backlog=*/4);
    REQUIRE(fd >= 0);

    sockaddr_in6 bound_address{};
    socklen_t bound_address_len = sizeof(bound_address);
    REQUIRE(getsockname(fd, reinterpret_cast<sockaddr*>(&bound_address), &bound_address_len) == 0);
    REQUIRE(ntohs(bound_address.sin6_port) != 0);

    // a successful loopback connect proves the socket is in listen state
    const auto client_fd = socket(AF_INET6, SOCK_STREAM, 0);
    REQUIRE(client_fd >= 0);
    const auto connect_result =
        connect(client_fd, reinterpret_cast<const sockaddr*>(&bound_address), sizeof(bound_address));
    CHECK(connect_result == 0);

    close(client_fd);
    close(fd);
}
