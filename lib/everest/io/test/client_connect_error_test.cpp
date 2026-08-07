// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest
//
// Loopback-only fixtures: no test here depends on DNS or on egress.

#include <everest/io/event/unique_fd.hpp>
#include <everest/io/mdns/mdns_socket.hpp>
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
using everest::lib::io::mdns::mdns_socket;
using everest::lib::io::tcp::tcp_socket;
using everest::lib::io::udp::udp_client_socket;
using everest::lib::io::udp::udp_server_socket;

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

// Let the kernel choose. A server bound to a nonexistent device never reaches
// bind(), and open_udp_server_socket sets SO_REUSEADDR, so a duplicate bind is
// not a dependable way to fail one.
constexpr std::uint16_t kernel_chosen_port = 0;

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

/// A 127.0.0.1 listen socket that accepts connects: bound to an ephemeral port
/// and listen()ed with backlog to spare, never accept()ed. The kernel completes
/// the handshake from the backlog, so a connect to port() succeeds without a
/// server thread. A throwaway probe connect confirms that, so accepting() means
/// "connects here succeed". Ephemeral, because concurrent worktrees in this repo
/// have collided on fixed test ports.
class accepting_loopback_port {
public:
    accepting_loopback_port() {
        m_listen_fd = make_loopback_socket(m_port);
        if (not m_listen_fd.is_fd() || ::listen(m_listen_fd, backlog) != 0) {
            return;
        }
        int err = 0;
        auto probe = start_nonblocking_connect(m_port, err);
        if (not probe.is_fd()) {
            return;
        }
        m_accepting = connect_completed(probe, probe_wait_ms);
    }

    bool accepting() const {
        return m_accepting;
    }
    std::uint16_t port() const {
        return m_port;
    }

private:
    // The probe plus one connect per test; the rest is headroom.
    static constexpr int backlog = 8;
    static constexpr int probe_wait_ms = 500;
    unique_fd m_listen_fd;
    std::uint16_t m_port{0};
    bool m_accepting{false};
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

// open() is the synchronous path and reports through the same get_error(). A
// refused open must name the refusal, not get_error()'s own probe of a
// descriptor that was never assigned.
TEST(client_connect_error_test, tcp_refused_open_reports_econnrefused) {
    refused_loopback_port refused;
    ASSERT_TRUE(refused.reserved()) << "could not reserve a closed loopback port that refuses connects";

    tcp_socket sock;
    ASSERT_FALSE(sock.open("127.0.0.1", refused.port())) << "open of a closed port reported success";

    const int err = sock.get_error();
    EXPECT_EQ(err, ECONNREFUSED) << "a refused open reported errno " << err << " (" << ::strerror(err)
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

// A deliberate close() leaves nothing to probe, and get_error() must still say
// something. Nonzero is the whole contract: zero means "no recorded reason, fall
// through to probing the descriptor", and generic_error_state treats zero as not
// on error, so a descriptor-less socket reporting zero would stop the client from
// reconnecting and present as a hang. The value itself (EBADF from the fallback
// probe) is an implementation detail and is not asserted.
TEST(client_connect_error_test, tcp_closed_socket_reports_nonzero_error) {
    accepting_loopback_port accepting;
    ASSERT_TRUE(accepting.accepting()) << "could not reserve a loopback port that accepts connects";

    tcp_socket sock;
    ASSERT_TRUE(sock.open("127.0.0.1", accepting.port())) << "open of an accepting port failed";
    ASSERT_TRUE(sock.is_open());

    sock.close();

    const int err = sock.get_error();
    EXPECT_NE(err, 0) << "a closed socket reported 0, which reads as healthy and suppresses the client reset";
}

// The recorded reason must move with the descriptor. A failed connect records
// ECONNREFUSED; a later successful open takes ownership of a descriptor and so
// invalidates that reason. Once the socket is closed again the stale value must
// not reappear.
TEST(client_connect_error_test, tcp_successful_open_clears_recorded_connect_error) {
    refused_loopback_port refused;
    ASSERT_TRUE(refused.reserved()) << "could not reserve a closed loopback port that refuses connects";
    accepting_loopback_port accepting;
    ASSERT_TRUE(accepting.accepting()) << "could not reserve a loopback port that accepts connects";

    tcp_socket sock;
    ASSERT_TRUE(sock.setup("127.0.0.1", refused.port(), 1000));
    const auto outcome = drive_connect(sock);
    ASSERT_FALSE(outcome.ok) << "connect to a closed port reported success";
    ASSERT_EQ(sock.get_error(), ECONNREFUSED)
        << "the refused connect was not recorded, so there is nothing to go stale";

    ASSERT_TRUE(sock.open("127.0.0.1", accepting.port())) << "open of an accepting port failed";
    sock.close();

    const int err = sock.get_error();
    EXPECT_NE(err, ECONNREFUSED) << "get_error() still reports the ECONNREFUSED of a connect that predates a "
                                    "successful open, so the recorded reason outlived the descriptor it described";
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

// The synchronous open_as_client path, reached through udp_client_socket::open.
// Same claim strength as the async UDP test above: the recorded reason has to be
// about the failure, not about probing an unassigned descriptor.
TEST(client_connect_error_test, udp_failed_open_reports_the_failure_reason) {
    udp_client_socket sock;
    ASSERT_FALSE(sock.open("127.0.0.1", unused_remote_port, unusable_device))
        << "open bound to a nonexistent device reported success";

    const int err = sock.get_error();
    EXPECT_NE(err, EBADF) << "a failed open reported EBADF, which describes get_error()'s own probe of "
                             "an unassigned descriptor rather than why the open failed";
    EXPECT_NE(err, 0) << "a failed open left get_error() at 0";
}

// open_as_server is reached through udp_server_socket::open, which fd_event_client
// calls from its synchronous init with no handler around it. A throw there escapes
// into the event loop instead of being reported, so a failing open has to return
// false and name the reason like every other policy does.
TEST(client_connect_error_test, udp_server_failed_open_reports_the_failure_reason) {
    udp_server_socket sock;
    ASSERT_FALSE(sock.open(kernel_chosen_port, unusable_device))
        << "open of a server bound to a nonexistent device reported success";
    EXPECT_FALSE(sock.is_open()) << "a failed open kept a descriptor the caller was told does not exist";

    const int err = sock.get_error();
    EXPECT_NE(err, EBADF) << "a failed open reported EBADF, which describes get_error()'s own probe of "
                             "an unassigned descriptor rather than why the open failed";
    EXPECT_NE(err, 0) << "a failed open left get_error() at 0";
    // Same device-lookup-before-capability-check path as the client leg, so ENODEV
    // at any privilege level.
    EXPECT_EQ(err, ENODEV) << "a failed open reported errno " << err << " (" << ::strerror(err)
                           << ") instead of ENODEV";
}

// mdns_socket::open is the third synchronous open on this policy shape and had the
// same unguarded throw. The interface lookup it fails in raises a plain
// std::runtime_error with no errno attached, so only the nonzero invariant is
// claimed here, not a specific value.
TEST(client_connect_error_test, mdns_failed_open_reports_failure_instead_of_throwing) {
    mdns_socket sock;
    ASSERT_FALSE(sock.open(unusable_device)) << "open on a nonexistent interface reported success";
    EXPECT_FALSE(sock.is_open()) << "a failed open kept a descriptor the caller was told does not exist";

    const int err = sock.get_error();
    EXPECT_NE(err, 0) << "a failed open left get_error() at 0, which reads as healthy and suppresses the client reset";
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
