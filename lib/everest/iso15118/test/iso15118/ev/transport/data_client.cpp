// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <chrono>
#include <cstdint>
#include <cstring>
#include <vector>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <catch2/catch_test_macros.hpp>

#include <everest/io/event/fd_event_handler.hpp>

#include <iso15118/ev/transport/data_client.hpp>
#include <iso15118/io/ipv6_endpoint.hpp>

using everest::lib::io::event::fd_event_handler;
using iso15118::ev::transport::DataClient;
using iso15118::io::Ipv6EndPoint;

namespace {

// A throwaway POSIX IPv6 listening socket bound to [::1]:0; the OS picks the port.
class LoopbackListener {
public:
    LoopbackListener() {
        listen_fd = ::socket(AF_INET6, SOCK_STREAM, 0);
        REQUIRE(listen_fd >= 0);

        int reuse = 1;
        ::setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

        sockaddr_in6 addr{};
        addr.sin6_family = AF_INET6;
        addr.sin6_addr = in6addr_loopback;
        addr.sin6_port = 0; // let the OS choose

        REQUIRE(::bind(listen_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0);
        REQUIRE(::listen(listen_fd, 1) == 0);

        sockaddr_in6 bound{};
        socklen_t bound_len = sizeof(bound);
        REQUIRE(::getsockname(listen_fd, reinterpret_cast<sockaddr*>(&bound), &bound_len) == 0);
        bound_port = ntohs(bound.sin6_port);
    }

    ~LoopbackListener() {
        if (peer_fd >= 0) {
            ::close(peer_fd);
        }
        if (listen_fd >= 0) {
            ::close(listen_fd);
        }
    }

    uint16_t port() const {
        return bound_port;
    }

    // Non-blocking accept attempt; stores the peer fd on success.
    bool try_accept() {
        if (peer_fd >= 0) {
            return true;
        }
        int flags = ::fcntl(listen_fd, F_GETFL, 0);
        ::fcntl(listen_fd, F_SETFL, flags | O_NONBLOCK);
        int fd = ::accept(listen_fd, nullptr, nullptr);
        ::fcntl(listen_fd, F_SETFL, flags);
        if (fd >= 0) {
            peer_fd = fd;
            return true;
        }
        return false;
    }

    int peer() const {
        return peer_fd;
    }

private:
    int listen_fd{-1};
    int peer_fd{-1};
    uint16_t bound_port{0};
};

// The IPv6 loopback endpoint (::1) as raw wire bytes for Ipv6EndPoint::address.
Ipv6EndPoint loopback_endpoint(uint16_t port) {
    Ipv6EndPoint endpoint{};
    endpoint.port = port;
    std::memcpy(endpoint.address, &in6addr_loopback, sizeof(endpoint.address));
    return endpoint;
}

} // namespace

SCENARIO("ISO15118-20 EV DataClient connects and exchanges raw V2GTP frames over IPv6 loopback") {
    GIVEN("a listening socket on [::1] and a DataClient bound to a reactor") {
        LoopbackListener listener;
        fd_event_handler handler;
        DataClient client(handler);

        std::vector<uint8_t> received_on_client;
        client.on_rx([&](const std::vector<uint8_t>& bytes) {
            received_on_client.insert(received_on_client.end(), bytes.begin(), bytes.end());
        });

        THEN("send before connect is rejected because no client exists yet") {
            REQUIRE_FALSE(client.send({0x01, 0x02, 0x03}));
        }

        int connected_count = 0;
        int failed_count = 0;
        client.connect(
            loopback_endpoint(listener.port()), "", [&]() { ++connected_count; }, [&]() { ++failed_count; });

        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        const auto run_reactor_until = [&](auto&& predicate) {
            while (not predicate() and std::chrono::steady_clock::now() < deadline) {
                handler.poll(std::chrono::milliseconds(50));
                handler.run_actions();
                listener.try_accept();
            }
            return predicate();
        };

        WHEN("the reactor is run until the connection is established") {
            const auto connected = run_reactor_until([&]() { return connected_count > 0 and listener.peer() >= 0; });

            THEN("the on-connected callback fired exactly once and on_failed did not fire") {
                REQUIRE(connected);
                REQUIRE(connected_count == 1);
                REQUIRE(failed_count == 0);
            }

            THEN("a second register_events is an idempotent no-op and the data path still works") {
                REQUIRE(connected);
                REQUIRE(failed_count == 0);
                // connect() already registered with this handler; a redundant call
                // must return true without double-registering or disturbing the link.
                REQUIRE(client.register_events(handler));

                const std::vector<uint8_t> frame{0xab, 0xcd, 0xef};
                REQUIRE(client.send(frame));

                std::vector<uint8_t> got;
                const auto rx_done = [&]() {
                    uint8_t buf[64];
                    int flags = ::fcntl(listener.peer(), F_GETFL, 0);
                    ::fcntl(listener.peer(), F_SETFL, flags | O_NONBLOCK);
                    ssize_t n = ::recv(listener.peer(), buf, sizeof(buf), 0);
                    ::fcntl(listener.peer(), F_SETFL, flags);
                    if (n > 0) {
                        got.insert(got.end(), buf, buf + n);
                    }
                    return got.size() >= frame.size();
                };
                while (not rx_done() and std::chrono::steady_clock::now() < deadline) {
                    handler.poll(std::chrono::milliseconds(50));
                    handler.run_actions();
                }
                REQUIRE(got == frame);
            }

            THEN("a frame sent via DataClient::send arrives intact on the peer socket") {
                REQUIRE(connected);
                REQUIRE(failed_count == 0);
                const std::vector<uint8_t> frame{0x01, 0x90, 0x00, 0x02, 0x00, 0x00, 0x00, 0x10, 0xde, 0xad};
                REQUIRE(client.send(frame));

                std::vector<uint8_t> got;
                const auto rx_done = [&]() {
                    uint8_t buf[64];
                    int flags = ::fcntl(listener.peer(), F_GETFL, 0);
                    ::fcntl(listener.peer(), F_SETFL, flags | O_NONBLOCK);
                    ssize_t n = ::recv(listener.peer(), buf, sizeof(buf), 0);
                    ::fcntl(listener.peer(), F_SETFL, flags);
                    if (n > 0) {
                        got.insert(got.end(), buf, buf + n);
                    }
                    return got.size() >= frame.size();
                };
                while (not rx_done() and std::chrono::steady_clock::now() < deadline) {
                    handler.poll(std::chrono::milliseconds(50));
                    handler.run_actions();
                }
                REQUIRE(got == frame);
            }

            THEN("bytes written to the peer arrive on the DataClient rx callback") {
                REQUIRE(connected);
                REQUIRE(failed_count == 0);
                const std::vector<uint8_t> reply{0xca, 0xfe, 0xba, 0xbe, 0x10};
                REQUIRE(::send(listener.peer(), reply.data(), reply.size(), 0) == static_cast<ssize_t>(reply.size()));

                run_reactor_until([&]() { return received_on_client.size() >= reply.size(); });
                REQUIRE(received_on_client == reply);
            }
        }
    }
}

SCENARIO("ISO15118-20 EV DataClient surfaces a failed connect via on_failed") {
    GIVEN("a closed port that nothing is listening on") {
        // Bind+listen to learn a free port, then let the listener close so the
        // connect is refused. libio's connect failure path sleeps for the full
        // connect timeout (5 s) before reporting, so the surfacing is
        // deterministic but not instant; the deadline below allows for it.
        uint16_t closed_port = 0;
        {
            LoopbackListener listener;
            closed_port = listener.port();
        }

        fd_event_handler handler;
        DataClient client(handler);

        int connected_count = 0;
        int failed_count = 0;
        client.connect(
            loopback_endpoint(closed_port), "", [&]() { ++connected_count; }, [&]() { ++failed_count; });

        WHEN("the reactor is run until the connect fails") {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            while (failed_count == 0 and std::chrono::steady_clock::now() < deadline) {
                handler.poll(std::chrono::milliseconds(50));
                handler.run_actions();
            }

            THEN("on_failed fired exactly once and on_connected never fired") {
                REQUIRE(failed_count == 1);
                REQUIRE(connected_count == 0);
            }

            THEN("a second connect to the same closed port re-registers and surfaces its own failure") {
                // Consume the first connect's failure so the second is unambiguous.
                while (failed_count == 0 and std::chrono::steady_clock::now() < deadline) {
                    handler.poll(std::chrono::milliseconds(50));
                    handler.run_actions();
                }
                REQUIRE(failed_count == 1);

                // A reconnect must tear down the prior registration so the fresh
                // client actually gets registered; otherwise register_events
                // short-circuits and the second failure never surfaces (hang).
                client.connect(
                    loopback_endpoint(closed_port), "", [&]() { ++connected_count; }, [&]() { ++failed_count; });

                const auto retry_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
                while (failed_count < 2 and std::chrono::steady_clock::now() < retry_deadline) {
                    handler.poll(std::chrono::milliseconds(50));
                    handler.run_actions();
                }

                REQUIRE(failed_count == 2);
                REQUIRE(connected_count == 0);
            }
        }
    }
}
