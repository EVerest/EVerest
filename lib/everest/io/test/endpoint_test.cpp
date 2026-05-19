// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#include <everest/io/udp/endpoint.hpp>

#include <cstring>
#include <stdexcept>

#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <gtest/gtest.h>

using everest::lib::io::udp::endpoint;

TEST(endpoint_test, ipv4_literal) {
    endpoint ep("127.0.0.1", 8080);
    EXPECT_EQ(ep.family(), AF_INET);
    EXPECT_EQ(ep.port(), 8080);
    EXPECT_EQ(ep.addr_str(), "127.0.0.1");
    EXPECT_EQ(ep.sa_len(), sizeof(sockaddr_in));
    EXPECT_NE(ep.sa(), nullptr);
}

TEST(endpoint_test, ipv6_loopback_literal) {
    endpoint ep("::1", 9000);
    EXPECT_EQ(ep.family(), AF_INET6);
    EXPECT_EQ(ep.port(), 9000);
    EXPECT_EQ(ep.addr_str(), "::1");
    EXPECT_EQ(ep.sa_len(), sizeof(sockaddr_in6));
}

TEST(endpoint_test, ipv6_multicast_literal) {
    endpoint ep("ff02::1", 5353);
    EXPECT_EQ(ep.family(), AF_INET6);
    EXPECT_EQ(ep.port(), 5353);
}

TEST(endpoint_test, port_is_host_order) {
    endpoint ep("127.0.0.1", 0x1234);
    // Raw sockaddr must carry network byte order.
    auto const* sin = reinterpret_cast<sockaddr_in const*>(ep.sa());
    EXPECT_EQ(sin->sin_port, htons(0x1234));
    EXPECT_EQ(ep.port(), 0x1234);
}

TEST(endpoint_test, ipv6_link_local_scope_id_from_iface) {
    // Loopback always exists; it is not link-local, so no scope is applied.
    // Use a synthetic link-local address with the loopback interface name to
    // exercise the scope-id path deterministically.
    unsigned int lo = if_nametoindex("lo");
    ASSERT_NE(lo, 0u);
    endpoint ep("fe80::1", 1234, "lo");
    auto const* sin6 = reinterpret_cast<sockaddr_in6 const*>(ep.sa());
    EXPECT_EQ(sin6->sin6_scope_id, lo);
    EXPECT_EQ(ep.iface(), "lo");
}

TEST(endpoint_test, hostname_resolves) {
    endpoint ep("localhost", 4711);
    EXPECT_TRUE(ep.family() == AF_INET || ep.family() == AF_INET6);
    EXPECT_EQ(ep.port(), 4711);
}

TEST(endpoint_test, invalid_host_throws) {
    EXPECT_THROW(endpoint("definitely.not.a.host.invalid.", 80), std::runtime_error);
}

TEST(endpoint_test, from_recvfrom_source_round_trips) {
    endpoint origin("127.0.0.1", 6543);
    sockaddr_storage ss{};
    std::memcpy(&ss, origin.sa(), origin.sa_len());
    endpoint restored(ss, origin.sa_len());
    EXPECT_EQ(restored.family(), AF_INET);
    EXPECT_EQ(restored.port(), 6543);
    EXPECT_EQ(restored.addr_str(), "127.0.0.1");
    EXPECT_TRUE(restored == origin);
}

TEST(endpoint_test, equality_distinguishes_port) {
    endpoint a("127.0.0.1", 1000);
    endpoint b("127.0.0.1", 1001);
    EXPECT_FALSE(a == b);
    EXPECT_TRUE(a != b);
}

// The raw sockaddr/len pair must be directly usable by sendto(): send one
// datagram to a bound loopback UDP socket and read it back.
TEST(endpoint_test, sa_usable_by_sendto) {
    int rx = ::socket(AF_INET, SOCK_DGRAM, 0);
    ASSERT_GE(rx, 0);

    sockaddr_in bind_addr{};
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    bind_addr.sin_port = 0; // ephemeral
    ASSERT_EQ(::bind(rx, reinterpret_cast<sockaddr*>(&bind_addr), sizeof(bind_addr)), 0);

    sockaddr_in bound{};
    socklen_t blen = sizeof(bound);
    ASSERT_EQ(::getsockname(rx, reinterpret_cast<sockaddr*>(&bound), &blen), 0);

    endpoint target("127.0.0.1", ntohs(bound.sin_port));

    int tx = ::socket(AF_INET, SOCK_DGRAM, 0);
    ASSERT_GE(tx, 0);
    const char msg[] = "hello";
    ASSERT_EQ(::sendto(tx, msg, sizeof(msg), 0, target.sa(), target.sa_len()), static_cast<ssize_t>(sizeof(msg)));

    char buf[16]{};
    ASSERT_EQ(::recv(rx, buf, sizeof(buf), 0), static_cast<ssize_t>(sizeof(msg)));
    EXPECT_STREQ(buf, "hello");

    ::close(tx);
    ::close(rx);
}
