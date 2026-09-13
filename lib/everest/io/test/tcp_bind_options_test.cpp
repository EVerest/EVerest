// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest
//
// Loopback-only fixtures: no test here depends on DNS or on egress.

#include <everest/io/event/unique_fd.hpp>
#include <everest/io/socket/socket.hpp>
#include <everest/io/tcp/tcp_socket.hpp>

#include <cstdint>
#include <string>

#include <netinet/in.h>
#include <sys/socket.h>

#include <gtest/gtest.h>

using everest::lib::io::event::unique_fd;
using everest::lib::io::socket::open_tcp_server_socket;
using everest::lib::io::tcp::source_port_range;
using everest::lib::io::tcp::tcp_socket;

namespace {

// The ISO 15118-2 [V2G2-077]/[V2G2-124] EVCC source port range.
constexpr source_port_range evcc_ports{49152, 65535};

std::uint16_t port_of(int fd) {
    sockaddr_in6 addr{};
    socklen_t len = sizeof(addr);
    if (::getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len) != 0) {
        return 0;
    }
    if (addr.sin6_family == AF_INET6) {
        return ntohs(addr.sin6_port);
    }
    return ntohs(reinterpret_cast<sockaddr_in*>(&addr)->sin_port);
}

// A listening loopback socket on an ephemeral port, never accept()ed: the kernel completes the
// handshake from the backlog.
class loopback_listener {
public:
    loopback_listener(std::string const& address, bool ipv6) : m_fd(open_tcp_server_socket(address, 0, ipv6)) {
        m_port = port_of(m_fd);
    }

    std::uint16_t port() const {
        return m_port;
    }

private:
    unique_fd m_fd;
    std::uint16_t m_port{0};
};

} // namespace

TEST(tcp_bind_options_test, source_port_range_binds_the_local_port_within_the_range_v4) {
    loopback_listener listener("127.0.0.1", false);
    ASSERT_NE(listener.port(), 0);

    tcp_socket sock;
    ASSERT_TRUE(sock.open("127.0.0.1", listener.port(), {}, evcc_ports));

    const auto local_port = port_of(sock.get_fd());
    EXPECT_GE(local_port, evcc_ports.min);
    EXPECT_LE(local_port, evcc_ports.max);
}

TEST(tcp_bind_options_test, source_port_range_binds_the_local_port_within_the_range_v6) {
    loopback_listener listener("::1", true);
    ASSERT_NE(listener.port(), 0);

    tcp_socket sock;
    ASSERT_TRUE(sock.open("::1", listener.port(), {}, evcc_ports));

    const auto local_port = port_of(sock.get_fd());
    EXPECT_GE(local_port, evcc_ports.min);
    EXPECT_LE(local_port, evcc_ports.max);
}

// Without the option the kernel picks, which is what every existing caller gets.
TEST(tcp_bind_options_test, without_a_source_port_range_the_connect_still_succeeds) {
    loopback_listener listener("127.0.0.1", false);
    ASSERT_NE(listener.port(), 0);

    tcp_socket sock;
    ASSERT_TRUE(sock.open("127.0.0.1", listener.port()));
    EXPECT_NE(port_of(sock.get_fd()), 0);
}

// A range of one already taken port: the retry loop must end and report the failure.
TEST(tcp_bind_options_test, an_exhausted_source_port_range_fails_the_open) {
    loopback_listener listener("127.0.0.1", false);
    ASSERT_NE(listener.port(), 0);
    loopback_listener occupied("127.0.0.1", false);
    ASSERT_NE(occupied.port(), 0);

    tcp_socket sock;
    const source_port_range single{occupied.port(), occupied.port()};
    EXPECT_FALSE(sock.open("127.0.0.1", listener.port(), {}, single));
    EXPECT_NE(sock.get_error(), 0);
}

// setup() records both options for the connect() that follows.
TEST(tcp_bind_options_test, setup_accepts_a_device_and_a_source_port_range) {
    loopback_listener listener("127.0.0.1", false);
    ASSERT_NE(listener.port(), 0);

    tcp_socket sock;
    EXPECT_TRUE(sock.setup("127.0.0.1", listener.port(), 1000, "lo", evcc_ports));
}

TEST(tcp_bind_options_test, a_device_bind_to_lo_reaches_a_loopback_peer) {
    loopback_listener listener("127.0.0.1", false);
    ASSERT_NE(listener.port(), 0);

    tcp_socket sock;
    ASSERT_TRUE(sock.open("127.0.0.1", listener.port(), "lo", evcc_ports));

    const auto local_port = port_of(sock.get_fd());
    EXPECT_GE(local_port, evcc_ports.min);
    EXPECT_LE(local_port, evcc_ports.max);
}
