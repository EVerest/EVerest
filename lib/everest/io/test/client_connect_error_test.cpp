// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest
//
// Loopback-only fixtures: no test here depends on DNS or on egress.

#include <everest/io/event/unique_fd.hpp>
#include <everest/io/tcp/tcp_socket.hpp>
#include <everest/io/udp/udp_socket.hpp>

#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>

#include <gtest/gtest.h>

using everest::lib::io::event::unique_fd;
using everest::lib::io::tcp::tcp_socket;
using everest::lib::io::udp::udp_client_socket;

namespace {

// Two unrelated budgets, so two names. jitter_slack_ms is scheduling noise on a
// differential comparison; max_reconnect_delay_ms is only a coarse guard against
// a fixed multi-second sleep and constrains no retry-policy choice. Once the
// retry policy has a production header, that bound belongs there.
constexpr int jitter_slack_ms = 100;
constexpr int max_reconnect_delay_ms = 1000;

// No such interface exists. SO_BINDTODEVICE resolves the device name before the
// CAP_NET_RAW check, so this fails ENODEV at the first step whether or not the
// runner is root, and bind_socket_to_device never reaches its fallbacks.
constexpr char unusable_device[] = "nosuchdev0";

// Discard port, never contacted: the device bind fails first.
constexpr std::uint16_t unused_remote_port = 9;

unique_fd make_loopback_socket(std::uint16_t& bound_port) {
    unique_fd fd{::socket(AF_INET, SOCK_STREAM, 0)};
    if (not fd.is_fd()) {
        return {};
    }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = ::inet_addr("127.0.0.1");
    addr.sin_port = 0;
    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        return {};
    }
    socklen_t addr_len = sizeof(addr);
    if (::getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &addr_len) != 0) {
        return {};
    }
    bound_port = ntohs(addr.sin_port);
    return fd;
}

/// Starts a nonblocking connect to 127.0.0.1:\p port. On failure the returned fd
/// is empty and \p err holds the reason.
unique_fd start_nonblocking_connect(std::uint16_t port, int& err) {
    err = 0;
    unique_fd fd{::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0)};
    if (not fd.is_fd()) {
        err = errno;
        return {};
    }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = ::inet_addr("127.0.0.1");
    addr.sin_port = htons(port);
    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 && errno != EINPROGRESS) {
        err = errno;
        return {};
    }
    return fd;
}

/// Clean completion is POLLOUT without POLLERR/POLLHUP. A connect the kernel
/// never completes reports nothing within the window.
bool connect_completed(int fd, int wait_ms) {
    pollfd pfd{fd, POLLOUT, 0};
    return ::poll(&pfd, 1, wait_ms) == 1 && (pfd.revents & POLLOUT) != 0 && (pfd.revents & (POLLERR | POLLHUP)) == 0;
}

/// Reports how the kernel answers a connect to 127.0.0.1:\p port. SO_ERROR is
/// read-and-clear, so it is read exactly once.
int probe_connect_error(std::uint16_t port, int wait_ms) {
    int err = 0;
    auto fd = start_nonblocking_connect(port, err);
    if (not fd.is_fd()) {
        return err;
    }
    pollfd pfd{fd, POLLOUT, 0};
    if (::poll(&pfd, 1, wait_ms) != 1) {
        return ETIMEDOUT;
    }
    socklen_t err_len = sizeof(err);
    if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &err_len) != 0) {
        return errno;
    }
    return err;
}

/// A 127.0.0.1 port bound but never listen()ed. The kernel answers a SYN with
/// RST, so a connect is refused immediately. Holding the socket open for the
/// object's lifetime keeps the port reserved, which is what makes the refusal
/// reproducible rather than a bet on nothing else claiming the port. A throwaway
/// probe connect confirms the refusal, so reserved() means "connects here are
/// refused" and not merely "a port got bound": a bind to 127.0.0.1 also succeeds
/// with lo down, where connects fail for routing reasons instead.
class refused_loopback_port {
public:
    refused_loopback_port() {
        m_fd = make_loopback_socket(m_port);
        if (not m_fd.is_fd()) {
            return;
        }
        m_refused = probe_connect_error(m_port, probe_wait_ms) == ECONNREFUSED;
    }

    bool reserved() const {
        return m_refused;
    }
    std::uint16_t port() const {
        return m_port;
    }

private:
    static constexpr int probe_wait_ms = 500;
    unique_fd m_fd;
    std::uint16_t m_port{0};
    bool m_refused{false};
};

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
        if (not m_listen_fd.is_fd() || ::listen(m_listen_fd, 1) != 0) {
            return;
        }
        for (int i = 0; i < max_pre_connects; ++i) {
            int err = 0;
            auto fd = start_nonblocking_connect(m_port, err);
            if (not fd.is_fd()) {
                return;
            }
            m_fds.push_back(std::move(fd));
            if (not connect_completed(m_fds.back(), pre_connect_wait_ms)) {
                // A stall on the very first pre-connect is a stalled machine,
                // not a full queue.
                m_saturated = i > 0;
                return;
            }
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
    static constexpr int pre_connect_wait_ms = 200;
    unique_fd m_listen_fd;
    std::uint16_t m_port{0};
    std::vector<unique_fd> m_fds;
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

TEST(client_connect_error_test, tcp_refused_connect_reports_econnrefused) {
    refused_loopback_port refused;
    ASSERT_TRUE(refused.reserved()) << "could not reserve a closed loopback port that refuses connects";

    tcp_socket sock;
    ASSERT_TRUE(sock.setup("127.0.0.1", refused.port(), 1000));

    const auto outcome = drive_connect(sock);

    ASSERT_FALSE(outcome.ok) << "connect to a closed port reported success";
    ASSERT_EQ(outcome.cb_fd, -1) << "a failed connect must report fd -1";
    // SO_ERROR is read-and-clear, so get_error() is read once and only the copy
    // is asserted on and reported.
    const int err = sock.get_error();
    EXPECT_EQ(err, ECONNREFUSED) << "a refused connect reported errno " << err << " (" << ::strerror(err)
                                 << ") instead of ECONNREFUSED";
}

TEST(client_connect_error_test, tcp_unreachable_peer_connect_reports_etimedout) {
    saturated_loopback_port blocked;
    ASSERT_TRUE(blocked.saturated()) << "could not saturate the loopback accept queue";

    tcp_socket sock;
    ASSERT_TRUE(sock.setup("127.0.0.1", blocked.port(), 300));

    const auto outcome = drive_connect(sock);

    ASSERT_FALSE(outcome.ok) << "connect to a saturated backlog reported success";
    ASSERT_EQ(outcome.cb_fd, -1) << "a failed connect must report fd -1";
    const int err = sock.get_error();
    EXPECT_EQ(err, ETIMEDOUT) << "a connect that timed out reported errno " << err << " (" << ::strerror(err)
                              << ") instead of ETIMEDOUT";
}

// Failure latency must not scale with the configured connect timeout. Driving
// the same refused connect at two timeouts cancels any constant reconnect delay
// out of the difference, so this holds for whatever delay the retry policy picks.
TEST(client_connect_error_test, tcp_refused_connect_latency_ignores_connect_timeout) {
    refused_loopback_port refused;
    ASSERT_TRUE(refused.reserved()) << "could not reserve a closed loopback port that refuses connects";

    constexpr int short_timeout_ms = 200;
    constexpr int long_timeout_ms = 2000;

    tcp_socket sock;
    ASSERT_TRUE(sock.setup("127.0.0.1", refused.port(), short_timeout_ms));
    const auto short_run = drive_connect(sock);
    ASSERT_FALSE(short_run.ok) << "connect to a closed port reported success";

    ASSERT_TRUE(sock.setup("127.0.0.1", refused.port(), long_timeout_ms));
    const auto long_run = drive_connect(sock);
    ASSERT_FALSE(long_run.ok) << "connect to a closed port reported success";

    EXPECT_LT(short_run.elapsed_ms, max_reconnect_delay_ms)
        << "an immediately refused connect took " << short_run.elapsed_ms << "ms";
    EXPECT_LT(long_run.elapsed_ms - short_run.elapsed_ms, jitter_slack_ms)
        << "raising the connect timeout from " << short_timeout_ms << "ms to " << long_timeout_ms << "ms moved failure "
        << "latency from " << short_run.elapsed_ms << "ms to " << long_run.elapsed_ms
        << "ms, so an immediately refused connect is charged the connect timeout";
}

// Same property on the timeout leg. Here the connect legitimately spends its
// timeout, so the difference must be the difference of the timeouts and nothing
// more: the timeout is not allowed to be charged a second time.
TEST(client_connect_error_test, tcp_timed_out_connect_latency_is_not_doubled) {
    saturated_loopback_port blocked;
    ASSERT_TRUE(blocked.saturated()) << "could not saturate the loopback accept queue";

    constexpr int short_timeout_ms = 300;
    constexpr int long_timeout_ms = 900;

    tcp_socket sock;
    ASSERT_TRUE(sock.setup("127.0.0.1", blocked.port(), short_timeout_ms));
    const auto short_run = drive_connect(sock);
    ASSERT_FALSE(short_run.ok) << "connect to a saturated backlog reported success";

    ASSERT_TRUE(sock.setup("127.0.0.1", blocked.port(), long_timeout_ms));
    const auto long_run = drive_connect(sock);
    ASSERT_FALSE(long_run.ok) << "connect to a saturated backlog reported success";

    EXPECT_GE(short_run.elapsed_ms, short_timeout_ms) << "the connect timeout was not honored at all";
    EXPECT_GE(long_run.elapsed_ms, long_timeout_ms) << "the connect timeout was not honored at all";
    EXPECT_LT(short_run.elapsed_ms - short_timeout_ms, max_reconnect_delay_ms)
        << "a connect that timed out after " << short_timeout_ms << "ms took " << short_run.elapsed_ms << "ms";
    EXPECT_LT(long_run.elapsed_ms - short_run.elapsed_ms, long_timeout_ms - short_timeout_ms + jitter_slack_ms)
        << "raising the connect timeout by " << long_timeout_ms - short_timeout_ms << "ms moved failure latency from "
        << short_run.elapsed_ms << "ms to " << long_run.elapsed_ms << "ms, so the connect timeout is charged twice";
}

// udp_client_socket::connect has the same shape and the same two defects. A UDP
// connect() is a local operation, so it cannot be refused by the peer; binding
// to a nonexistent device is the deterministic way to fail it.
TEST(client_connect_error_test, udp_failed_connect_reports_enodev) {
    udp_client_socket sock;
    ASSERT_TRUE(sock.setup("127.0.0.1", unused_remote_port, 1000, unusable_device));

    const auto outcome = drive_connect(sock);

    ASSERT_FALSE(outcome.ok) << "connect bound to a nonexistent device reported success";
    ASSERT_EQ(outcome.cb_fd, -1) << "a failed connect must report fd -1";
    const int err = sock.get_error();
    EXPECT_NE(err, EBADF) << "a failed connect reported EBADF, which describes get_error()'s own probe of "
                             "an unassigned descriptor rather than why the connect failed";
    EXPECT_NE(err, 0) << "a failed connect left get_error() at 0";
    // The device lookup precedes the capability check, so ENODEV is the answer at
    // any privilege level. Satisfying this needs errno carried across the
    // std::runtime_error unwind.
    EXPECT_EQ(err, ENODEV) << "a failed connect reported errno " << err << " (" << ::strerror(err)
                           << ") instead of ENODEV";
}

TEST(client_connect_error_test, udp_failed_connect_latency_ignores_connect_timeout) {
    constexpr int short_timeout_ms = 200;
    constexpr int long_timeout_ms = 2000;

    udp_client_socket sock;
    ASSERT_TRUE(sock.setup("127.0.0.1", unused_remote_port, short_timeout_ms, unusable_device));
    const auto short_run = drive_connect(sock);
    ASSERT_FALSE(short_run.ok) << "connect bound to a nonexistent device reported success";

    ASSERT_TRUE(sock.setup("127.0.0.1", unused_remote_port, long_timeout_ms, unusable_device));
    const auto long_run = drive_connect(sock);
    ASSERT_FALSE(long_run.ok) << "connect bound to a nonexistent device reported success";

    EXPECT_LT(short_run.elapsed_ms, max_reconnect_delay_ms)
        << "a connect that failed before touching the network took " << short_run.elapsed_ms << "ms";
    EXPECT_LT(long_run.elapsed_ms - short_run.elapsed_ms, jitter_slack_ms)
        << "raising the connect timeout from " << short_timeout_ms << "ms to " << long_timeout_ms << "ms moved failure "
        << "latency from " << short_run.elapsed_ms << "ms to " << long_run.elapsed_ms
        << "ms, so a connect that never touched the network is charged the connect timeout";
}
