// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest
//
// Characterization test: the connected udp_client already works over IPv6.
// This locks that guarantee so the unconnected-client refactor cannot regress
// it. No udp_client / udp_socket production code is touched by this work.

#include <everest/io/udp/udp_client.hpp>
#include <everest/io/udp/udp_payload.hpp>

#include <atomic>
#include <chrono>
#include <thread>

#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <gtest/gtest.h>

using everest::lib::io::udp::udp_client;
using everest::lib::io::udp::udp_payload;
using namespace std::chrono_literals;

namespace {

// A minimal blocking IPv6 echo socket on [::1]. Bound in the test body so the
// ephemeral port is known before the client connects; serviced by a thread.
class v6_echo {
public:
    v6_echo() {
        m_fd = ::socket(AF_INET6, SOCK_DGRAM, 0);
        EXPECT_GE(m_fd, 0);
        sockaddr_in6 addr{};
        addr.sin6_family = AF_INET6;
        addr.sin6_addr = in6addr_loopback;
        addr.sin6_port = 0;
        EXPECT_EQ(::bind(m_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)), 0);
        socklen_t len = sizeof(addr);
        EXPECT_EQ(::getsockname(m_fd, reinterpret_cast<sockaddr*>(&addr), &len), 0);
        m_port = ntohs(addr.sin6_port);

        timeval tv{};
        tv.tv_usec = 200000; // 200ms so the loop can observe the stop flag
        ::setsockopt(m_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        m_thread = std::thread([this] { run(); });
    }

    ~v6_echo() {
        m_stop = true;
        if (m_thread.joinable()) {
            m_thread.join();
        }
        ::close(m_fd);
    }

    std::uint16_t port() const {
        return m_port;
    }

private:
    void run() {
        char buf[256];
        while (not m_stop) {
            sockaddr_in6 src{};
            socklen_t slen = sizeof(src);
            auto n = ::recvfrom(m_fd, buf, sizeof(buf), 0, reinterpret_cast<sockaddr*>(&src), &slen);
            if (n > 0) {
                ::sendto(m_fd, buf, static_cast<size_t>(n), 0, reinterpret_cast<sockaddr*>(&src), slen);
            }
        }
    }

    int m_fd{-1};
    std::uint16_t m_port{0};
    std::atomic<bool> m_stop{false};
    std::thread m_thread;
};

} // namespace

TEST(udp_client_v6_test, connected_roundtrip_over_ipv6_loopback) {
    v6_echo echo;

    udp_client client("::1", echo.port(), 1000);

    std::atomic<bool> got{false};
    udp_payload received;
    client.set_rx_handler([&](udp_payload const& p, auto&) {
        received = p;
        got = true;
    });

    udp_payload msg("v6-roundtrip");

    auto deadline = std::chrono::steady_clock::now() + 5s;
    auto next_send = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() < deadline && not got) {
        if (std::chrono::steady_clock::now() >= next_send) {
            client.tx(msg);
            next_send = std::chrono::steady_clock::now() + 500ms;
        }
        client.sync(100ms);
    }

    ASSERT_TRUE(got) << "no IPv6 loopback echo received within timeout";
    EXPECT_EQ(received, msg);
}
