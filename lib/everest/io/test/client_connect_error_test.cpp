// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest
//
// A failed client connect has to answer two questions: why did it fail, and how
// long did failing take. These tests pin both on tcp_socket::connect and
// udp_client_socket::connect. Every fixture is loopback-only, so no test here
// depends on DNS or on egress.

#include <everest/io/tcp/tcp_socket.hpp>
#include <everest/io/udp/udp_socket.hpp>

#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <gtest/gtest.h>

using everest::lib::io::tcp::tcp_socket;
using everest::lib::io::udp::udp_client_socket;

namespace {

// A reconnect delay belongs to the retry policy, not to the connect timeout.
// Bounds below are written as "time the connect legitimately took, plus this",
// so a delay that merely copies the connect timeout breaks them.
constexpr int max_reconnect_delay_ms = 250;

// No such interface exists, so bind_socket_to_device exhausts SO_BINDTODEVICE,
// IP_UNICAST_IF and the source-address fallback and throws. Reaches the connect
// failure path without issuing a single packet.
constexpr char unusable_device[] = "nosuchdev0";

int make_loopback_socket(std::uint16_t& bound_port) {
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = ::inet_addr("127.0.0.1");
    addr.sin_port = 0;
    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        ::close(fd);
        return -1;
    }
    socklen_t addr_len = sizeof(addr);
    if (::getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &addr_len) != 0) {
        ::close(fd);
        return -1;
    }
    bound_port = ntohs(addr.sin_port);
    return fd;
}

/// A 127.0.0.1 port bound but never listen()ed. The kernel answers a SYN with
/// RST, so a connect is refused immediately. Holding the socket open for the
/// object's lifetime keeps the port reserved, which is what makes the refusal
/// reproducible rather than a bet on nothing else claiming the port.
class refused_loopback_port {
public:
    refused_loopback_port() {
        m_fd = make_loopback_socket(m_port);
    }

    refused_loopback_port(refused_loopback_port const&) = delete;
    refused_loopback_port& operator=(refused_loopback_port const&) = delete;

    ~refused_loopback_port() {
        if (m_fd >= 0) {
            ::close(m_fd);
        }
    }

    bool reserved() const {
        return m_fd >= 0;
    }
    std::uint16_t port() const {
        return m_port;
    }

private:
    int m_fd{-1};
    std::uint16_t m_port{0};
};

/// Clean completion is POLLOUT without POLLERR/POLLHUP. A connect the kernel
/// never completes reports nothing within the window.
bool connect_completed(int fd, int wait_ms) {
    pollfd pfd{fd, POLLOUT, 0};
    return ::poll(&pfd, 1, wait_ms) == 1 && (pfd.revents & POLLOUT) != 0 && (pfd.revents & (POLLERR | POLLHUP)) == 0;
}

int start_nonblocking_connect(std::uint16_t port) {
    const int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (fd < 0) {
        return -1;
    }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = ::inet_addr("127.0.0.1");
    addr.sin_port = htons(port);
    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 && errno != EINPROGRESS) {
        ::close(fd);
        return -1;
    }
    return fd;
}

/// A 127.0.0.1 listen socket whose accept queue is deliberately saturated:
/// listen(fd, 1), never accept, pre-connects issued until one stalls. With the
/// queue full the kernel stops completing handshakes, so a later connect to
/// port() stays pending until the caller's timeout expires. A loopback stand-in
/// for an unreachable host. All fds stay open until destruction, so the block
/// holds for the object's whole lifetime.
class saturated_loopback_port {
public:
    saturated_loopback_port() {
        m_listen_fd = make_loopback_socket(m_port);
        if (m_listen_fd < 0 || ::listen(m_listen_fd, 1) != 0) {
            return;
        }
        for (int i = 0; i < max_pre_connects; ++i) {
            const int fd = start_nonblocking_connect(m_port);
            if (fd < 0) {
                return;
            }
            m_fds.push_back(fd);
            if (not connect_completed(fd, 200)) {
                m_saturated = true;
                return;
            }
        }
    }

    saturated_loopback_port(saturated_loopback_port const&) = delete;
    saturated_loopback_port& operator=(saturated_loopback_port const&) = delete;

    ~saturated_loopback_port() {
        for (int fd : m_fds) {
            ::close(fd);
        }
        if (m_listen_fd >= 0) {
            ::close(m_listen_fd);
        }
    }

    bool saturated() const {
        return m_saturated;
    }
    std::uint16_t port() const {
        return m_port;
    }

private:
    // ~3 connects saturate in practice on Linux; 8 is headroom, not a tuned value.
    static constexpr int max_pre_connects = 8;
    int m_listen_fd{-1};
    std::uint16_t m_port{0};
    std::vector<int> m_fds;
    bool m_saturated{false};
};

struct connect_outcome {
    bool ok{true};
    int cb_fd{0};
    int elapsed_ms{0};
};

/// Drives connect() and captures both what the callback saw and how long the
/// call blocked. connect() is synchronous, so no worker thread is needed.
template <typename SocketT> connect_outcome drive_connect(SocketT& sock) {
    connect_outcome out;
    const auto start = std::chrono::steady_clock::now();
    sock.connect([&out](bool ok, int fd) {
        out.ok = ok;
        out.cb_fd = fd;
    });
    out.elapsed_ms = static_cast<int>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count());
    return out;
}

} // namespace

// The peer sent an RST, which is the one diagnosis a caller can act on: the host
// is up and the port is closed. connect() drops it, so get_error() falls through
// to get_pending_error() on a descriptor that was never assigned and getsockopt
// reports EBADF instead.
TEST(client_connect_error, tcp_refused_connect_reports_econnrefused) {
    refused_loopback_port refused;
    ASSERT_TRUE(refused.reserved()) << "could not reserve a closed loopback port";

    tcp_socket sock;
    ASSERT_TRUE(sock.setup("127.0.0.1", refused.port(), 1000));

    const auto outcome = drive_connect(sock);

    ASSERT_FALSE(outcome.ok) << "connect to a closed port reported success";
    ASSERT_EQ(outcome.cb_fd, -1) << "a failed connect must report fd -1";
    EXPECT_EQ(sock.get_error(), ECONNREFUSED) << "a refused connect reported errno " << sock.get_error() << " ("
                                              << ::strerror(sock.get_error()) << ") instead of ECONNREFUSED";
}

// Same loss on the timeout leg. connect_with_timeout honors its contract and
// sets ETIMEDOUT, then the layer above discards it.
TEST(client_connect_error, tcp_unreachable_peer_connect_reports_etimedout) {
    saturated_loopback_port blocked;
    ASSERT_TRUE(blocked.saturated()) << "could not saturate the loopback accept queue";

    tcp_socket sock;
    ASSERT_TRUE(sock.setup("127.0.0.1", blocked.port(), 300));

    const auto outcome = drive_connect(sock);

    ASSERT_FALSE(outcome.ok) << "connect to a saturated backlog reported success";
    ASSERT_EQ(outcome.cb_fd, -1) << "a failed connect must report fd -1";
    EXPECT_EQ(sock.get_error(), ETIMEDOUT) << "a connect that timed out reported errno " << sock.get_error() << " ("
                                           << ::strerror(sock.get_error()) << ") instead of ETIMEDOUT";
}

// An RST arrives in well under a millisecond, so nothing about this failure
// justifies waiting out the connect timeout. connect() sleeps m_timeout_ms in
// its catch block regardless, spending a full connect timeout on a connect that
// never took any.
TEST(client_connect_error, tcp_refused_connect_latency_excludes_connect_timeout) {
    refused_loopback_port refused;
    ASSERT_TRUE(refused.reserved()) << "could not reserve a closed loopback port";

    constexpr int connect_timeout_ms = 1000;
    tcp_socket sock;
    ASSERT_TRUE(sock.setup("127.0.0.1", refused.port(), connect_timeout_ms));

    const auto outcome = drive_connect(sock);

    ASSERT_FALSE(outcome.ok) << "connect to a closed port reported success";
    EXPECT_LT(outcome.elapsed_ms, max_reconnect_delay_ms)
        << "an immediately refused connect took " << outcome.elapsed_ms << "ms, which spends the " << connect_timeout_ms
        << "ms connect timeout on a connect that returned at once";
}

// The timeout leg compounds it: connect_with_timeout spends the whole timeout,
// then the catch block sleeps it a second time, so failure latency is twice the
// configured value instead of the timeout plus a reconnect delay.
TEST(client_connect_error, tcp_timed_out_connect_latency_is_not_doubled) {
    saturated_loopback_port blocked;
    ASSERT_TRUE(blocked.saturated()) << "could not saturate the loopback accept queue";

    constexpr int connect_timeout_ms = 500;
    tcp_socket sock;
    ASSERT_TRUE(sock.setup("127.0.0.1", blocked.port(), connect_timeout_ms));

    const auto outcome = drive_connect(sock);

    ASSERT_FALSE(outcome.ok) << "connect to a saturated backlog reported success";
    EXPECT_GE(outcome.elapsed_ms, connect_timeout_ms) << "the connect timeout was not honored at all";
    EXPECT_LT(outcome.elapsed_ms, connect_timeout_ms + max_reconnect_delay_ms)
        << "a connect that timed out took " << outcome.elapsed_ms << "ms, about twice the " << connect_timeout_ms
        << "ms timeout, so the reconnect delay is a second copy of the connect timeout";
}

// udp_client_socket::connect has the same shape and the same two defects. A UDP
// connect() is a local operation, so it cannot be refused by the peer; binding
// to a nonexistent device is the deterministic way to fail it. Whatever the
// cause, EBADF is a claim about a descriptor that was never opened.
TEST(client_connect_error, udp_failed_connect_does_not_report_ebadf) {
    udp_client_socket sock;
    ASSERT_TRUE(sock.setup("127.0.0.1", 9, 1000, unusable_device));

    const auto outcome = drive_connect(sock);

    ASSERT_FALSE(outcome.ok) << "connect bound to a nonexistent device reported success";
    ASSERT_EQ(outcome.cb_fd, -1) << "a failed connect must report fd -1";
    EXPECT_NE(sock.get_error(), EBADF) << "a failed connect reported EBADF, which describes get_error()'s own probe of "
                                          "an unassigned descriptor rather than why the connect failed";
    EXPECT_NE(sock.get_error(), 0) << "a failed connect left get_error() at 0";
}

// The device bind fails before any packet is sent, so there is nothing to wait
// for, yet the catch block sleeps the whole connect timeout.
TEST(client_connect_error, udp_failed_connect_latency_excludes_connect_timeout) {
    constexpr int connect_timeout_ms = 1000;
    udp_client_socket sock;
    ASSERT_TRUE(sock.setup("127.0.0.1", 9, connect_timeout_ms, unusable_device));

    const auto outcome = drive_connect(sock);

    ASSERT_FALSE(outcome.ok) << "connect bound to a nonexistent device reported success";
    EXPECT_LT(outcome.elapsed_ms, max_reconnect_delay_ms)
        << "a connect that failed before touching the network took " << outcome.elapsed_ms << "ms, spending the "
        << connect_timeout_ms << "ms connect timeout";
}
