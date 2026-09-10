// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#include <everest/io/event/fd_event_handler.hpp>
#include <everest/io/socket/socket.hpp>

#include <cerrno>
#include <cstdint>
#include <string>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <gtest/gtest.h>

using everest::lib::io::event::poll_events;
using everest::lib::io::socket::consume_poll_error;
using everest::lib::io::socket::open_tcp_server_socket;

namespace {

std::uint16_t bound_port(int fd) {
    sockaddr_storage ss{};
    socklen_t len = sizeof(ss);
    EXPECT_EQ(::getsockname(fd, reinterpret_cast<sockaddr*>(&ss), &len), 0);
    return ntohs(ss.ss_family == AF_INET6 ? reinterpret_cast<sockaddr_in6*>(&ss)->sin6_port
                                          : reinterpret_cast<sockaddr_in*>(&ss)->sin_port);
}

// A socket whose connect was refused, so SO_ERROR holds ECONNREFUSED until the first read of it.
// poll() does not consume SO_ERROR, so waiting for the failure here leaves the code to be resolved.
int make_refused_tcp_fd() {
    int probe = ::socket(AF_INET, SOCK_STREAM, 0);
    if (probe < 0) {
        return -1;
    }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = ::inet_addr("127.0.0.1");
    addr.sin_port = 0;
    socklen_t addr_len = sizeof(addr);
    if (::bind(probe, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 or
        ::getsockname(probe, reinterpret_cast<sockaddr*>(&addr), &addr_len) != 0) {
        ::close(probe);
        return -1;
    }
    ::close(probe);

    int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (fd < 0) {
        return -1;
    }
    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0) {
        // Something claimed the released port in the meantime.
        ::close(fd);
        return -1;
    }
    pollfd pfd{fd, POLLOUT, 0};
    if (::poll(&pfd, 1, 2000) != 1 or (pfd.revents & POLLERR) == 0) {
        ::close(fd);
        return -1;
    }
    return fd;
}

// Not a socket, so getsockopt fails on it just as it does on a pty or a tun/tap device.
int make_hungup_pipe_read_fd() {
    int fds[2]{-1, -1};
    if (::pipe2(fds, O_NONBLOCK) != 0) {
        return -1;
    }
    ::close(fds[1]);
    return fds[0];
}

} // namespace

TEST(open_tcp_server_socket_test, OpenTcpServerSocketV4) {
    auto fd = open_tcp_server_socket("127.0.0.1", 0, false);
    ASSERT_GE(static_cast<int>(fd), 0);
    EXPECT_NE(bound_port(fd), 0);
}

TEST(open_tcp_server_socket_test, OpenTcpServerSocketV6) {
    auto fd = open_tcp_server_socket("::1", 0, true);
    ASSERT_GE(static_cast<int>(fd), 0);
    EXPECT_NE(bound_port(fd), 0);
}

TEST(open_tcp_server_socket_test, OpenTcpServerSocketBadAddress) {
    EXPECT_THROW(open_tcp_server_socket("not.an.ip", 0, false), std::runtime_error);
}

TEST(consume_poll_error_test, the_pending_socket_error_outranks_the_fallback) {
    const int fd = make_refused_tcp_fd();
    ASSERT_GE(fd, 0) << "could not produce a refused loopback connect";

    EXPECT_EQ(consume_poll_error(fd, poll_events::error, ECONNRESET), ECONNREFUSED);

    ::close(fd);
}

// Reading SO_ERROR clears it, so only the first call can answer with the real code.
TEST(consume_poll_error_test, the_socket_error_is_consumed_by_the_first_call) {
    const int fd = make_refused_tcp_fd();
    ASSERT_GE(fd, 0) << "could not produce a refused loopback connect";
    ASSERT_EQ(consume_poll_error(fd, poll_events::error, ECONNRESET), ECONNREFUSED);

    EXPECT_EQ(consume_poll_error(fd, poll_events::error, ECONNRESET), ECONNRESET);

    ::close(fd);
}

// getsockopt fails outright on anything that is not a socket, leaving only the caller's fallback.
TEST(consume_poll_error_test, a_descriptor_without_a_code_reports_the_fallback) {
    const int fd = make_hungup_pipe_read_fd();
    ASSERT_GE(fd, 0) << "could not produce a hung up pipe";

    EXPECT_EQ(consume_poll_error(fd, poll_events::hungup, ECONNRESET), ECONNRESET);

    ::close(fd);
}

// 0 reads as success on a descriptor that just died, so it is never returned.
TEST(consume_poll_error_test, a_zero_fallback_resolves_from_the_notification) {
    const int fd = make_hungup_pipe_read_fd();
    ASSERT_GE(fd, 0) << "could not produce a hung up pipe";

    EXPECT_EQ(consume_poll_error(fd, poll_events::hungup, 0), ENOTCONN);
    EXPECT_EQ(consume_poll_error(fd, poll_events::error, 0), EIO);

    ::close(fd);
}
