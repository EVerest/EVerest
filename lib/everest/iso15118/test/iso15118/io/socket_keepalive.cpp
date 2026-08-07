// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <string>

#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include <iso15118/detail/io/socket_helper.hpp>
#include <iso15118/io/logging.hpp>

SCENARIO("set_tcp_keepalive configures the socket") {

    GIVEN("An open TCP/IPv6 socket") {
        const int fd = ::socket(AF_INET6, SOCK_STREAM, 0);
        REQUIRE(fd >= 0);

        WHEN("set_tcp_keepalive is called") {
            const auto ok = iso15118::io::set_tcp_keepalive(fd);

            THEN("keepalive is enabled with the configured idle/interval/count") {
                REQUIRE(ok);

                int value = 0;
                socklen_t len = sizeof(value);

                REQUIRE(::getsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &value, &len) == 0);
                REQUIRE(value == 1);

                REQUIRE(::getsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &value, &len) == 0);
                REQUIRE(value == 10);

                REQUIRE(::getsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &value, &len) == 0);
                REQUIRE(value == 3);

                REQUIRE(::getsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &value, &len) == 0);
                REQUIRE(value == 3);
            }
        }

        ::close(fd);
    }
}

namespace {

// Checks that the keepalive parameters set_tcp_keepalive configures are present on fd.
void require_keepalive_configured(int fd) {
    int value = 0;
    socklen_t len = sizeof(value);

    REQUIRE(::getsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &value, &len) == 0);
    REQUIRE(value == 1);

    REQUIRE(::getsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &value, &len) == 0);
    REQUIRE(value == 10);
}

} // namespace

SCENARIO("create_tcp_listen_socket creates a non-blocking listener") {
    iso15118::io::set_logging_callback([](iso15118::LogLevel, const std::string&) {});

    GIVEN("A listener bound to loopback on an ephemeral port") {
        sockaddr_in6 addr{};
        addr.sin6_family = AF_INET6;
        addr.sin6_addr = in6addr_loopback;

        const int listen_fd = iso15118::io::create_tcp_listen_socket(addr, 0, 1, "lo");
        REQUIRE(listen_fd >= 0);

        THEN("the listener fd is non-blocking, so accept_connection's Transient/EAGAIN contract "
             "holds and a client aborting between poll() and accept4() cannot block the poll loop") {
            const auto flags = ::fcntl(listen_fd, F_GETFL);
            REQUIRE(flags >= 0);
            REQUIRE((flags & O_NONBLOCK) != 0);
        }

        ::close(listen_fd);
    }
}

SCENARIO("accept_connection hands out configured sockets and classifies accept errors") {
    using iso15118::io::AcceptResult;

    iso15118::io::set_logging_callback([](iso15118::LogLevel, const std::string&) {});

    GIVEN("A non-blocking TCP/IPv6 listener on loopback") {
        const int listen_fd = ::socket(AF_INET6, SOCK_STREAM | SOCK_NONBLOCK, 0);
        REQUIRE(listen_fd >= 0);

        sockaddr_in6 addr{};
        addr.sin6_family = AF_INET6;
        addr.sin6_addr = in6addr_loopback;
        addr.sin6_port = 0; // ephemeral: no clash with the port-50000 tests
        REQUIRE(::bind(listen_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0);
        REQUIRE(::listen(listen_fd, 1) == 0);

        socklen_t addr_len = sizeof(addr);
        REQUIRE(::getsockname(listen_fd, reinterpret_cast<sockaddr*>(&addr), &addr_len) == 0);

        WHEN("no connection is pending") {
            sockaddr_in6 peer{};
            const auto result = iso15118::io::accept_connection(listen_fd, peer);

            THEN("the EAGAIN is classified as Transient (keep the listener, no teardown)") {
                REQUIRE(result.status == AcceptResult::Status::Transient);
                REQUIRE(result.fd == -1);
            }
        }

        WHEN("a client connects") {
            const int client_fd = ::socket(AF_INET6, SOCK_STREAM, 0);
            REQUIRE(client_fd >= 0);
            REQUIRE(::connect(client_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0);

            sockaddr_in6 peer{};
            const auto result = iso15118::io::accept_connection(listen_fd, peer);

            THEN("the accepted socket is non-blocking, keepalive-configured, and the peer is filled in") {
                REQUIRE(result.status == AcceptResult::Status::Accepted);
                REQUIRE(result.fd >= 0);

                const auto flags = ::fcntl(result.fd, F_GETFL);
                REQUIRE(flags >= 0);
                REQUIRE((flags & O_NONBLOCK) != 0);

                require_keepalive_configured(result.fd);

                REQUIRE(peer.sin6_family == AF_INET6);

                ::close(result.fd);
            }

            ::close(client_fd);
        }

        ::close(listen_fd);
    }

    GIVEN("An invalid listener fd") {
        WHEN("accept_connection is called on it") {
            sockaddr_in6 peer{};
            const auto result = iso15118::io::accept_connection(-1, peer);

            THEN("the hard failure is classified as Fatal (the caller must tear down), without throwing") {
                REQUIRE(result.status == AcceptResult::Status::Fatal);
                REQUIRE(result.fd == -1);
            }
        }
    }
}
