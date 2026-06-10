// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#include <everest/io/socket/socket.hpp>

#include <cstdint>
#include <string>

#include <netinet/in.h>
#include <sys/socket.h>

#include <gtest/gtest.h>

using everest::lib::io::socket::open_tcp_server_socket;

namespace {

std::uint16_t bound_port(int fd) {
    sockaddr_storage ss{};
    socklen_t len = sizeof(ss);
    EXPECT_EQ(::getsockname(fd, reinterpret_cast<sockaddr*>(&ss), &len), 0);
    return ntohs(ss.ss_family == AF_INET6 ? reinterpret_cast<sockaddr_in6*>(&ss)->sin6_port
                                          : reinterpret_cast<sockaddr_in*>(&ss)->sin_port);
}

} // namespace

TEST(open_tcp_server_socket_test, OpenTcpServerSocketV4) {
    auto fd = open_tcp_server_socket("127.0.0.1", 0, false);
    ASSERT_GE(static_cast<int>(fd), 0);
    EXPECT_NE(bound_port(fd), 0);
}

TEST(open_tcp_server_socket_test, OpenTcpServerSocketV6) {
    auto fd = open_tcp_server_socket("::1", 0, true);
    ASSERT_GE(static_cast<int>(fd), 0);
    EXPECT_NE(bound_port(fd), 0);
}

TEST(open_tcp_server_socket_test, OpenTcpServerSocketBadAddress) {
    EXPECT_THROW(open_tcp_server_socket("not.an.ip", 0, false), std::runtime_error);
}
