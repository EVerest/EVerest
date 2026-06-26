// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#include <everest/io/udp/udp_unconnected_client.hpp>
#include <everest/io/udp/udp_unconnected_socket.hpp>
// event_client_async_policy_v arrives transitively via udp_unconnected_client.hpp
// (fd_event_client.hpp); that header has no include guard, so do not include it
// a second time directly here.

#include <cstring>
#include <string>

#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <gtest/gtest.h>

using everest::lib::io::udp::endpoint;
using everest::lib::io::udp::udp_payload;
using everest::lib::io::udp::udp_unconnected_socket;
using everest::lib::io::utilities::event_client_async_policy_v;

// Policy must be the synchronous variant (open only; no setup/connect), so
// fd_event_client uses the in-thread open() path with no detached connect.
static_assert(not event_client_async_policy_v<udp_unconnected_socket>,
              "udp_unconnected_socket must be a synchronous client policy");

namespace {

const char* loopback(int family) {
    return family == AF_INET6 ? "::1" : "127.0.0.1";
}

// A raw datagram peer bound to loopback on an ephemeral port.
struct peer {
    int fd{-1};
    std::uint16_t port{0};

    explicit peer(int family) {
        fd = ::socket(family, SOCK_DGRAM, 0);
        EXPECT_GE(fd, 0);
        sockaddr_storage ss{};
        socklen_t len = 0;
        if (family == AF_INET6) {
            auto* a = reinterpret_cast<sockaddr_in6*>(&ss);
            a->sin6_family = AF_INET6;
            a->sin6_addr = in6addr_loopback;
            len = sizeof(*a);
        } else {
            auto* a = reinterpret_cast<sockaddr_in*>(&ss);
            a->sin_family = AF_INET;
            a->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            len = sizeof(*a);
        }
        EXPECT_EQ(::bind(fd, reinterpret_cast<sockaddr*>(&ss), len), 0);
        socklen_t blen = sizeof(ss);
        EXPECT_EQ(::getsockname(fd, reinterpret_cast<sockaddr*>(&ss), &blen), 0);
        port = ntohs(family == AF_INET6 ? reinterpret_cast<sockaddr_in6*>(&ss)->sin6_port
                                        : reinterpret_cast<sockaddr_in*>(&ss)->sin_port);
    }
    ~peer() {
        if (fd >= 0) {
            ::close(fd);
        }
    }
};

std::uint16_t bound_port(int fd) {
    sockaddr_storage ss{};
    socklen_t len = sizeof(ss);
    EXPECT_EQ(::getsockname(fd, reinterpret_cast<sockaddr*>(&ss), &len), 0);
    return ntohs(ss.ss_family == AF_INET6 ? reinterpret_cast<sockaddr_in6*>(&ss)->sin6_port
                                          : reinterpret_cast<sockaddr_in*>(&ss)->sin_port);
}

bool wait_readable(int fd, int timeout_ms) {
    pollfd pfd{fd, POLLIN, 0};
    return ::poll(&pfd, 1, timeout_ms) > 0 && (pfd.revents & POLLIN) != 0;
}

bool rx_with_timeout(udp_unconnected_socket& sock, udp_payload& out, int timeout_ms = 1000) {
    if (not wait_readable(sock.get_fd(), timeout_ms)) {
        return false;
    }
    return sock.rx(out);
}

void roundtrip_for_family(int family) {
    peer p(family);

    udp_unconnected_socket u;
    ASSERT_TRUE(u.open(endpoint(loopback(family), p.port)));

    udp_payload msg("ping");
    ASSERT_TRUE(u.tx(msg));

    // Peer receives, learns u's source, echoes back.
    char buf[64];
    sockaddr_storage src{};
    socklen_t slen = sizeof(src);
    ASSERT_TRUE(wait_readable(p.fd, 1000));
    auto n = ::recvfrom(p.fd, buf, sizeof(buf), 0, reinterpret_cast<sockaddr*>(&src), &slen);
    ASSERT_GT(n, 0);
    ASSERT_EQ(::sendto(p.fd, buf, static_cast<size_t>(n), 0, reinterpret_cast<sockaddr*>(&src), slen),
              static_cast<ssize_t>(n));

    udp_payload reply;
    ASSERT_TRUE(rx_with_timeout(u, reply));
    EXPECT_EQ(reply, msg);

    auto last = u.last_source();
    ASSERT_TRUE(last.has_value());
    EXPECT_EQ(last->family(), family);
    EXPECT_EQ(last->port(), p.port);
    EXPECT_EQ(last->addr_str(), loopback(family));
}

// connect-drop-absence: u targets an arbitrary endpoint, but a *different*
// peer sends straight to u's ephemeral port. Because open() does no ::connect,
// the unrelated source is still delivered, and last_source() reflects that
// sender, not the configured target.
void connect_drop_absence_for_family(int family) {
    peer target(family); // configured tx destination; never receives here
    udp_unconnected_socket u;
    ASSERT_TRUE(u.open(endpoint(loopback(family), target.port)));

    std::uint16_t u_port = bound_port(u.get_fd());

    peer other(family); // a sender unrelated to the configured target
    sockaddr_storage dst{};
    socklen_t dlen = 0;
    if (family == AF_INET6) {
        auto* a = reinterpret_cast<sockaddr_in6*>(&dst);
        a->sin6_family = AF_INET6;
        a->sin6_addr = in6addr_loopback;
        a->sin6_port = htons(u_port);
        dlen = sizeof(*a);
    } else {
        auto* a = reinterpret_cast<sockaddr_in*>(&dst);
        a->sin_family = AF_INET;
        a->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        a->sin_port = htons(u_port);
        dlen = sizeof(*a);
    }
    const char payload[] = "from-other";
    ASSERT_EQ(::sendto(other.fd, payload, sizeof(payload), 0, reinterpret_cast<sockaddr*>(&dst), dlen),
              static_cast<ssize_t>(sizeof(payload)));

    udp_payload got;
    ASSERT_TRUE(rx_with_timeout(u, got)) << "datagram from a non-target source was dropped (socket is connected?)";

    auto last = u.last_source();
    ASSERT_TRUE(last.has_value());
    EXPECT_EQ(last->port(), other.port);
    EXPECT_NE(last->port(), target.port);
    EXPECT_FALSE(*last == endpoint(loopback(family), target.port));
}

} // namespace

TEST(udp_unconnected_socket_test, roundtrip_ipv4) {
    roundtrip_for_family(AF_INET);
}

TEST(udp_unconnected_socket_test, roundtrip_ipv6) {
    roundtrip_for_family(AF_INET6);
}

TEST(udp_unconnected_socket_test, connect_drop_absence_ipv4) {
    connect_drop_absence_for_family(AF_INET);
}

TEST(udp_unconnected_socket_test, connect_drop_absence_ipv6) {
    connect_drop_absence_for_family(AF_INET6);
}

TEST(udp_unconnected_socket_test, multicast_egress_iface_set_ipv6) {
    unsigned int lo = if_nametoindex("lo");
    ASSERT_NE(lo, 0u);
    udp_unconnected_socket u;
    ASSERT_TRUE(u.open(endpoint("ff02::1", 5353, "lo")));

    int ifindex = 0;
    socklen_t len = sizeof(ifindex);
    ASSERT_EQ(::getsockopt(u.get_fd(), IPPROTO_IPV6, IPV6_MULTICAST_IF, &ifindex, &len), 0);
    EXPECT_EQ(static_cast<unsigned int>(ifindex), lo);
}

TEST(udp_unconnected_socket_test, multicast_egress_iface_set_ipv4) {
    unsigned int lo = if_nametoindex("lo");
    ASSERT_NE(lo, 0u);
    udp_unconnected_socket u;
    // set_multicast_if() throws if setsockopt(IP_MULTICAST_IF) fails, which
    // would make open() return false; a successful open for a multicast v4
    // target on a named interface proves the egress option was applied.
    // (Linux getsockopt(IP_MULTICAST_IF) returns only a zeroed in_addr when
    // set by interface index, so a value read-back is not possible here; the
    // v6 case below asserts the index strictly via getsockopt.)
    EXPECT_TRUE(u.open(endpoint("239.1.2.3", 5000, "lo")));
}

TEST(udp_unconnected_socket_test, client_alias_constructs) {
    // The fd_event_client alias must instantiate against the sync policy and
    // open the underlying socket synchronously in its constructor.
    peer p(AF_INET);
    everest::lib::io::udp::udp_unconnected_client client(endpoint("127.0.0.1", p.port));
    EXPECT_GE(client.get_poll_fd(), 0);
    auto const& raw = client.get_raw_handler();
    ASSERT_NE(raw, nullptr);
    EXPECT_GE(raw->get_fd(), 0);
}
