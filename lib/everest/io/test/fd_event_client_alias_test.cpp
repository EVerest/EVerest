// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
//
// Regression gate for fd_event_client: every existing client alias must still
// resolve unchanged, and a client (tcp_client) must construct via the plain
// synchronous open() path with no deferral.

#include <everest/io/raw/raw_client.hpp>
#include <everest/io/tcp/tcp_client.hpp>
#include <everest/io/tun_tap/tap_client.hpp>
#include <everest/io/udp/udp_client.hpp>
#include <everest/io/udp/udp_unconnected_client.hpp>

#include <atomic>
#include <chrono>
#include <thread>
#include <type_traits>

#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <gtest/gtest.h>

using namespace std::chrono_literals;

// The existing aliases must remain valid types. A type that fails to resolve
// would not even name here.
static_assert(std::is_class_v<everest::lib::io::tcp::tcp_client>, "tcp_client alias must resolve");
static_assert(std::is_class_v<everest::lib::io::udp::udp_client>, "udp_client alias must resolve");
static_assert(std::is_class_v<everest::lib::io::udp::udp_unconnected_client>,
              "udp_unconnected_client alias must resolve");
static_assert(std::is_class_v<everest::lib::io::raw::raw_client>, "raw_client alias must resolve");
static_assert(std::is_class_v<everest::lib::io::tun_tap::tap_client>, "tap_client alias must resolve");

TEST(fd_event_client_alias, tcp_client_constructs_hookless_path_unchanged) {
    // A loopback listener gives tcp_socket's async setup()/connect() a real peer.
    int srv = ::socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_GE(srv, 0);
    sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    sa.sin_port = 0;
    ASSERT_EQ(::bind(srv, reinterpret_cast<sockaddr*>(&sa), sizeof(sa)), 0);
    ASSERT_EQ(::listen(srv, 1), 0);
    socklen_t len = sizeof(sa);
    ASSERT_EQ(::getsockname(srv, reinterpret_cast<sockaddr*>(&sa), &len), 0);
    const std::uint16_t port = ntohs(sa.sin_port);

    // Accept the connection so the async connect() completes.
    std::atomic<bool> accepted{false};
    std::thread accept_thread([&]() {
        int c = ::accept(srv, nullptr, nullptr);
        if (c >= 0) {
            accepted = true;
            std::this_thread::sleep_for(200ms);
            ::close(c);
        }
    });

    everest::lib::io::tcp::tcp_client client("127.0.0.1", port, 1000);

    // The ready action fires via the synchronous open() path with no deferral.
    std::atomic<bool> ready{false};
    client.set_on_ready_action([&]() { ready = true; });

    EXPECT_GE(client.get_poll_fd(), 0);

    auto deadline = std::chrono::steady_clock::now() + 5s;
    while (std::chrono::steady_clock::now() < deadline && not ready.load()) {
        client.sync(100ms);
    }

    EXPECT_TRUE(ready.load()) << "hookless tcp_client never reached ready";
    auto const& raw = client.get_raw_handler();
    ASSERT_NE(raw, nullptr);
    EXPECT_GE(raw->get_fd(), 0);

    if (accept_thread.joinable()) {
        accept_thread.join();
    }
    ::close(srv);
}
