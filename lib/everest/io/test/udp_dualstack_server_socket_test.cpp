// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#include <everest/io/socket/socket.hpp>
#include <everest/io/udp/endpoint.hpp>
#include <everest/io/udp/udp_dualstack_server.hpp>
#include <everest/io/udp/udp_dualstack_server_socket.hpp>
// event_client_async_policy_v arrives transitively via udp_dualstack_server.hpp
// (fd_event_client.hpp); that header has no include guard, so do not include it
// a second time directly here.

#include <cstring>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <gtest/gtest.h>

using namespace everest::lib::io;
using everest::lib::io::udp::endpoint;
using everest::lib::io::udp::udp_dualstack_server_socket;
using everest::lib::io::udp::udp_payload;
using everest::lib::io::utilities::event_client_async_policy_v;

// Open only, no setup/connect: must be the synchronous client policy.
static_assert(not event_client_async_policy_v<udp_dualstack_server_socket>,
              "udp_dualstack_server_socket must be a synchronous client policy");

namespace {

bool wait_readable(int fd, int timeout_ms) {
    pollfd pfd{fd, POLLIN, 0};
    return ::poll(&pfd, 1, timeout_ms) > 0 && (pfd.revents & POLLIN) != 0;
}

bool rx_with_timeout(udp_dualstack_server_socket& s, udp_payload& out, int timeout_ms = 1000) {
    if (not wait_readable(s.get_fd(), timeout_ms)) {
        return false;
    }
    return s.rx(out);
}

std::uint16_t bound_port(int fd) {
    sockaddr_storage ss{};
    socklen_t len = sizeof(ss);
    EXPECT_EQ(::getsockname(fd, reinterpret_cast<sockaddr*>(&ss), &len), 0);
    return ntohs(ss.ss_family == AF_INET6 ? reinterpret_cast<sockaddr_in6*>(&ss)->sin6_port
                                          : reinterpret_cast<sockaddr_in*>(&ss)->sin_port);
}

} // namespace

TEST(udp_dualstack_factory_test, binds_dualstack_v6_any) {
    auto fd = socket::open_udp_dualstack_server_socket(0); // ephemeral
    ASSERT_GE(static_cast<int>(fd), 0);

    int v6only = 1;
    socklen_t l = sizeof(v6only);
    ASSERT_EQ(::getsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &v6only, &l), 0);
    EXPECT_EQ(v6only, 0); // dual-stack

    sockaddr_storage ss{};
    socklen_t sl = sizeof(ss);
    ASSERT_EQ(::getsockname(fd, reinterpret_cast<sockaddr*>(&ss), &sl), 0);
    EXPECT_EQ(ss.ss_family, AF_INET6);
}

TEST(udp_dualstack_factory_test, device_empty_is_ok) {
    EXPECT_GE(static_cast<int>(socket::open_udp_dualstack_server_socket(0, {})), 0);
}

TEST(udp_dualstack_server_socket_test, v6_roundtrip_loopback) {
    udp_dualstack_server_socket S;
    ASSERT_TRUE(S.open(0));
    const std::uint16_t srv_port = bound_port(S.get_fd());

    int cl = ::socket(AF_INET6, SOCK_DGRAM, 0);
    ASSERT_GE(cl, 0);
    sockaddr_in6 cl_bind{};
    cl_bind.sin6_family = AF_INET6;
    cl_bind.sin6_addr = in6addr_loopback;
    ASSERT_EQ(::bind(cl, reinterpret_cast<sockaddr*>(&cl_bind), sizeof(cl_bind)), 0);
    const std::uint16_t cl_port = bound_port(cl);

    sockaddr_in6 dst{};
    dst.sin6_family = AF_INET6;
    dst.sin6_addr = in6addr_loopback;
    dst.sin6_port = htons(srv_port);
    udp_payload ping("ping6");
    ASSERT_EQ(::sendto(cl, ping.buffer.data(), ping.size(), 0, reinterpret_cast<sockaddr*>(&dst), sizeof(dst)),
              static_cast<ssize_t>(ping.size()));

    udp_payload got;
    ASSERT_TRUE(rx_with_timeout(S, got));
    EXPECT_EQ(got, udp_payload("ping6"));

    auto src = S.last_source();
    ASSERT_TRUE(src.has_value());
    EXPECT_EQ(src->family(), AF_INET6);
    EXPECT_EQ(src->port(), cl_port);
    EXPECT_FALSE(src->is_v4_mapped());

    ASSERT_TRUE(S.tx(udp_payload("pong6")));
    char buf[64]{};
    ASSERT_TRUE(wait_readable(cl, 1000));
    auto n = ::recv(cl, buf, sizeof(buf), 0);
    ASSERT_GT(n, 0);
    EXPECT_STREQ(buf, "pong6");
    ::close(cl);
}

TEST(udp_dualstack_server_socket_test, v4_mapped_roundtrip_loopback) {
    udp_dualstack_server_socket S;
    ASSERT_TRUE(S.open(0));
    const std::uint16_t srv_port = bound_port(S.get_fd());

    int cl = ::socket(AF_INET, SOCK_DGRAM, 0);
    ASSERT_GE(cl, 0);
    sockaddr_in cl_bind{};
    cl_bind.sin_family = AF_INET;
    cl_bind.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ASSERT_EQ(::bind(cl, reinterpret_cast<sockaddr*>(&cl_bind), sizeof(cl_bind)), 0);
    const std::uint16_t cl_port = bound_port(cl);

    sockaddr_in dst{};
    dst.sin_family = AF_INET;
    dst.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    dst.sin_port = htons(srv_port);
    udp_payload ping("ping4");
    ASSERT_EQ(::sendto(cl, ping.buffer.data(), ping.size(), 0, reinterpret_cast<sockaddr*>(&dst), sizeof(dst)),
              static_cast<ssize_t>(ping.size()));

    udp_payload got;
    ASSERT_TRUE(rx_with_timeout(S, got));
    EXPECT_EQ(got, udp_payload("ping4"));

    auto src = S.last_source();
    ASSERT_TRUE(src.has_value());
    EXPECT_EQ(src->family(), AF_INET6); // v4-mapped is carried in a v6 sockaddr
    EXPECT_TRUE(src->is_v4_mapped());
    auto v4 = src->as_v4();
    EXPECT_EQ(v4.family(), AF_INET);
    EXPECT_EQ(v4.addr_str(), "127.0.0.1");
    EXPECT_EQ(v4.port(), cl_port);

    // Reply must reach the v4 client via the verbatim mapped sockaddr.
    ASSERT_TRUE(S.tx(udp_payload("pong4")));
    char buf[64]{};
    ASSERT_TRUE(wait_readable(cl, 1000));
    auto n = ::recv(cl, buf, sizeof(buf), 0);
    ASSERT_GT(n, 0);
    EXPECT_STREQ(buf, "pong4");
    ::close(cl);
}

TEST(udp_dualstack_server_socket_test, tx_before_rx_is_false) {
    udp_dualstack_server_socket S;
    ASSERT_TRUE(S.open(0));
    EXPECT_FALSE(S.tx(udp_payload("nope")));
}
