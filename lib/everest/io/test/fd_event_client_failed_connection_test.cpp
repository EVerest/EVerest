// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
//
// A connection that has reported a failure takes no further reads or writes (fd_event_client.cpp).
// The shape here is the one an AF_PACKET socket shows when its interface goes down with frames
// queued, on a connected UDP socket over loopback: an ICMP port unreachable sets the socket error
// (EPOLLERR), and a datagram queued behind it still reads fine. A client that read it would put
// the state back to connected before the error-fd handler ran: the failure never reported, the
// failed flag stuck for the fd's life, every later error on it skipped and level-triggered forever.
#include <everest/io/event/event_fd.hpp>
#include <everest/io/event/fd_event_client.hpp>
#include <everest/io/socket/socket.hpp>
#include <everest/io/tcp/tcp_socket.hpp>
#include <everest/io/udp/udp_client.hpp>
#include <everest/io/udp/udp_payload.hpp>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <memory>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

using namespace std::chrono_literals;
using everest::lib::io::tcp::tcp_socket;
using everest::lib::io::udp::udp_client;
using everest::lib::io::udp::udp_payload;

namespace {

int bind_loopback_udp(std::uint16_t port) {
    int const fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        return -1;
    }
    int one = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    sa.sin_port = htons(port);
    if (::bind(fd, reinterpret_cast<sockaddr*>(&sa), sizeof(sa)) != 0) {
        ::close(fd);
        return -1;
    }
    return fd;
}

std::uint16_t port_of(int fd) {
    sockaddr_in sa{};
    socklen_t len = sizeof(sa);
    if (::getsockname(fd, reinterpret_cast<sockaddr*>(&sa), &len) != 0) {
        return 0;
    }
    return ntohs(sa.sin_port);
}

sockaddr_in loopback(std::uint16_t port) {
    sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    sa.sin_port = htons(port);
    return sa;
}

bool connect_loopback(int fd, std::uint16_t port) {
    auto const sa = loopback(port);
    return ::connect(fd, reinterpret_cast<sockaddr const*>(&sa), sizeof(sa)) == 0;
}

bool sendto_loopback(int fd, std::uint16_t port, char const* data, size_t size) {
    auto const sa = loopback(port);
    return ::sendto(fd, data, size, 0, reinterpret_cast<sockaddr const*>(&sa), sizeof(sa)) ==
           static_cast<ssize_t>(size);
}

// A UDP port that answers a datagram with ICMP port unreachable yet stays owned by the test: the
// socket bound to it is connected to another address, so a datagram from the client matches no
// socket on it. Owning it rules out another process taking the number in between. The socket is
// pointed at the client later when it has to speak from that port.
bool make_unreachable_from_clients(int fd) {
    return connect_loopback(fd, 1);
}

// The socket error after an ICMP error; poll does not consume it.
bool wait_for_socket_error(int fd) {
    pollfd pfd{fd, POLLERR, 0};
    return ::poll(&pfd, 1, 2000) == 1 and (pfd.revents & POLLERR);
}

template <class Client, class Predicate>
bool pump_until(Client& client, std::chrono::milliseconds timeout, Predicate&& predicate) {
    auto const deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        client.sync(1ms);
        if (predicate()) {
            return true;
        }
    }
    return predicate();
}

template <class Client> void pump_for(Client& client, std::chrono::milliseconds duration) {
    auto const deadline = std::chrono::steady_clock::now() + duration;
    while (std::chrono::steady_clock::now() < deadline) {
        client.sync(1ms);
    }
}

// A synchronous policy whose reads come from a semaphore fd (readable once per push) and whose
// writes fail on demand with an error code of their own: the shape of a send that fails on a
// device that still has data queued for reading.
struct fake_device {
    everest::lib::io::event::semaphore_fd readable; // an eventfd: readable per push, always writable
    std::atomic<int> pending_reads{0};
    std::atomic<int> tx_error{0};            // the next write fails with this errno, once
    std::atomic<int> tx_error_persistent{0}; // every write fails with this errno
    std::atomic<int> tx_calls{0};
    std::atomic<int> opens{0};
    void push_read() {
        ++pending_reads;
        readable.notify();
    }
};

class failing_write_policy {
public:
    using PayloadT = std::vector<std::uint8_t>;
    bool open(std::shared_ptr<fake_device> device) {
        m_device = std::move(device);
        if (m_device) {
            ++m_device->opens;
        }
        return static_cast<bool>(m_device);
    }
    bool tx(PayloadT&) {
        ++m_device->tx_calls;
        // Fails once with the armed error; the retry succeeds, as a send does once a transient
        // condition has passed. A revived connection would therefore stay up.
        if (auto const once = m_device->tx_error.exchange(0); once != 0) {
            m_error = once;
            return false;
        }
        if (auto const always = m_device->tx_error_persistent.load(); always != 0) {
            m_error = always;
            return false;
        }
        m_error = 0;
        return true;
    }
    bool rx(PayloadT& data) {
        if (m_device->pending_reads.load() <= 0) {
            return false;
        }
        --m_device->pending_reads;
        m_device->readable.read();
        data.assign(1, 0x2a);
        return true;
    }
    int get_fd() const {
        return m_device->readable.get_raw_fd();
    }
    int get_error() const {
        return m_error;
    }

private:
    std::shared_ptr<fake_device> m_device;
    int m_error{0};
};

using failing_write_client = everest::lib::io::event::fd_event_client<failing_write_policy>::type;

// An open that fails and leaves no errno behind. No policy in tree answers that way (a probe of
// the missing descriptor yields EBADF), so this synthetic one pins the backstop.
class silent_open_failure_policy {
public:
    using PayloadT = std::vector<std::uint8_t>;
    bool open(int) {
        return false;
    }
    bool tx(PayloadT&) {
        return false;
    }
    bool rx(PayloadT&) {
        return false;
    }
    int get_fd() const {
        return -1;
    }
    int get_error() const {
        return 0;
    }
};

using silent_open_failure_client = everest::lib::io::event::fd_event_client<silent_open_failure_policy>::type;

} // namespace

// The write-failure path: a send that fails with an error of its own, and a read that lands on the
// device right after. The device fd is re-queued ahead of the error eventfd written by the
// failure, so on the next pass the read would succeed first and put the state back to connected,
// the retried write would succeed too, and the failure would never be reported. (A read in the
// same pass as the failure does not show this: a successful read notifies the error eventfd as
// well, so its handler then runs right behind the device in that pass.)
TEST(fd_event_client_failed_connection_test, a_read_queued_behind_a_failed_write_does_not_revive_the_connection) {
    auto device = std::make_shared<fake_device>();
    failing_write_client client(device);
    std::atomic<int> up_edges{0};
    std::atomic<int> failure{0};
    std::atomic<int> reads{0};
    client.set_error_handler([&](int code, auto const&) {
        if (code == 0) {
            ++up_edges;
        } else if (failure.load() == 0) {
            failure = code;
        }
    });
    client.set_rx_handler([&](auto const&, auto&) { ++reads; });
    ASSERT_TRUE(pump_until(client, 2s, [&] { return up_edges.load() >= 1; }));

    // Single passes until the write has been attempted (the armed error is consumed by it); the
    // failure is on the error eventfd now but its handler has not run yet.
    device->tx_error = EIO;
    ASSERT_TRUE(client.tx(std::vector<std::uint8_t>{1}));
    for (int pass = 0; pass < 20 and device->tx_error.load() != 0; ++pass) {
        client.sync(1ms);
    }
    ASSERT_EQ(device->tx_error.load(), 0) << "the write was never attempted";
    ASSERT_EQ(failure.load(), 0) << "reported in the failing pass itself; the scenario did not arise";
    // A read lands behind the failure. It must not run.
    device->push_read();
    EXPECT_TRUE(pump_until(client, 2s, [&] { return failure.load() != 0; }))
        << "the failure was never reported: the queued read revived the connection";
    EXPECT_EQ(failure.load(), EIO);
    EXPECT_EQ(device->pending_reads.load(), 1) << "a read ran on the connection after its write had failed";
    EXPECT_EQ(reads.load(), 0);
}

// A send that fails with an errno of its own (ENETUNREACH, EHOSTUNREACH, EPERM, EMSGSIZE ...) never
// reaches SO_ERROR; the policies record it. This pins the client's side: a cause is reported once
// and nothing is retried on that connection. A policy that answered zero instead would have the
// client wait for writable, with the payload at the head of the queue and the fd writable, on every
// pass.
TEST(fd_event_client_failed_connection_test, a_write_that_fails_with_a_cause_is_reported_once_and_not_retried) {
    auto device = std::make_shared<fake_device>();
    failing_write_client client(device);
    std::atomic<int> up_edges{0};
    std::atomic<int> failure{0};
    client.set_error_handler([&](int code, auto const&) {
        if (code == 0) {
            ++up_edges;
        } else if (failure.load() == 0) {
            failure = code;
        }
    });
    client.set_rx_handler([&](auto const&, auto&) {});
    ASSERT_TRUE(pump_until(client, 2s, [&] { return up_edges.load() >= 1; }));

    device->tx_error_persistent = EHOSTUNREACH;
    ASSERT_TRUE(client.tx(std::vector<std::uint8_t>{1}));
    EXPECT_TRUE(pump_until(client, 2s, [&] { return failure.load() != 0; })) << "the failed write was never reported";
    EXPECT_EQ(failure.load(), EHOSTUNREACH);
    pump_for(client, 50ms);
    EXPECT_EQ(device->tx_calls.load(), 1) << "the write was retried on the failed connection";
}

// The policy half on a real socket. A send that fails synchronously never reaches SO_ERROR; the
// UDP policy records its errno and reports it. EMSGSIZE needs no privilege and no network to
// provoke: a datagram larger than IPv4 allows is rejected by the kernel at send time. (ENETUNREACH
// after a route disappears is the same path; it needs a route to take away and was checked by
// hand on a veth pair.)
TEST(fd_event_client_failed_connection_test, a_send_error_without_a_socket_error_is_reported_with_its_errno) {
    int const peer = bind_loopback_udp(0);
    ASSERT_GE(peer, 0);
    udp_client client("127.0.0.1", port_of(peer));
    std::atomic<int> up_edges{0};
    std::atomic<int> failure{0};
    client.set_error_handler([&](int code, auto const&) {
        if (code == 0) {
            ++up_edges;
        } else if (failure.load() == 0) {
            failure = code;
        }
    });
    client.set_rx_handler([&](udp_payload const&, auto&) {});
    ASSERT_TRUE(pump_until(client, 5s, [&] { return up_edges.load() >= 1; }));

    udp_payload too_big;
    too_big.buffer.assign(70000, 0x42); // above the 65507 bytes an IPv4 datagram can carry
    ASSERT_TRUE(client.tx(too_big));
    int passes = 0;
    while (failure.load() == 0 and passes < 200) {
        client.sync(1ms);
        ++passes;
    }
    EXPECT_EQ(failure.load(), EMSGSIZE) << "the send error was not reported with its own errno";
    EXPECT_LE(passes, 4) << "the failed send was retried before being reported";
    ::close(peer);
}

// The record describes the last operation only. A consumer may send through the raw handler,
// outside the client's queue, with no client call to report that failure; the record must not then
// be charged to the next operation the client does run. A read that goes through clears it, so a
// healthy socket is not failed for a send that happened earlier.
TEST(fd_event_client_failed_connection_test, a_send_error_recorded_outside_the_client_does_not_outlive_the_next_read) {
    int const peer = bind_loopback_udp(0);
    ASSERT_GE(peer, 0);
    auto const peer_port = port_of(peer);
    udp_client client("127.0.0.1", peer_port);
    std::atomic<int> up_edges{0};
    std::atomic<int> failure{0};
    std::atomic<int> reads{0};
    client.set_error_handler([&](int code, auto const&) {
        if (code == 0) {
            ++up_edges;
        } else if (failure.load() == 0) {
            failure = code;
        }
    });
    client.set_rx_handler([&](udp_payload const&, auto&) { ++reads; });
    ASSERT_TRUE(pump_until(client, 5s, [&] { return up_edges.load() >= 1; }));
    auto& raw = *client.get_raw_handler();

    udp_payload too_big;
    too_big.buffer.assign(70000, 0x42);
    EXPECT_FALSE(raw.tx(too_big));
    EXPECT_EQ(raw.get_error(), EMSGSIZE) << "the failed send was not recorded";

    char const pong[] = "pong";
    ASSERT_TRUE(sendto_loopback(peer, port_of(raw.get_fd()), pong, sizeof(pong)));
    EXPECT_TRUE(pump_until(client, 2s, [&] { return reads.load() >= 1; }));
    EXPECT_EQ(raw.get_error(), 0) << "the send error outlived the read that followed it";
    EXPECT_EQ(failure.load(), 0) << "a healthy socket was failed for an earlier send";
    ::close(peer);
}

// A pending socket error is handed out ahead of queued data by the read that finds it, and that
// read clears it. The record keeps what the read saw, so the cause is reported although SO_ERROR
// is clear by the time the client asks.
TEST(fd_event_client_failed_connection_test, a_socket_error_consumed_by_a_read_is_still_reported) {
    int const peer = bind_loopback_udp(0);
    ASSERT_GE(peer, 0);
    auto const peer_port = port_of(peer);
    udp_client client("127.0.0.1", peer_port);
    std::atomic<int> up_edges{0};
    client.set_error_handler([&](int code, auto const&) {
        if (code == 0) {
            ++up_edges;
        }
    });
    client.set_rx_handler([&](udp_payload const&, auto&) {});
    ASSERT_TRUE(pump_until(client, 5s, [&] { return up_edges.load() >= 1; }));
    auto& raw = *client.get_raw_handler();

    // The peer keeps its port but answers to nobody: the client's datagram matches no socket and
    // draws an ICMP port unreachable, which lands as the socket error.
    ASSERT_TRUE(connect_loopback(peer, 1));
    udp_payload ping;
    ping.buffer.assign(4, 0x42);
    ASSERT_TRUE(raw.tx(ping));
    pollfd pending{raw.get_fd(), POLLIN, 0};
    ASSERT_EQ(::poll(&pending, 1, 2000), 1) << "no socket error reached the client";
    ASSERT_TRUE(pending.revents & POLLERR);

    udp_payload received;
    EXPECT_FALSE(raw.rx(received));
    EXPECT_EQ(raw.get_error(), ECONNREFUSED) << "the error the read consumed was lost";
    ::close(peer);
}

// Back-pressure is retried on the next writable event: a full socket buffer, an interrupted call,
// and a full device transmit queue, which AF_PACKET and SocketCAN report as ENOBUFS. Anything else
// is the error the send leaves on its connection.
TEST(fd_event_client_failed_connection_test, back_pressure_errnos_are_not_send_failures) {
    using everest::lib::io::socket::is_send_backpressure;
    using everest::lib::io::socket::send_failure_code;
    EXPECT_TRUE(is_send_backpressure(EAGAIN));
    EXPECT_TRUE(is_send_backpressure(EWOULDBLOCK));
    EXPECT_TRUE(is_send_backpressure(EINTR));
    EXPECT_TRUE(is_send_backpressure(ENOBUFS));
    EXPECT_FALSE(is_send_backpressure(ENETUNREACH));
    EXPECT_FALSE(is_send_backpressure(EMSGSIZE));
    EXPECT_FALSE(is_send_backpressure(EPERM));
    EXPECT_FALSE(is_send_backpressure(0));
    EXPECT_EQ(send_failure_code(EAGAIN), 0);
    EXPECT_EQ(send_failure_code(ENOBUFS), 0);
    EXPECT_EQ(send_failure_code(EMSGSIZE), EMSGSIZE);
    EXPECT_EQ(send_failure_code(0), 0);
}

TEST(fd_event_client_failed_connection_test, a_read_queued_behind_a_socket_error_does_not_revive_the_connection) {
    int const peer = bind_loopback_udp(0);
    ASSERT_GE(peer, 0);
    auto const peer_port = port_of(peer);
    ASSERT_NE(peer_port, 0);
    ASSERT_TRUE(make_unreachable_from_clients(peer));

    udp_client client("127.0.0.1", peer_port);
    std::atomic<bool> ready{false};
    std::atomic<int> ready_count{0};
    std::atomic<int> up_edges{0};
    std::atomic<int> failure{0};
    client.set_on_ready_action([&] {
        ready = true;
        ++ready_count;
    });
    client.set_error_handler([&](int code, auto const&) {
        if (code == 0) {
            ++up_edges;
        } else if (failure.load() == 0) {
            failure = code;
            // What a consumer does with a failure: open the device again.
            client.reset();
        }
    });
    client.set_rx_handler([&](udp_payload const&, auto&) {});
    ASSERT_TRUE(pump_until(client, 5s, [&] { return ready.load(); }));
    // Drain the connect's up-edge (code 0) first. Left pending, its eventfd sits ahead of the
    // socket in epoll's ready list and would be dispatched before the queued read below, which is
    // not the order the kernel produces once the list is quiet: socket first, then the eventfd
    // written while handling it.
    ASSERT_TRUE(pump_until(client, 2s, [&] { return up_edges.load() >= 1; }));
    int const client_fd = client.get_raw_handler()->get_fd();
    ASSERT_GE(client_fd, 0);
    auto const client_port = port_of(client_fd);
    ASSERT_NE(client_port, 0);

    // Outside the loop: a datagram to the unreachable port draws the ICMP port unreachable, which
    // lands as the socket error.
    char const ping[] = "ping";
    ASSERT_EQ(::send(client_fd, ping, sizeof(ping), 0), static_cast<ssize_t>(sizeof(ping)));
    ASSERT_TRUE(wait_for_socket_error(client_fd)) << "no socket error after sending to an unreachable port";

    // Now a datagram from the connected peer address lands behind the error, so the client reads
    // it fine on the next pass.
    ASSERT_TRUE(connect_loopback(peer, client_port));
    char const pong[] = "pong";
    ASSERT_EQ(::send(peer, pong, sizeof(pong), 0), static_cast<ssize_t>(sizeof(pong)));

    // The first pass sees EPOLLERR and marks the connection failed; the second pass sees the queued
    // datagram on the same fd before the error-fd handler. The read must not revive the connection.
    EXPECT_TRUE(pump_until(client, 2s, [&] { return failure.load() != 0; }))
        << "the failure was never reported: the queued read revived the connection";
    EXPECT_EQ(failure.load(), ECONNREFUSED);
    // The reset from the handler reopened the device: a second up-edge.
    EXPECT_TRUE(pump_until(client, 2s, [&] { return ready_count.load() >= 2; })) << "no reconnect after the failure";

    ::close(peer);
}

// A consumer reset retires the handle at once, but its fd stays registered until the queued reopen
// runs, at the end of the next pass. An error the fd reports in that pass belongs to the peer the
// reset abandoned. Read as the client's, it would fail the fresh state the reset set, and with the
// error eventfd already in the same batch behind the fd (a read the pass before notifies it, and
// queued data keeps the fd ahead of it) the error handler would report the abandoned peer's error
// and queue a teardown of the connection the reset just opened. Neither happens: the error is not
// reported, and the reopen stands.
TEST(fd_event_client_failed_connection_test, an_error_on_a_fd_retired_by_reset_is_neither_reported_nor_fatal) {
    int const peer = bind_loopback_udp(0);
    ASSERT_GE(peer, 0);
    auto const peer_port = port_of(peer);
    ASSERT_NE(peer_port, 0);

    udp_client client("127.0.0.1", peer_port);
    std::atomic<int> ready_count{0};
    std::atomic<int> up_edges{0};
    std::atomic<int> failure{0};
    std::atomic<int> reads{0};
    client.set_on_ready_action([&] { ++ready_count; });
    client.set_error_handler([&](int code, auto const&) {
        if (code == 0) {
            ++up_edges;
        } else if (failure.load() == 0) {
            failure = code;
        }
    });
    client.set_rx_handler([&](udp_payload const&, auto&) { ++reads; });
    ASSERT_TRUE(pump_until(client, 5s, [&] { return ready_count.load() >= 1 and up_edges.load() >= 1; }));
    int const client_fd = client.get_raw_handler()->get_fd();
    ASSERT_GE(client_fd, 0);
    auto const client_port = port_of(client_fd);
    ASSERT_NE(client_port, 0);

    // Two datagrams from the peer. One pass reads the first: the read notifies the error eventfd,
    // which queues it behind the fd, and the second datagram keeps the fd in the ready list.
    char const data[] = "data";
    ASSERT_TRUE(sendto_loopback(peer, client_port, data, sizeof(data)));
    ASSERT_TRUE(sendto_loopback(peer, client_port, data, sizeof(data)));
    pollfd readable{client_fd, POLLIN, 0};
    ASSERT_EQ(::poll(&readable, 1, 2000), 1);
    client.sync(1ms);
    ASSERT_EQ(reads.load(), 1) << "the pass did not read exactly one datagram; the ordering below is not set up";

    // The peer goes away, and the error for a datagram sent to it lands on the fd.
    ASSERT_TRUE(make_unreachable_from_clients(peer));
    char const ping[] = "ping";
    ASSERT_EQ(::send(client_fd, ping, sizeof(ping), 0), static_cast<ssize_t>(sizeof(ping)));
    ASSERT_TRUE(wait_for_socket_error(client_fd)) << "no socket error after sending to an unreachable port";

    // The consumer gives the connection up before the pass that would report the error.
    client.reset();
    client.sync(1ms);
    EXPECT_EQ(failure.load(), 0) << "an error on the fd retired by the reset was reported as the new connection's";
    EXPECT_TRUE(pump_until(client, 2s, [&] { return ready_count.load() >= 2 and up_edges.load() >= 2; }))
        << "the reset's reopen was dropped";
    EXPECT_EQ(failure.load(), 0);
    EXPECT_EQ(reads.load(), 1) << "data queued on the retired connection was presented as the new one's";
    ::close(peer);
}

// A policy whose open fails without recording an errno. Zero would read as an up-edge on a
// connection with no fd: the consumer sees the client come up and every write is buffered for a
// drain that never comes. It is a failure.
TEST(fd_event_client_failed_connection_test, an_open_that_fails_without_an_errno_is_still_a_failure) {
    silent_open_failure_client client(0);
    std::atomic<int> up_edges{0};
    std::atomic<int> failure{0};
    client.set_error_handler([&](int code, auto const&) {
        if (code == 0) {
            ++up_edges;
        } else if (failure.load() == 0) {
            failure = code;
        }
    });
    client.set_rx_handler([&](auto const&, auto&) {});
    EXPECT_TRUE(pump_until(client, 2s, [&] { return failure.load() != 0; })) << "the failed open was never reported";
    EXPECT_EQ(up_edges.load(), 0) << "a failed open was reported as a connection that came up";
    EXPECT_FALSE(client.tx(std::vector<std::uint8_t>{1}))
        << "a payload was accepted for a connection that never opened";
}

// The TCP policy's record is cleared wherever it takes a descriptor. A caller holding a tcp_socket
// directly may reconnect through connect() alone, without setup(); the previous connection's send
// error must not be reported on the healthy replacement.
TEST(fd_event_client_failed_connection_test,
     a_tcp_socket_reconnected_through_connect_does_not_report_the_previous_send_error) {
    auto listener = everest::lib::io::socket::open_tcp_server_socket("127.0.0.1", 0, false);
    auto const port = port_of(listener);
    ASSERT_NE(port, 0);

    tcp_socket socket;
    ASSERT_TRUE(socket.setup("127.0.0.1", port, 1000));
    bool connected = false;
    socket.connect([&](bool ok, int) { connected = ok; });
    ASSERT_TRUE(connected);
    pollfd pending{listener, POLLIN, 0};
    ASSERT_EQ(::poll(&pending, 1, 2000), 1);
    int const accepted = ::accept(listener, nullptr, nullptr);
    ASSERT_GE(accepted, 0);
    ::close(accepted);

    // The peer's reset reaches the socket with the first or second write.
    std::vector<std::uint8_t> payload(16, 0x42);
    bool failed = false;
    for (int attempt = 0; attempt < 200 and not failed; ++attempt) {
        auto copy = payload;
        failed = not socket.tx(copy);
        if (not failed) {
            std::this_thread::sleep_for(1ms);
        }
    }
    ASSERT_TRUE(failed) << "no write failed after the peer closed";
    ASSERT_NE(socket.get_error(), 0);

    connected = false;
    socket.connect([&](bool ok, int) { connected = ok; });
    ASSERT_TRUE(connected);
    EXPECT_EQ(socket.get_error(), 0) << "the previous connection's send error was reported on the new one";
}

// The kernel aborts a connection whose peer keeps its window closed past TCP_USER_TIMEOUT and
// leaves ETIMEDOUT pending. A liveness peek consumes that error, so it has to be read first; a
// connection the kernel ended used to be reported as reset by a peer that reset nothing.
TEST(fd_event_client_failed_connection_test, a_tcp_socket_aborted_by_the_kernel_reports_the_kernels_errno) {
    auto listener = everest::lib::io::socket::open_tcp_server_socket("127.0.0.1", 0, false);
    int const window = 2048;
    ASSERT_EQ(::setsockopt(listener, SOL_SOCKET, SO_RCVBUF, &window, sizeof(window)), 0);
    auto const port = port_of(listener);
    ASSERT_NE(port, 0);

    tcp_socket socket;
    ASSERT_TRUE(socket.setup("127.0.0.1", port, 1000));
    bool connected = false;
    socket.connect([&](bool ok, int) { connected = ok; });
    ASSERT_TRUE(connected);
    ASSERT_TRUE(socket.set_user_timeout(1000));
    pollfd pending{listener, POLLIN, 0};
    ASSERT_EQ(::poll(&pending, 1, 2000), 1);
    int const accepted = ::accept(listener, nullptr, nullptr); // never read from: the window closes
    ASSERT_GE(accepted, 0);

    // Fill the peer's window and the local send buffer; back-pressure is not a failure.
    std::vector<std::uint8_t> payload(1024, 0x42);
    for (int attempt = 0; attempt < 10000; ++attempt) {
        auto copy = payload;
        if (not socket.tx(copy)) {
            break;
        }
    }
    ASSERT_EQ(socket.get_error(), 0) << "a closed window was reported as a failure";

    int error = 0;
    auto const deadline = std::chrono::steady_clock::now() + 8s;
    while (error == 0 and std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(50ms);
        error = socket.get_error();
    }
    EXPECT_EQ(error, ETIMEDOUT) << "the kernel's abort was not reported with its own errno";
    ::close(accepted);
}

// A peer that closes its side without an error is reported as ECONNRESET, the library's code for a
// stream peer closing (see fd_event_client::consume_poll_error).
TEST(fd_event_client_failed_connection_test, a_tcp_socket_whose_peer_closed_reports_a_reset) {
    auto listener = everest::lib::io::socket::open_tcp_server_socket("127.0.0.1", 0, false);
    auto const port = port_of(listener);
    ASSERT_NE(port, 0);

    tcp_socket socket;
    ASSERT_TRUE(socket.setup("127.0.0.1", port, 1000));
    bool connected = false;
    socket.connect([&](bool ok, int) { connected = ok; });
    ASSERT_TRUE(connected);
    pollfd pending{listener, POLLIN, 0};
    ASSERT_EQ(::poll(&pending, 1, 2000), 1);
    int const accepted = ::accept(listener, nullptr, nullptr);
    ASSERT_GE(accepted, 0);
    ASSERT_EQ(socket.get_error(), 0);
    ::close(accepted); // nothing unread on the peer: a FIN, not a reset

    pollfd closed{socket.get_fd(), POLLIN | POLLRDHUP, 0};
    ASSERT_EQ(::poll(&closed, 1, 2000), 1);
    EXPECT_EQ(socket.get_error(), ECONNRESET);
}

// A ready action registered and a reset issued between two syncs. The registration's queued action
// runs before the queued reopen, while the retired device is still in place; a ready fired then is
// for the connection the reset abandoned, moments before its teardown. The ready action belongs to
// the reopened connection, once.
TEST(fd_event_client_failed_connection_test,
     a_ready_action_registered_before_a_reset_fires_for_the_reopened_connection) {
    auto device = std::make_shared<fake_device>();
    failing_write_client client(device);
    std::atomic<int> up_edges{0};
    client.set_error_handler([&](int code, auto const&) {
        if (code == 0) {
            ++up_edges;
        }
    });
    client.set_rx_handler([&](auto const&, auto&) {});
    ASSERT_TRUE(pump_until(client, 2s, [&] { return up_edges.load() >= 1; }));
    ASSERT_EQ(device->opens.load(), 1);

    std::vector<int> ready_opens;
    client.set_on_ready_action([&] { ready_opens.push_back(device->opens.load()); });
    client.reset();
    ASSERT_TRUE(pump_until(client, 2s, [&] { return up_edges.load() >= 2 and not ready_opens.empty(); }));
    pump_for(client, 50ms);
    ASSERT_EQ(ready_opens.size(), 1u) << "the ready action fired " << ready_opens.size() << " times";
    EXPECT_EQ(ready_opens.front(), 2) << "the ready action fired for the connection the reset abandoned";
}
