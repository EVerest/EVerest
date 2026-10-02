// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include <iso15118/detail/io/socket_helper.hpp>

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
