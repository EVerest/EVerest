// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest

#include "tls_test_common.hpp"

#include <everest/io/event/fd_event_handler.hpp>
#include <everest/io/tls/tls_listener.hpp>
#include <everest/io/tls/tls_server.hpp>
#include <everest/io/tls/tls_server_socket.hpp>

#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

namespace {

namespace io = everest::lib::io;
namespace test = everest::lib::io::test;

const std::vector<std::uint8_t> kPayload = {'p', 'i', 'n', 'g'};

/// A blocking TLS client on a worker thread while the main thread pumps the listener's loop.
void run_round_trip(std::string const& bind_addr, bool ipv6_only) {
    // Declared first so it outlives every endpoint, whose destructor unregisters from it.
    io::event::fd_event_handler handler;

    io::tls::tls_listener listener(test::listener_test_config(bind_addr, ipv6_only));

    const auto port = listener.listen_port();
    ASSERT_GT(port, 0u) << "listener bound to port 0 unexpectedly";

    std::atomic<bool> server_rx_fired{false};
    std::atomic<std::uint16_t> captured_peer_port{0};
    // Must outlive the accept callback: the loop drives its handshake/rx/tx after registration.
    std::unique_ptr<io::tls::tls_server> server_conn;

    listener.set_accept_callback(
        [&](std::unique_ptr<io::tls::tls_server> srv, std::string /*ip*/, std::uint16_t peer_port) {
            captured_peer_port = peer_port;
            srv->set_rx_handler([&server_rx_fired](io::tls::tls_server_socket::PayloadT const& payload, auto& self) {
                server_rx_fired = true;
                self.tx(payload);
            });
            handler.register_event_handler(srv.get());
            server_conn = std::move(srv);
        });

    ASSERT_TRUE(handler.register_event_handler(&listener));

    std::atomic<bool> client_ok{false};
    std::string client_error;
    std::thread client_thread(
        [&]() { test::run_blocking_echo_client(bind_addr, port, kPayload, client_ok, client_error); });

    test::pump_until(
        handler, [&] { return server_rx_fired && client_ok; }, 5s);

    client_thread.join();

    EXPECT_TRUE(server_rx_fired) << "server-side rx handler did not fire within 5 seconds";
    EXPECT_TRUE(client_ok) << "client round-trip did not complete: " << client_error;
    EXPECT_GT(captured_peer_port.load(), 0u) << "peer_port not extracted from sockaddr_storage";
}

} // namespace

TEST(TlsListener, AcceptCallbackFires) {
    run_round_trip("127.0.0.1", false);
}

TEST(TlsListener, AcceptCallbackFiresIPv6) {
    run_round_trip("::1", true);
}

namespace {

// Makes the listener's accept4 fail the way it would under real load.
struct fd_exhauster {
    ~fd_exhauster() {
        release();
    }
    bool exhaust() {
        for (;;) {
            const int fd = ::open("/dev/null", O_RDONLY | O_CLOEXEC);
            if (fd < 0) {
                return errno == EMFILE || errno == ENFILE;
            }
            held.push_back(fd);
        }
    }
    void release() {
        for (const int fd : held) {
            ::close(fd);
        }
        held.clear();
    }
    std::vector<int> held;
};

// An ASSERT returning early must not leak a descriptor: this binary exhausts the table below.
struct scoped_fd {
    explicit scoped_fd(int fd_in) : fd{fd_in} {
    }
    scoped_fd(scoped_fd const&) = delete;
    scoped_fd& operator=(scoped_fd const&) = delete;
    ~scoped_fd() {
        reset();
    }
    void reset() {
        if (fd >= 0) {
            ::close(fd);
            fd = -1;
        }
    }
    int fd;
};

// Queue a TCP connection on the listener without accepting it.
int connect_raw(std::uint16_t port) {
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = ::inet_addr("127.0.0.1");
    addr.sin_port = htons(port);
    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        ::close(fd);
        return -1;
    }
    return fd;
}

} // namespace

// Plain HTTP into the TLS port fails the accept handshake. Two properties: the endpoint reports a
// reason text rather than dying silently, and the failure is contained to that connection.
//
// Not covered: fd recycling after the deferred close. One connection is in flight at a time, so the
// deferred remove always drains before the next accept.
TEST(TlsListener, handshake_failure_reports_and_loop_stays_healthy) {
    io::event::fd_event_handler ev;
    io::tls::tls_listener listener(test::listener_test_config());
    const auto port = listener.listen_port();
    ASSERT_GT(port, 0u);

    std::atomic<int> err_code{0};
    std::atomic<bool> got_error{false};
    std::string err_text;
    std::atomic<bool> second_rx_fired{false};
    // Dropped together at the end: the failed endpoint must not be destroyed while the loop is
    // still tearing its fd down.
    std::vector<std::unique_ptr<io::tls::tls_server>> conns;

    listener.set_accept_callback([&](std::unique_ptr<io::tls::tls_server> srv, std::string, std::uint16_t) {
        srv->set_error_handler([&](int code, std::string const& text) {
            if (code != 0) {
                err_code = code;
                err_text = text;
                got_error = true;
            }
        });
        srv->set_rx_handler([&second_rx_fired](io::tls::tls_server_socket::PayloadT const& payload, auto& self) {
            second_rx_fired = true;
            self.tx(payload);
        });
        ev.register_event_handler(srv.get());
        conns.push_back(std::move(srv));
    });
    ASSERT_TRUE(ev.register_event_handler(&listener));

    scoped_fd raw{connect_raw(port)};
    ASSERT_GE(raw.fd, 0) << "could not connect to the listener";
    const std::string junk = "GET / HTTP/1.1\r\n\r\n";
    ASSERT_EQ(::write(raw.fd, junk.data(), junk.size()), static_cast<ssize_t>(junk.size()));

    EXPECT_TRUE(test::pump_until(
        ev, [&] { return got_error.load(); }, 5s))
        << "the failed handshake was never reported";
    raw.reset();

    // libtls collapses every handshake error into result_t::closed, so a terminal accept surfaces as
    // ECONNRESET, not EPROTO. fail() substitutes the same ECONNRESET for a code of 0, so only the
    // reason text below proves the socket recorded the failure at all.
    EXPECT_EQ(err_code.load(), ECONNRESET) << "unexpected errno for a terminal accept handshake";
    EXPECT_FALSE(err_text.empty()) << "handshake failure reported no reason";

    std::atomic<bool> client_ok{false};
    std::string client_error;
    std::thread client_thread(
        [&]() { test::run_blocking_echo_client("127.0.0.1", port, kPayload, client_ok, client_error); });

    const bool served = test::pump_until(
        ev, [&] { return second_rx_fired && client_ok; }, 5s);
    client_thread.join();

    EXPECT_TRUE(served) << "the loop did not serve a good client after the failed handshake: " << client_error;
    EXPECT_TRUE(client_ok) << "client round-trip did not complete: " << client_error;
    // The echo alone does not say the listener accepted again: pin the second accept directly.
    EXPECT_EQ(conns.size(), 2u) << "the listener did not accept a second connection";

    ev.unregister_event_handler(&listener);
    conns.clear();
}

// Descriptor exhaustion leaves the connection queued, so the listen fd stays readable and the loop
// wakes on it every poll. The listener has to drain that connection to stay live.
TEST(TlsListener, descriptor_exhaustion_does_not_spin_the_loop) {
    io::event::fd_event_handler ev;
    io::tls::tls_listener listener(test::listener_test_config());
    const auto port = listener.listen_port();
    ASSERT_GT(port, 0u);

    std::atomic<int> error_calls{0};
    std::atomic<int> last_code{0};
    std::atomic<int> accepted{0};
    listener.set_error_handler([&](int code, std::string const&) {
        last_code = code;
        ++error_calls;
    });
    listener.set_accept_callback([&](std::unique_ptr<io::tls::tls_server>, std::string, std::uint16_t) { ++accepted; });
    ASSERT_TRUE(ev.register_event_handler(&listener));

    scoped_fd queued{connect_raw(port)};
    ASSERT_GE(queued.fd, 0) << "could not queue a connection on the listener";

    fd_exhauster hogs;
    ASSERT_TRUE(hogs.exhaust()) << "could not exhaust the descriptor table";

    constexpr int polls = 10;
    for (int i = 0; i < polls; ++i) {
        ev.poll(10ms);
        ev.run_actions();
    }

    hogs.release();

    EXPECT_GE(error_calls.load(), 1) << "descriptor exhaustion was never reported";
    EXPECT_LT(error_calls.load(), polls) << "the listener reported once per poll, so the queued "
                                            "connection was never drained and the loop is spinning";
    EXPECT_TRUE(last_code.load() == EMFILE || last_code.load() == ENFILE)
        << "unexpected errno reported: " << last_code.load();

    queued.reset();

    scoped_fd second{connect_raw(port)};
    ASSERT_GE(second.fd, 0);
    EXPECT_TRUE(test::pump_until(
        ev, [&] { return accepted.load() > 0; }, 5s))
        << "the listener never accepted again after recovering from exhaustion";
    second.reset();

    ev.unregister_event_handler(&listener);
}
