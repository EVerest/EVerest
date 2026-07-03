// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <future>
#include <thread>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <iso15118/io/connection_plain.hpp>
#include <iso15118/io/logging.hpp>
#include <iso15118/io/poll_manager.hpp>

using namespace std::chrono_literals;

namespace {

constexpr auto LOOPBACK_IFACE = "lo";
constexpr auto SERVER_PORT = 50000;

// Drive poll_manager until predicate returns true or the deadline expires.
template <typename Predicate>
bool poll_until(iso15118::io::PollManager& pm, Predicate done, std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        pm.poll(50);
        if (done()) {
            return true;
        }
    }
    return false;
}

// Connect to the server on loopback, send data the server will NOT read, then
// abort with SO_LINGER {1,0}. Because unread data is still queued at the peer,
// the close sends a RST, so the server's next read() fails with ECONNRESET
// rather than a clean EOF. Signals `reset_done` once the abort has been issued.
bool run_resetting_client(std::atomic<bool>& reset_done) {
    sockaddr_in6 addr{};
    addr.sin6_family = AF_INET6;
    addr.sin6_port = htons(SERVER_PORT);
    if (inet_pton(AF_INET6, "::1", &addr.sin6_addr) != 1) {
        return false;
    }

    const int fd = ::socket(AF_INET6, SOCK_STREAM, 0);
    if (fd < 0) {
        return false;
    }

    int connected = -1;
    for (int i = 0; i < 50; ++i) {
        connected = ::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
        if (connected == 0) {
            break;
        }
        std::this_thread::sleep_for(20ms);
    }
    if (connected != 0) {
        ::close(fd);
        return false;
    }

    const std::array<uint8_t, 8> payload{};
    (void)::write(fd, payload.data(), payload.size());
    // Give the server kernel time to buffer the bytes so they are still unread
    // when the abortive close arrives.
    std::this_thread::sleep_for(100ms);

    struct linger lin {};
    lin.l_onoff = 1;
    lin.l_linger = 0;
    ::setsockopt(fd, SOL_SOCKET, SO_LINGER, &lin, sizeof(lin));

    ::close(fd);
    reset_done.store(true);
    return true;
}

} // namespace

SCENARIO("ConnectionPlain::read reports a fatal errno as a closed connection") {

    GIVEN("A ConnectionPlain listening on loopback") {
        iso15118::io::set_logging_callback([](iso15118::LogLevel, const std::string&) {});

        iso15118::io::PollManager poll_manager;
        iso15118::io::ConnectionPlain connection(poll_manager, LOOPBACK_IFACE);

        std::atomic<bool> connection_open{false};
        std::atomic<bool> reset_done{false};
        std::atomic<bool> saw_reset_read{false};
        std::atomic<bool> reset_read_reported_closed{false};
        connection.set_event_callback([&](iso15118::io::ConnectionEvent event) {
            if (event == iso15118::io::ConnectionEvent::OPEN) {
                connection_open.store(true);
            } else if (event == iso15118::io::ConnectionEvent::NEW_DATA) {
                // Withhold the read until the peer has aborted, so that unread
                // data is still queued when the abortive close fires and the
                // peer therefore sends a RST rather than a clean FIN.
                if (not reset_done.load()) {
                    return;
                }
                std::array<uint8_t, 64> buf{};
                errno = 0;
                const auto r = connection.read(buf.data(), buf.size());
                if (errno == ECONNRESET) {
                    // The read that hits the reset must be surfaced as a closed
                    // connection, not masked as would_block.
                    reset_read_reported_closed.store(r.connection_closed);
                    saw_reset_read.store(true);
                }
            }
        });

        WHEN("the peer aborts the connection with a RST and the server reads") {
            auto client_future = std::async(std::launch::async, [&]() { return run_resetting_client(reset_done); });

            const bool got_open = poll_until(
                poll_manager, [&]() { return connection_open.load(); }, 5s);

            const bool got_reset = poll_until(
                poll_manager, [&]() { return saw_reset_read.load(); }, 5s);

            const auto client_connected = client_future.get();

            THEN("read() surfaces connection_closed instead of would_block") {
                REQUIRE(client_connected);
                REQUIRE(got_open);
                REQUIRE(got_reset);
                REQUIRE(reset_read_reported_closed.load());
            }
        }

        if (connection_open.load()) {
            connection.close();
        }
    }
}
