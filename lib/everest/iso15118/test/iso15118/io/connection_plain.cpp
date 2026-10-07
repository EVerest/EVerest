// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <future>
#include <string>
#include <thread>
#include <vector>

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
    [[maybe_unused]] auto res = ::write(fd, payload.data(), payload.size());
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

bool run_connecting_client() {
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

    ::close(fd);
    return connected == 0;
}

} // namespace

SCENARIO("ConnectionPlain survives a close() from the ACCEPTED event handler") {

    GIVEN("A ConnectionPlain whose ACCEPTED handler rejects the connection (e.g. protocol/TLS gating)") {
        iso15118::io::set_logging_callback([](iso15118::LogLevel, const std::string&) {});

        iso15118::io::PollManager poll_manager;
        iso15118::io::ConnectionPlain connection(poll_manager, LOOPBACK_IFACE);

        std::atomic<bool> saw_closed{false};
        std::atomic<bool> saw_open{false};
        connection.set_event_callback([&](iso15118::io::ConnectionEvent event) {
            if (event == iso15118::io::ConnectionEvent::ACCEPTED) {
                connection.close();
            } else if (event == iso15118::io::ConnectionEvent::OPEN) {
                saw_open.store(true);
            } else if (event == iso15118::io::ConnectionEvent::CLOSED) {
                saw_closed.store(true);
            }
        });

        WHEN("a client connects") {
            auto client_future = std::async(std::launch::async, []() { return run_connecting_client(); });

            const bool got_closed = poll_until(
                poll_manager, [&]() { return saw_closed.load(); }, 5s);

            // A closed fd wrongly re-registered by handle_connect would trip handle_data's assertion here.
            for (int i = 0; i < 5; ++i) {
                poll_manager.poll(10);
            }

            const auto client_connected = client_future.get();

            THEN("CLOSED is delivered and OPEN never fires after it") {
                REQUIRE(client_connected);
                REQUIRE(got_closed);
                REQUIRE_FALSE(saw_open.load());
            }
        }
    }
}

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

SCENARIO("ConnectionPlain::half_close sends our close and reads until the peer closes") {

    GIVEN("A ConnectionPlain on a connected socketpair, polled until OPEN") {
        std::vector<std::string> log_lines;
        iso15118::io::set_logging_callback(
            [&log_lines](iso15118::LogLevel, const std::string& line) { log_lines.push_back(line); });

        std::array<int, 2> fds{-1, -1};
        REQUIRE(::socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, fds.data()) == 0);
        const int peer_fd = fds[1];

        iso15118::io::PollManager poll_manager;
        iso15118::io::ConnectionPlain connection(poll_manager, fds[0], std::nullopt);

        bool open{false};
        int closed_count{0};
        connection.set_event_callback([&](iso15118::io::ConnectionEvent event) {
            if (event == iso15118::io::ConnectionEvent::OPEN) {
                open = true;
            } else if (event == iso15118::io::ConnectionEvent::CLOSED) {
                ++closed_count;
            }
        });

        // The connection opens on the first readable event, so the peer speaks first, as an EV does.
        const uint8_t hello{0x01};
        REQUIRE(::write(peer_fd, &hello, 1) == 1);
        REQUIRE(poll_until(
            poll_manager, [&]() { return open; }, 1s));
        uint8_t received{};
        REQUIRE(connection.read(&received, 1).bytes_read == 1);

        connection.half_close();

        WHEN("the peer reads after the half-close") {
            std::array<uint8_t, 16> buf{};
            const auto peer_read = ::read(peer_fd, buf.data(), buf.size());

            THEN("it sees EOF while the connection stays open") {
                REQUIRE(peer_read == 0);
                REQUIRE(closed_count == 0);
            }
        }

        WHEN("the peer writes after the half-close") {
            const std::array<uint8_t, 8> payload{1, 2, 3, 4, 5, 6, 7, 8};
            REQUIRE(::write(peer_fd, payload.data(), payload.size()) == static_cast<ssize_t>(payload.size()));

            std::array<uint8_t, 16> buf{};
            buf.fill(0xAA);
            const auto result = connection.read(buf.data(), buf.size());

            THEN("read() discards the bytes and reports would_block without touching the buffer") {
                REQUIRE(result.would_block);
                REQUIRE(result.bytes_read == 0);
                REQUIRE_FALSE(result.connection_closed);
                REQUIRE(std::all_of(buf.begin(), buf.end(), [](uint8_t byte) { return byte == 0xAA; }));
            }
        }

        WHEN("the peer closes after the half-close") {
            ::close(peer_fd);
            fds[1] = -1;

            std::array<uint8_t, 16> buf{};
            const auto result = connection.read(buf.data(), buf.size());

            THEN("read() reports connection_closed and close() fires CLOSED exactly once") {
                REQUIRE(result.connection_closed);
                REQUIRE(closed_count == 0);

                connection.close();
                REQUIRE(closed_count == 1);

                const auto lines_after_close = log_lines.size();
                connection.half_close();
                REQUIRE(closed_count == 1);
                REQUIRE(log_lines.size() == lines_after_close);
            }
        }

        if (fds[1] != -1) {
            ::close(fds[1]);
        }
        iso15118::io::set_logging_callback([](iso15118::LogLevel, const std::string&) {});
    }
}
