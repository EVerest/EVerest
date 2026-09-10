// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest

#include "tls_test_common.hpp"

#include <everest/io/event/fd_event_handler.hpp>
#include <everest/io/tls/tls_client.hpp>
// Impl header: these tests instantiate the base with stub sockets of their own.
#include <everest/io/tls/tls_endpoint_base_impl.hpp>
#include <everest/io/tls/tls_server.hpp>
#include <everest/io/tls/tls_server_socket.hpp>
#include <everest/tls/tls.hpp>

#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

namespace {

namespace test = everest::lib::io::test;

/// Exceeds the 4096-byte rx() chunk buffer, so rx() must drain the remainder via has_pending().
constexpr std::size_t kLargePayload = 10000;

} // namespace

TEST(TlsServer, HandshakeAndExchange) {
    int listen_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_GE(listen_fd, 0);

    int opt = 1;
    ::setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = ::inet_addr("127.0.0.1");
    addr.sin_port = 0;
    ASSERT_EQ(::bind(listen_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)), 0);
    ASSERT_EQ(::listen(listen_fd, 1), 0);

    sockaddr_in bound{};
    socklen_t bound_len = sizeof(bound);
    ASSERT_EQ(::getsockname(listen_fd, reinterpret_cast<sockaddr*>(&bound), &bound_len), 0);
    const uint16_t port = ntohs(bound.sin_port);
    const std::string port_str = std::to_string(port);

    auto cfg = test::server_test_config();
    cfg.host = "127.0.0.1";
    cfg.service = port_str.c_str();
    cfg.ipv6_only = false;
    cfg.socket = listen_fd; // bypass init_socket()

    tls::Server server;
    const auto state = server.init(cfg, nullptr);
    ASSERT_NE(state, tls::Server::state_t::init_needed)
        << "tls::Server::init failed: check that server_chain.pem etc. exist "
           "in the working directory (lib/everest/tls/tests in the build tree).";

    std::vector<uint8_t> msg(kLargePayload);
    for (std::size_t i = 0; i < msg.size(); ++i) {
        msg[i] = static_cast<uint8_t>(i & 0xFF);
    }
    std::atomic<bool> client_ok{false};
    std::string client_error;
    std::thread client_thread(
        [&]() { test::run_blocking_echo_client("127.0.0.1", port, msg, client_ok, client_error); });

    sockaddr peer{};
    socklen_t peer_len = sizeof(peer);
    int accepted_fd = ::accept(listen_fd, &peer, &peer_len);
    ASSERT_GE(accepted_fd, 0);

    char ip_buf[NI_MAXHOST] = "127.0.0.1";
    char svc_buf[NI_MAXSERV] = "0";
    ::getnameinfo(&peer, peer_len, ip_buf, sizeof ip_buf, svc_buf, sizeof svc_buf, NI_NUMERICHOST | NI_NUMERICSERV);

    auto conn = server.wrap_accepted_fd(accepted_fd, ip_buf, svc_buf);
    ASSERT_TRUE(conn) << "wrap_accepted_fd returned nullptr";

    // The handler must outlive the endpoint, whose dtor unregisters from it, so declare it first.
    everest::lib::io::event::fd_event_handler ev;
    everest::lib::io::tls::tls_server srv(std::move(conn));

    std::atomic<bool> rx_fired{false};
    std::vector<std::uint8_t> in_buf;

    srv.set_rx_handler(
        [&in_buf, &rx_fired](everest::lib::io::tls::tls_server_socket::PayloadT const& payload, auto& self) {
            rx_fired = true;
            in_buf.insert(in_buf.end(), payload.begin(), payload.end());
            self.tx(payload);
        });
    ASSERT_TRUE(ev.register_event_handler(&srv));

    // Stopping when the server merely RECEIVES would race the not-yet-drained echo write.
    test::pump_until(
        ev, [&] { return client_ok.load(); }, 5s);

    client_thread.join();
    ::close(listen_fd);

    EXPECT_TRUE(rx_fired) << "server-side rx handler did not fire";
    ASSERT_EQ(in_buf.size(), kLargePayload) << "rx() did not drain all buffered TLS records in one call";
    for (std::size_t i = 0; i < in_buf.size(); ++i) {
        ASSERT_EQ(in_buf[i], static_cast<uint8_t>(i & 0xFF)) << "payload corruption at index " << i;
    }
    EXPECT_TRUE(client_ok) << "Client error: " << client_error;
}

namespace {

/// Returns the client's connection fd, or -1 when the round trip did not complete.
int reset_peer_after_round_trip(everest::lib::io::event::fd_event_handler& ev, test::echo_listener& rig) {
    everest::lib::io::tls::tls_client client(test::client_test_config(), std::string("127.0.0.1"), rig.port(), 2000);
    std::atomic<bool> got_echo{false};
    const everest::lib::io::tls::tls_client_socket::PayloadT ping = {'p', 'i', 'n', 'g'};
    client.set_on_ready_action([&client, &ping]() { client.tx(ping); });
    client.set_rx_handler([&got_echo](everest::lib::io::tls::tls_client_socket::PayloadT const&,
                                      everest::lib::io::tls::tls_client_interface&) { got_echo = true; });
    if (not ev.register_event_handler(&client)) {
        return -1;
    }
    if (not test::pump_until(
            ev, [&] { return got_echo.load(); }, 5s)) {
        ev.unregister_event_handler(&client);
        return -1;
    }
    auto const& handle = client.get_raw_handler();
    const int client_fd = handle ? handle->get_fd() : -1;
    if (client_fd >= 0) {
        // Zero linger emits an RST instead of a graceful close_notify.
        struct linger lo {
            1, 0
        };
        ::setsockopt(client_fd, SOL_SOCKET, SO_LINGER, &lo, sizeof(lo));
    }
    ev.unregister_event_handler(&client);
    return client_fd;
}

} // namespace

// A peer RST leaves the socket with m_last_error 0, and every shipped consumer ignores the callback
// via `if (err != 0)`, so an unresolved 0 strands the endpoint dead but silent.
TEST(TlsServer, poll_error_delivers_nonzero_code) {
    everest::lib::io::event::fd_event_handler ev;
    auto rig = test::make_echo_listener(ev);
    ASSERT_TRUE(rig.registered);
    ASSERT_GT(rig.port(), 0u) << "listener bound to port 0 unexpectedly";

    std::atomic<bool> got_error{false};
    std::atomic<int> err_code{-1};
    ASSERT_GE(reset_peer_after_round_trip(ev, rig), 0) << "the round trip did not complete within 5 seconds";
    ASSERT_NE(rig.server_conn, nullptr) << "the listener never accepted a connection";
    rig.server_conn->set_error_handler([&](int err, std::string const&) {
        err_code = err;
        got_error = true;
    });

    ASSERT_TRUE(test::pump_until(
        ev, [&] { return got_error.load(); }, 5s))
        << "the server endpoint never reported the peer reset";
    EXPECT_NE(err_code.load(), 0) << "the endpoint error callback received code 0 on the poll-error path";
}

namespace {

struct stub_socket {
    int get_error() const {
        return error;
    }
    std::string const& get_error_string() const {
        return text;
    }
    std::function<void()> release_closer() {
        return []() {};
    }
    void close() {
    }
    int error{0};
    std::string text;
};

struct poll_error_probe : everest::lib::io::tls::tls_endpoint_base<stub_socket> {
    bool start(everest::lib::io::event::fd_event_handler&) override {
        return true;
    }
    void stop() override {
    }
    int probe(int fd) {
        return consume_poll_error(fd);
    }
    void set_socket_error(int error) {
        m_socket.error = error;
    }
};

// The connection-fd dispatch lambda names all of these, so they must exist even for a probe that
// never dispatches.
struct monitor_stub_socket {
    int get_error() const {
        return 0;
    }
    std::string const& get_error_string() const {
        return text;
    }
    bool handshake_complete() const {
        return true;
    }
    bool is_open() const {
        return true;
    }
    bool handshake_step() {
        return true;
    }
    bool rx(std::vector<std::uint8_t>&) {
        return false;
    }
    bool tx(std::vector<std::uint8_t>&) {
        return true;
    }
    everest::lib::io::event::poll_events desired_events() const {
        return everest::lib::io::event::poll_events::read;
    }
    void close() {
    }
    std::function<void()> release_closer() {
        return []() {};
    }
    std::string text;
};

struct monitor_probe : everest::lib::io::tls::tls_endpoint_base<monitor_stub_socket> {
    explicit monitor_probe(int fd) : m_start_fd(fd) {
    }
    bool start(everest::lib::io::event::fd_event_handler& handler) override {
        return register_connection_fd(handler, m_start_fd, everest::lib::io::event::poll_events::read);
    }
    void stop() override {
    }
    bool monitor(int fd) {
        return monitor_for(fd, everest::lib::io::event::poll_events::read);
    }
    int m_start_fd;
};

// Both success paths reset m_desired to read, mirroring tls_socket_base, so the stub cannot produce
// a completed operation that still wants the descriptor writable. That is why no loop-driven test
// reaches the routing table's `none` rows paired with a write want; the table walk sets m_desired
// directly instead. Should production stop resetting there, this stub has to follow.
struct steady_state_stub_socket {
    using poll_events = everest::lib::io::event::poll_events;

    int get_error() const {
        return 0;
    }
    std::string const& get_error_string() const {
        return text;
    }
    // Counted here because an rx/tx counter cannot see a loop spinning on an empty queue plus a
    // monitored write, which calls neither.
    bool handshake_complete() const {
        ++dispatches;
        return true;
    }
    bool is_open() const {
        return true;
    }
    bool handshake_step() {
        return true;
    }
    bool rx(std::vector<std::uint8_t>& buffer) {
        ++rx_calls;
        if (rx_succeeds) {
            buffer.assign({'r'});
            m_desired = poll_events::read;
            return true;
        }
        m_desired = rx_wants;
        return false;
    }
    bool tx(std::vector<std::uint8_t>& payload) {
        ++tx_calls;
        if (tx_succeeds) {
            payload.clear();
            m_desired = poll_events::read;
            return true;
        }
        m_desired = tx_wants;
        return false;
    }
    poll_events desired_events() const {
        return m_desired;
    }
    void close() {
    }
    std::function<void()> release_closer() {
        return []() {};
    }

    poll_events rx_wants{poll_events::write};
    poll_events tx_wants{poll_events::read};
    bool tx_succeeds{false};
    bool rx_succeeds{false};
    poll_events m_desired{poll_events::read};
    int rx_calls{0};
    int tx_calls{0};
    mutable int dispatches{0};
    std::string text;
};

struct steady_state_probe : everest::lib::io::tls::tls_endpoint_base<steady_state_stub_socket> {
    explicit steady_state_probe(int fd) : m_start_fd(fd) {
    }
    bool start(everest::lib::io::event::fd_event_handler& handler) override {
        return register_connection_fd(handler, m_start_fd, everest::lib::io::event::poll_events::read);
    }
    void stop() override {
    }
    steady_state_stub_socket& socket() {
        return m_socket;
    }
    std::size_t queue_depth() const {
        return m_tx_buffer.size();
    }
    bool request_the_mask_already_held() {
        return monitor_for(m_start_fd, everest::lib::io::event::poll_events::read);
    }
    using everest::lib::io::tls::tls_endpoint_base<steady_state_stub_socket>::direction;
    bool route(direction blocked) {
        return route_desired_events(m_start_fd, blocked);
    }
    bool monitors_read() const {
        return m_monitored_read;
    }
    bool monitors_write() const {
        return m_monitored_write;
    }
    void set_queue_depth(std::size_t depth) {
        while (m_tx_buffer.size() > depth) {
            m_tx_buffer.pop();
        }
        while (m_tx_buffer.size() < depth) {
            m_tx_buffer.push({'a'});
        }
    }
    int m_start_fd;
};

} // namespace

TEST(TlsEndpointBase, poll_error_prefers_the_socket_error) {
    int fds[2]{-1, -1};
    ASSERT_EQ(::pipe(fds), 0);
    poll_error_probe probe;
    probe.set_socket_error(EPIPE);

    EXPECT_EQ(probe.probe(fds[0]), EPIPE);

    ::close(fds[0]);
    ::close(fds[1]);
}

// The endpoint always sits on a real socket, so the last rung names a network errno rather than
// deriving one from the notification kind.
TEST(TlsEndpointBase, poll_error_without_a_code_reports_connection_reset) {
    // Not a socket, so getsockopt fails and the ladder falls through to its last rung.
    int fds[2]{-1, -1};
    ASSERT_EQ(::pipe(fds), 0);
    poll_error_probe probe;

    const int code = probe.probe(fds[0]);
    EXPECT_EQ(code, ECONNRESET) << "the fallback resolved to " << std::strerror(code);

    ::close(fds[0]);
    ::close(fds[1]);
}

namespace everest::lib::io::tls {
// Test seam without friendship: a derived class may name a protected member of its base, and the
// resulting pointer to member applies to any tls_server. Never instantiated.
struct endpoint_peek : tls_server {
    static int connection_fd(tls_server& endpoint) {
        return endpoint.*(&endpoint_peek::m_fd);
    }
    static int tx_notify_fd(tls_server& endpoint) {
        return (endpoint.*(&endpoint_peek::m_tx_notify)).get_raw_fd();
    }
    static std::size_t tx_queue_depth(tls_server& endpoint) {
        return (endpoint.*(&endpoint_peek::m_tx_buffer)).size();
    }
    static everest::lib::io::event::poll_events desired_events(tls_server& endpoint) {
        return (endpoint.*(&endpoint_peek::m_socket)).desired_events();
    }
    // The error report is deferred, so the callback cannot mark the moment fail() ran.
    static bool errored(tls_server& endpoint) {
        return endpoint.*(&endpoint_peek::m_errored);
    }
};
} // namespace everest::lib::io::tls

// ~tls_server() is the only backstop: fd_event_register_interface records nothing and defaults its
// destructor, so any further endpoint derived from it needs its own.
TEST(TlsServer, drop_registered_server_unregisters_fds) {
    everest::lib::io::event::fd_event_handler ev;
    auto rig = test::make_echo_listener(ev);
    ASSERT_TRUE(rig.registered);
    ASSERT_GT(rig.port(), 0u) << "listener bound to port 0 unexpectedly";

    int conn_fd = -1;
    int notify_fd = -1;
    {
        everest::lib::io::tls::tls_client client(test::client_test_config(), std::string("127.0.0.1"), rig.port(),
                                                 2000);
        std::atomic<bool> ready{false};
        client.set_on_ready_action([&ready]() { ready = true; });
        ASSERT_TRUE(ev.register_event_handler(&client));
        ASSERT_TRUE(test::pump_until(
            ev, [&] { return ready.load(); }, 5s))
            << "the handshake did not complete within 5 seconds";
        ASSERT_NE(rig.server_conn, nullptr) << "the listener never accepted a connection";

        conn_fd = everest::lib::io::tls::endpoint_peek::connection_fd(*rig.server_conn);
        notify_fd = everest::lib::io::tls::endpoint_peek::tx_notify_fd(*rig.server_conn);
        ASSERT_GE(conn_fd, 0);
        ASSERT_GE(notify_fd, 0);

        EXPECT_FALSE(ev.register_event_handler(rig.server_conn.get()))
            << "a second register on a live endpoint must be rejected, not restart it";

        // Drop WITHOUT unregister: the destructor has to clean up after itself.
        rig.server_conn.reset();
        ev.unregister_event_handler(&client);
    }

    // Nothing below opens new fds, so both numbers stay closed but unclaimed.
    for (int i = 0; i < 5; ++i) {
        ev.poll(10ms);
        ev.run_actions();
    }

    EXPECT_FALSE(ev.is_registered(conn_fd)) << "connection fd still registered after dropping the endpoint";
    EXPECT_FALSE(ev.is_registered(notify_fd)) << "tx-notify eventfd still registered after dropping the endpoint";
}

// A synchronous close would let an accept in the same poll batch recycle the fd number, and the
// queued remove-by-number would then strand the new connection.
TEST(TlsServer, endpoint_failure_defers_fd_close_until_after_unregister) {
    everest::lib::io::event::fd_event_handler ev;
    auto rig = test::make_echo_listener(ev);
    ASSERT_TRUE(rig.registered);
    ASSERT_GT(rig.port(), 0u) << "listener bound to port 0 unexpectedly";

    std::atomic<bool> server_error{false};
    ASSERT_GE(reset_peer_after_round_trip(ev, rig), 0) << "the round trip did not complete within 5 seconds";
    ASSERT_NE(rig.server_conn, nullptr) << "the listener never accepted a connection";
    const int conn_fd = everest::lib::io::tls::endpoint_peek::connection_fd(*rig.server_conn);
    ASSERT_GE(conn_fd, 0);
    rig.server_conn->set_error_handler([&server_error](int err, std::string const&) {
        if (err != 0) {
            server_error = true;
        }
    });

    bool checked_open = false;
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (std::chrono::steady_clock::now() < deadline && !checked_open) {
        ev.poll(50ms);
        // Probe the state, not the callback: the report is deferred and fires too late for this.
        if (everest::lib::io::tls::endpoint_peek::errored(*rig.server_conn)) {
            // fail() ran in this pass, before run_actions() drains the queued remove.
            EXPECT_NE(::fcntl(conn_fd, F_GETFD), -1)
                << "connection fd was closed synchronously in fail() (recycle race window)";
            EXPECT_FALSE(server_error.load())
                << "the error report reached the handler inside fail(), so a consumer that drops the "
                   "endpoint there resumes on freed memory";
            checked_open = true;
        }
        ev.run_actions();
    }
    ASSERT_TRUE(checked_open) << "the server endpoint never failed";

    EXPECT_EQ(::fcntl(conn_fd, F_GETFD), -1) << "connection fd still open after run_actions()";
    EXPECT_FALSE(ev.is_registered(conn_fd)) << "connection fd still registered after the deferred teardown";
    EXPECT_TRUE(server_error.load()) << "the deferred error report never reached the handler";
}

TEST(TlsEndpointBase, monitoring_an_unregistered_fd_reports_failure) {
    int fds[2]{-1, -1};
    ASSERT_EQ(::pipe(fds), 0);
    everest::lib::io::event::fd_event_handler ev;
    monitor_probe probe(fds[0]);
    ASSERT_TRUE(ev.register_event_handler(&probe)) << "the probe's own connection fd was rejected";

    EXPECT_FALSE(probe.monitor(fds[1])) << "monitoring a descriptor outside the handler reported success";

    ev.unregister_event_handler(&probe);
    ::close(fds[0]);
    ::close(fds[1]);
}

// epoll rejects a regular file with EPERM, so the connection fd never reaches the handler and
// registration must say so rather than report the unrelated success of its tx-notify fd.
TEST(TlsEndpointBase, register_reports_failure_when_the_connection_fd_is_rejected) {
    char path[] = "/tmp/everest_io_tls_endpoint_XXXXXX";
    const int file_fd = ::mkstemp(path);
    ASSERT_GE(file_fd, 0);
    ::unlink(path);

    everest::lib::io::event::fd_event_handler ev;
    monitor_probe probe(file_fd);

    EXPECT_FALSE(ev.register_event_handler(&probe))
        << "registration reported success though the connection fd was rejected";

    ev.unregister_event_handler(&probe);
    ::close(file_fd);
}

// A post-handshake SSL_read may want the descriptor writable (a rekey has a record to flush). The
// descriptor is held unwritable for the whole window on purpose, which pins the routing decision
// rather than what happens after it.
TEST(TlsEndpointBase, rx_wanting_write_routes_the_monitored_event) {
    int fds[2]{-1, -1};
    ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
    ASSERT_NE(::fcntl(fds[0], F_SETFL, O_NONBLOCK), -1);
    ASSERT_NE(::fcntl(fds[1], F_SETFL, O_NONBLOCK), -1);

    const std::vector<char> filler(4096, 'f');
    std::size_t filled = 0;
    while (::write(fds[0], filler.data(), filler.size()) > 0) {
        filled += filler.size();
    }
    ASSERT_EQ(errno, EAGAIN);
    ASSERT_GT(filled, 0u) << "the socketpair accepted nothing, so it was never writable";

    everest::lib::io::event::fd_event_handler ev;
    steady_state_probe probe(fds[0]);
    ASSERT_TRUE(ev.register_event_handler(&probe));

    // The stub never consumes it, so the descriptor stays readable while read is monitored.
    const char byte = 'x';
    ASSERT_EQ(::write(fds[1], &byte, 1), 1);

    for (int i = 0; i < 10; ++i) {
        ev.poll(10ms);
        ev.run_actions();
    }

    EXPECT_EQ(probe.socket().rx_calls, 1)
        << "rx() answered want_write but read stayed monitored, so the loop re-drove it " << probe.socket().rx_calls
        << " times";

    // A tx() must not re-arm read behind the routing decision that just dropped it: rx() still
    // answers want_write, so read back on is the same re-drive.
    ASSERT_TRUE(probe.tx({'a'}));
    for (int i = 0; i < 10; ++i) {
        ev.poll(10ms);
        ev.run_actions();
    }
    EXPECT_EQ(probe.socket().rx_calls, 1) << "tx() re-armed read on a descriptor whose rx() answers want_write, so "
                                             "the loop re-drove the receive "
                                          << probe.socket().rx_calls << " times";

    ev.unregister_event_handler(&probe);
    ::close(fds[0]);
    ::close(fds[1]);
}

// A TLS record split across TCP segments makes SSL_read answer want_read on a descriptor that did
// report readable. This is the common case, so dropping POLLOUT here strands queues routinely.
TEST(TlsEndpointBase, a_receive_would_block_keeps_a_queued_payload_monitored) {
    int fds[2]{-1, -1};
    ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
    ASSERT_NE(::fcntl(fds[0], F_SETFL, O_NONBLOCK), -1);
    ASSERT_NE(::fcntl(fds[1], F_SETFL, O_NONBLOCK), -1);

    // Fill the endpoint's send buffer so its descriptor is readable but not writable, which keeps
    // the read and the write dispatch in separate poll passes.
    const std::vector<char> filler(4096, 'f');
    std::size_t filled = 0;
    while (::write(fds[0], filler.data(), filler.size()) > 0) {
        filled += filler.size();
    }
    ASSERT_EQ(errno, EAGAIN);
    ASSERT_GT(filled, 0u) << "the socketpair accepted nothing, so it was never writable";

    everest::lib::io::event::fd_event_handler ev;
    steady_state_probe probe(fds[0]);
    probe.socket().rx_wants = everest::lib::io::event::poll_events::read;
    probe.socket().tx_succeeds = true;
    ASSERT_TRUE(ev.register_event_handler(&probe));
    ASSERT_TRUE(probe.tx({'a'}));

    const char byte = 'x';
    ASSERT_EQ(::write(fds[1], &byte, 1), 1);
    for (int i = 0; i < 10; ++i) {
        ev.poll(10ms);
        ev.run_actions();
    }
    ASSERT_GT(probe.socket().rx_calls, 0) << "the receive never ran";
    ASSERT_EQ(probe.socket().tx_calls, 0) << "the descriptor became writable too early to prove anything";

    std::vector<char> sink(filler.size());
    while (::read(fds[1], sink.data(), sink.size()) > 0) {
    }
    ASSERT_EQ(errno, EAGAIN);
    for (int i = 0; i < 10; ++i) {
        ev.poll(10ms);
        ev.run_actions();
    }

    EXPECT_EQ(probe.socket().tx_calls, 1) << "the queued payload was stranded: a receive would-block "
                                             "dropped POLLOUT even though the tx buffer was not empty";

    ev.unregister_event_handler(&probe);
    ::close(fds[0]);
    ::close(fds[1]);
}

// The mirror case: a post-handshake SSL_write may want the descriptor readable.
TEST(TlsEndpointBase, tx_wanting_read_routes_the_monitored_event) {
    int fds[2]{-1, -1};
    ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
    everest::lib::io::event::fd_event_handler ev;
    steady_state_probe probe(fds[0]);
    ASSERT_TRUE(ev.register_event_handler(&probe));
    // Nothing is written to fds[1], so read never fires and only the tx path runs.
    ASSERT_TRUE(probe.tx({'a'}));

    for (int i = 0; i < 10; ++i) {
        ev.poll(10ms);
        ev.run_actions();
    }

    EXPECT_EQ(probe.socket().tx_calls, 1)
        << "tx() answered want_read but write stayed monitored, so the loop re-drove it " << probe.socket().tx_calls
        << " times";

    ev.unregister_event_handler(&probe);
    ::close(fds[0]);
    ::close(fds[1]);
}

// An SSL_write answering want_write means only a full kernel send buffer and says nothing about the
// receive side. Dropping POLLIN there deadlocks against a peer that writes before it reads.
TEST(TlsEndpointBase, a_send_would_block_keeps_read_monitored) {
    int fds[2]{-1, -1};
    ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
    everest::lib::io::event::fd_event_handler ev;
    steady_state_probe probe(fds[0]);
    probe.socket().rx_wants = everest::lib::io::event::poll_events::read;
    probe.socket().tx_wants = everest::lib::io::event::poll_events::write;
    ASSERT_TRUE(ev.register_event_handler(&probe));
    ASSERT_TRUE(probe.tx({'a'}));

    for (int i = 0; i < 10; ++i) {
        ev.poll(10ms);
        ev.run_actions();
    }
    ASSERT_GT(probe.socket().tx_calls, 0) << "the send never ran, so nothing blocked on writability";
    ASSERT_EQ(probe.socket().m_desired, everest::lib::io::event::poll_events::write)
        << "the send did not leave the socket wanting the descriptor writable";
    ASSERT_EQ(probe.socket().rx_calls, 0) << "the receive ran before the peer sent anything";

    const char byte = 'x';
    ASSERT_EQ(::write(fds[1], &byte, 1), 1);
    for (int i = 0; i < 10; ++i) {
        ev.poll(10ms);
        ev.run_actions();
    }

    EXPECT_GT(probe.socket().rx_calls, 0)
        << "a send blocked on writability dropped POLLIN, so the peer's payload was never received";

    ev.unregister_event_handler(&probe);
    ::close(fds[0]);
    ::close(fds[1]);
}

// A completed send leaves the socket's need at read, the same value a send parked on readability
// reports. Reading the completed one as parked drops POLLOUT, and only tx() re-arms it.
TEST(TlsEndpointBase, a_completed_send_keeps_the_rest_of_the_queue_monitored) {
    int fds[2]{-1, -1};
    ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
    everest::lib::io::event::fd_event_handler ev;
    steady_state_probe probe(fds[0]);
    probe.socket().tx_succeeds = true;
    ASSERT_TRUE(ev.register_event_handler(&probe));
    ASSERT_TRUE(probe.tx({'a'}));
    ASSERT_TRUE(probe.tx({'b'}));

    for (int i = 0; i < 10; ++i) {
        ev.poll(10ms);
        ev.run_actions();
    }

    EXPECT_EQ(probe.queue_depth(), 0u) << "the drain stopped after the first payload: a completed send dropped "
                                          "POLLOUT while the queue still held "
                                       << probe.queue_depth();
    EXPECT_EQ(probe.socket().tx_calls, 2);

    ev.unregister_event_handler(&probe);
    ::close(fds[0]);
    ::close(fds[1]);
}

// The write dispatch calls no socket operation on an empty queue, so nothing can move the desired
// event off write.
TEST(TlsEndpointBase, an_unsatisfiable_write_want_does_not_spin_the_loop) {
    int fds[2]{-1, -1};
    ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
    ASSERT_NE(::fcntl(fds[0], F_SETFL, O_NONBLOCK), -1);
    everest::lib::io::event::fd_event_handler ev;
    steady_state_probe probe(fds[0]);
    probe.socket().rx_wants = everest::lib::io::event::poll_events::write;
    ASSERT_TRUE(ev.register_event_handler(&probe));

    const char byte = 'x';
    ASSERT_EQ(::write(fds[1], &byte, 1), 1);
    for (int i = 0; i < 10 && probe.socket().rx_calls == 0; ++i) {
        ev.poll(10ms);
        ev.run_actions();
    }
    ASSERT_GT(probe.socket().rx_calls, 0) << "the receive never ran";
    ASSERT_EQ(probe.socket().m_desired, everest::lib::io::event::poll_events::write);
    ASSERT_EQ(probe.queue_depth(), 0u);

    // Consume the byte the stub never read, so read alone would leave the loop idle in epoll_wait.
    char sink = 0;
    ASSERT_EQ(::read(fds[0], &sink, 1), 1);

    // One write dispatch is due: the receive's want_write put POLLOUT on, and the empty-queue write
    // dispatch is what takes it back off.
    ASSERT_TRUE(ev.poll(200ms)) << "the POLLOUT the receive asked for never fired";
    ev.run_actions();

    // Asserted on the loop, not a socket-call count, which caching the handshake state would zero.
    // poll() reports false only when it sat in epoll_wait for the whole timeout.
    probe.socket().dispatches = 0;
    EXPECT_FALSE(ev.poll(200ms)) << "the loop returned from epoll_wait with nothing to do ("
                                 << probe.socket().dispatches
                                 << " connection-fd dispatches): POLLOUT stayed monitored on an always-writable "
                                    "descriptor with an empty tx buffer";

    ev.unregister_event_handler(&probe);
    ::close(fds[0]);
    ::close(fds[1]);
}

// A send that answered want_read parks the queue behind readability and drops POLLOUT. Only routing
// puts it back, since the tx-notify fires from tx() alone.
TEST(TlsEndpointBase, a_receive_success_restores_a_queued_payload) {
    int fds[2]{-1, -1};
    ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
    everest::lib::io::event::fd_event_handler ev;
    steady_state_probe probe(fds[0]);
    probe.socket().tx_wants = everest::lib::io::event::poll_events::read;
    ASSERT_TRUE(ev.register_event_handler(&probe));
    ASSERT_TRUE(probe.tx({'a'}));

    for (int i = 0; i < 10; ++i) {
        ev.poll(10ms);
        ev.run_actions();
    }
    ASSERT_EQ(probe.socket().tx_calls, 1) << "the send did not park on readability";
    ASSERT_EQ(probe.queue_depth(), 1u);

    // The rx handler is unset, so nothing replies and routing is the only thing that can recover.
    probe.socket().rx_succeeds = true;
    probe.socket().tx_succeeds = true;
    const char byte = 'x';
    ASSERT_EQ(::write(fds[1], &byte, 1), 1);
    for (int i = 0; i < 10; ++i) {
        ev.poll(10ms);
        ev.run_actions();
    }

    ASSERT_GT(probe.socket().rx_calls, 0) << "the receive never ran";
    EXPECT_EQ(probe.queue_depth(), 0u) << "a successful receive left the queued payload stranded: POLLOUT was "
                                          "dropped by the send would-block and never restored";

    ev.unregister_event_handler(&probe);
    ::close(fds[0]);
    ::close(fds[1]);
}

// A descriptor monitored for nothing never dispatches again, reports no error and hits no timeout.
// No row reaching the loop would notice an edit breaking that, so the table is walked directly.
TEST(TlsEndpointBase, no_routing_decision_leaves_the_descriptor_unmonitored) {
    using poll_events = everest::lib::io::event::poll_events;
    using dir = steady_state_probe::direction;

    int fds[2]{-1, -1};
    ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
    everest::lib::io::event::fd_event_handler ev;
    steady_state_probe probe(fds[0]);
    ASSERT_TRUE(ev.register_event_handler(&probe));

    for (auto queued : {std::size_t{0}, std::size_t{1}}) {
        for (auto wants : {poll_events::read, poll_events::write}) {
            for (auto who : {dir::receive, dir::send, dir::none}) {
                probe.set_queue_depth(queued);
                probe.socket().m_desired = wants;
                const std::string row = "queued=" + std::to_string(queued) +
                                        " wants_write=" + std::to_string(wants == poll_events::write) +
                                        " blocked=" + std::to_string(static_cast<int>(who));
                EXPECT_TRUE(probe.route(who)) << "the routing decision was rejected: " << row;
                EXPECT_TRUE(probe.monitors_read() or probe.monitors_write())
                    << "the descriptor was left monitored for nothing, so nothing will ever dispatch it "
                       "again: "
                    << row;
            }
        }
    }

    probe.set_queue_depth(0);
    ev.unregister_event_handler(&probe);
    ::close(fds[0]);
    ::close(fds[1]);
}

// The mask cache answers an unchanged request without reaching the handler, but only while the
// registration it describes exists. Otherwise a future caller ends up with a descriptor monitored
// for nothing while the endpoint reports healthy.
TEST(TlsEndpointBase, a_mask_unchanged_request_fails_once_the_fd_is_gone) {
    int fds[2]{-1, -1};
    ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
    everest::lib::io::event::fd_event_handler ev;
    steady_state_probe probe(fds[0]);
    ASSERT_TRUE(ev.register_event_handler(&probe));

    ASSERT_TRUE(probe.request_the_mask_already_held()) << "the cache rejected the mask the handler holds";

    // Behind the endpoint's back, so the cache describes a descriptor the handler no longer knows.
    ASSERT_TRUE(ev.remove_event_handler(fds[0]));

    EXPECT_FALSE(probe.request_the_mask_already_held())
        << "the mask cache reported success for a descriptor the handler no longer holds";

    ev.unregister_event_handler(&probe);
    ::close(fds[0]);
    ::close(fds[1]);
}

// The tx-notify eventfd is one-shot, drained by the wrapper before the callback runs, so a monitor
// request that fails there is never retried.
TEST(TlsEndpointBase, a_failed_tx_notify_monitor_fails_the_endpoint) {
    int fds[2]{-1, -1};
    ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
    everest::lib::io::event::fd_event_handler ev;
    steady_state_probe probe(fds[0]);
    ASSERT_TRUE(ev.register_event_handler(&probe));

    std::atomic<int> err_code{0};
    probe.set_error_handler([&err_code](int err, std::string const&) { err_code = err; });
    // Behind the endpoint's back, so the notify callback's modify request finds no entry.
    ASSERT_TRUE(ev.remove_event_handler(fds[0]));
    ASSERT_TRUE(probe.tx({'a'}));

    for (int i = 0; i < 10 && err_code.load() == 0; ++i) {
        ev.poll(10ms);
        ev.run_actions();
    }

    EXPECT_EQ(err_code.load(), EPROTO) << "a failed tx-notify monitor request left the endpoint reporting healthy "
                                          "with a queue nothing will drain";

    ev.unregister_event_handler(&probe);
    ::close(fds[0]);
    ::close(fds[1]);
}

namespace {

/// Blocking libtls peer that handshakes and then stops reading, so the server's send buffer backs up
/// and SSL_write starts answering want_write. Reads only once `drain` is set. Its trailer, sent once
/// it has everything, proves the endpoint is still monitored for read after the queue drained.
struct stalled_peer {
    std::atomic<bool> handshaked{false};
    std::atomic<bool> probe{false};
    std::atomic<bool> probe_sent{false};
    std::atomic<bool> drain{false};
    std::atomic<bool> failed{false};
    std::atomic<bool> finished{false};
    std::atomic<bool> close_now{false};
    std::size_t expected{0};
    std::string error;
    std::vector<std::uint8_t> received;
};

const std::vector<std::uint8_t> peer_trailer = {'p', 'o', 'n', 'g'};

const std::vector<std::uint8_t> peer_probe = {'p', 'r', 'o', 'b', 'e'};

void run_stalled_peer(std::uint16_t port, stalled_peer& peer) {
    struct done_guard {
        stalled_peer& p;
        ~done_guard() {
            p.finished = true;
        }
    } guard{peer};

    ::tls::Client client;
    ::tls::Client::config_t ccfg;
    ccfg.cipher_list = "ECDHE-ECDSA-AES128-SHA256";
    ccfg.verify_locations_file = "server_root_cert.pem";
    ccfg.io_timeout_ms = 5000;
    ccfg.verify_server = true;
    if (!client.init(ccfg)) {
        peer.error = "client.init failed";
        peer.failed = true;
        return;
    }

    const std::string port_str = std::to_string(port);
    auto conn = client.connect("127.0.0.1", port_str.c_str(), false, 5000);
    if (!conn || conn->connect() != ::tls::Connection::result_t::success) {
        peer.error = "TLS handshake failed on peer side";
        peer.failed = true;
        return;
    }

    // Do not shrink the receive buffer to back the endpoint up sooner: that throttles the drain to a
    // few KiB per poll and the test stops being about the endpoint. Not reading is enough.
    peer.handshaked = true;

    // Sending while still not reading is the shape that deadlocks an endpoint which drops POLLIN
    // whenever its own send wants the descriptor writable.
    while (!peer.probe.load() && !peer.drain.load()) {
        std::this_thread::sleep_for(2ms);
    }
    if (peer.probe.load()) {
        std::size_t sent = 0;
        if (conn->write(reinterpret_cast<const std::byte*>(peer_probe.data()), peer_probe.size(), sent, 5000) !=
            ::tls::Connection::result_t::success) {
            peer.error = "peer probe write failed";
            peer.failed = true;
            return;
        }
        peer.probe_sent = true;
    }

    while (!peer.drain.load()) {
        std::this_thread::sleep_for(2ms);
    }

    while (peer.received.size() < peer.expected) {
        std::byte buf[8192]{};
        std::size_t nread = 0;
        if (conn->read(buf, sizeof buf, nread, 5000) != ::tls::Connection::result_t::success) {
            peer.error = "peer read failed after " + std::to_string(peer.received.size()) + " bytes";
            peer.failed = true;
            return;
        }
        peer.received.insert(peer.received.end(), reinterpret_cast<std::uint8_t*>(buf),
                             reinterpret_cast<std::uint8_t*>(buf) + nread);
    }

    std::size_t written = 0;
    if (conn->write(reinterpret_cast<const std::byte*>(peer_trailer.data()), peer_trailer.size(), written, 5000) !=
        ::tls::Connection::result_t::success) {
        peer.error = "peer trailer write failed";
        peer.failed = true;
        return;
    }

    // A close_notify here would fail the endpoint before the test can tell a stranded read apart
    // from a peer that simply hung up.
    while (!peer.close_now.load()) {
        std::this_thread::sleep_for(2ms);
    }
    conn->shutdown();
}

/// Keyed on its index, so a reordered delivery is visible in the byte stream, not just the length.
std::vector<std::uint8_t> indexed_payload(std::size_t index, std::size_t size) {
    std::vector<std::uint8_t> payload(size);
    for (std::size_t i = 0; i < size; ++i) {
        payload[i] = static_cast<std::uint8_t>((index * 31 + i) & 0xFF);
    }
    return payload;
}

/// Returns the accepted endpoint's connection fd, or -1 without a completed handshake on both sides.
int connect_stalled_peer(everest::lib::io::event::fd_event_handler& ev, test::echo_listener& rig, stalled_peer& peer) {
    if (not test::pump_until(
            ev, [&] { return peer.handshaked.load() || peer.failed.load(); }, 5s)) {
        return -1;
    }
    if (peer.failed.load()) {
        return -1;
    }
    // The handshake completed with the peer's connect(), but the endpoint still needs a loop tick.
    if (not test::pump_until(
            ev, [&] { return rig.server_conn != nullptr; }, 5s)) {
        return -1;
    }
    return everest::lib::io::tls::endpoint_peek::connection_fd(*rig.server_conn);
}

} // namespace

// libtls never enables SSL_MODE_ENABLE_PARTIAL_WRITE, so a want_write mid-payload means the front
// payload must be retried byte for byte, and POLLOUT must stay monitored while it is.
TEST(TlsServer, backpressure_preserves_payload_order) {
    everest::lib::io::event::fd_event_handler ev;
    auto rig = test::make_echo_listener(ev);
    ASSERT_TRUE(rig.registered);
    ASSERT_GT(rig.port(), 0u) << "listener bound to port 0 unexpectedly";

    stalled_peer peer;
    std::thread worker([&]() { run_stalled_peer(rig.port(), peer); });
    struct joiner {
        stalled_peer& p;
        std::thread& t;
        ~joiner() {
            p.drain = true;
            p.close_now = true;
            if (t.joinable()) {
                t.join();
            }
        }
    } join_worker{peer, worker};

    const int conn_fd = connect_stalled_peer(ev, rig, peer);
    ASSERT_GE(conn_fd, 0) << "the stalled peer never reached a live connection: " << peer.error;

    // The kernel floors this well above the requested value; it only has to be small enough that one
    // payload cannot be absorbed in a single write.
    int sndbuf = 2048;
    ASSERT_EQ(::setsockopt(conn_fd, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf)), 0);

    std::atomic<bool> errored{false};
    rig.server_conn->set_error_handler([&errored](int, std::string const&) { errored = true; });
    std::vector<std::uint8_t> trailer;
    rig.server_conn->set_rx_handler(
        [&trailer](everest::lib::io::tls::tls_server_socket::PayloadT const& payload, auto&) {
            trailer.insert(trailer.end(), payload.begin(), payload.end());
        });

    // Too large for the shrunken send buffer, so SSL_write answers want_write mid-payload rather
    // than the descriptor simply never becoming writable.
    constexpr std::size_t payload_size = 64 * 1024;
    constexpr std::size_t min_queued = 3;
    constexpr std::size_t max_payloads = 64;
    std::vector<std::uint8_t> expected;
    std::size_t queued = 0;
    while (everest::lib::io::tls::endpoint_peek::tx_queue_depth(*rig.server_conn) < min_queued &&
           queued < max_payloads) {
        auto payload = indexed_payload(queued, payload_size);
        ASSERT_TRUE(rig.server_conn->tx(payload)) << "tx() rejected payload " << queued;
        expected.insert(expected.end(), payload.begin(), payload.end());
        ++queued;
        ev.poll(1ms);
        ev.run_actions();
    }
    ASSERT_GE(everest::lib::io::tls::endpoint_peek::tx_queue_depth(*rig.server_conn), min_queued)
        << "the send path never backed up after " << queued << " payloads of " << payload_size << " bytes";
    ASSERT_FALSE(errored.load()) << "a backed-up send buffer failed the endpoint instead of queueing";
    // Without this the test also passes on a peer that merely stopped being writable, which never
    // reaches SSL_write and pins neither the retry nor the re-arm.
    ASSERT_EQ(everest::lib::io::tls::endpoint_peek::desired_events(*rig.server_conn),
              everest::lib::io::event::poll_events::write)
        << "SSL_write never answered want_write, so the payload was never retried mid-write";

    // The trailer arrives only after the peer read everything, so it proves both that the queue
    // drained and that the endpoint went back to monitoring read.
    peer.expected = expected.size();
    peer.drain = true;
    ASSERT_TRUE(test::pump_until(
        ev, [&] { return trailer.size() >= peer_trailer.size() || peer.failed.load(); }, 30s))
        << "the peer's trailer never arrived, so the endpoint stopped monitoring read once the "
        << "backed-up tx queue drained"
        << " [queued=" << queued << " expected=" << expected.size()
        << " depth=" << everest::lib::io::tls::endpoint_peek::tx_queue_depth(*rig.server_conn)
        << " desired=" << static_cast<int>(everest::lib::io::tls::endpoint_peek::desired_events(*rig.server_conn))
        << "]";
    ASSERT_FALSE(peer.failed.load()) << peer.error;
    EXPECT_EQ(trailer, peer_trailer);
    EXPECT_FALSE(errored.load()) << "the endpoint failed while draining a backed-up queue";

    peer.close_now = true;
    ASSERT_TRUE(test::pump_until(
        ev, [&] { return peer.finished.load(); }, 10s));
    worker.join();

    ASSERT_FALSE(peer.failed.load()) << peer.error;
    ASSERT_EQ(peer.received.size(), expected.size()) << "the drained byte count does not match what was accepted";
    EXPECT_EQ(peer.received, expected) << "the drained bytes are not the accepted payloads in order";
}

// The full-duplex shape every request/response client has: write, then read. An endpoint that stops
// monitoring read while its own send is backed up goes deaf, and against a peer itself blocked
// writing the queue never drains: no error fires and the connection is frozen for good.
TEST(TlsServer, a_backed_up_send_still_receives) {
    everest::lib::io::event::fd_event_handler ev;
    auto rig = test::make_echo_listener(ev);
    ASSERT_TRUE(rig.registered);
    ASSERT_GT(rig.port(), 0u) << "listener bound to port 0 unexpectedly";

    stalled_peer peer;
    std::thread worker([&]() { run_stalled_peer(rig.port(), peer); });
    struct joiner {
        stalled_peer& p;
        std::thread& t;
        ~joiner() {
            p.probe = true;
            p.drain = true;
            p.close_now = true;
            if (t.joinable()) {
                t.join();
            }
        }
    } join_worker{peer, worker};

    const int conn_fd = connect_stalled_peer(ev, rig, peer);
    ASSERT_GE(conn_fd, 0) << "the stalled peer never reached a live connection: " << peer.error;

    int sndbuf = 2048;
    ASSERT_EQ(::setsockopt(conn_fd, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf)), 0);

    std::atomic<bool> errored{false};
    rig.server_conn->set_error_handler([&errored](int, std::string const&) { errored = true; });
    std::vector<std::uint8_t> received;
    rig.server_conn->set_rx_handler(
        [&received](everest::lib::io::tls::tls_server_socket::PayloadT const& payload, auto&) {
            received.insert(received.end(), payload.begin(), payload.end());
        });

    constexpr std::size_t payload_size = 64 * 1024;
    constexpr std::size_t min_queued = 3;
    constexpr std::size_t max_payloads = 64;
    std::vector<std::uint8_t> expected;
    std::size_t queued = 0;
    while (everest::lib::io::tls::endpoint_peek::tx_queue_depth(*rig.server_conn) < min_queued &&
           queued < max_payloads) {
        auto payload = indexed_payload(queued, payload_size);
        ASSERT_TRUE(rig.server_conn->tx(payload)) << "tx() rejected payload " << queued;
        expected.insert(expected.end(), payload.begin(), payload.end());
        ++queued;
        ev.poll(1ms);
        ev.run_actions();
    }
    ASSERT_GE(everest::lib::io::tls::endpoint_peek::tx_queue_depth(*rig.server_conn), min_queued)
        << "the send path never backed up after " << queued << " payloads of " << payload_size << " bytes";
    ASSERT_EQ(everest::lib::io::tls::endpoint_peek::desired_events(*rig.server_conn),
              everest::lib::io::event::poll_events::write)
        << "SSL_write never answered want_write, so the receive side was never at risk";
    ASSERT_FALSE(errored.load()) << "a backed-up send buffer failed the endpoint instead of queueing";

    // Without draining anything, so the endpoint's send buffer stays full for the whole window.
    peer.probe = true;
    ASSERT_TRUE(test::pump_until(
        ev, [&] { return peer.probe_sent.load() || peer.failed.load(); }, 5s))
        << "the peer never managed to send while stalled: " << peer.error;
    ASSERT_FALSE(peer.failed.load()) << peer.error;

    ASSERT_TRUE(test::pump_until(
        ev, [&] { return received.size() >= peer_probe.size(); }, 5s))
        << "the endpoint never received the peer's payload while its own send was backed up "
        << "[depth=" << everest::lib::io::tls::endpoint_peek::tx_queue_depth(*rig.server_conn)
        << " desired=" << static_cast<int>(everest::lib::io::tls::endpoint_peek::desired_events(*rig.server_conn))
        << "]";
    EXPECT_EQ(received, peer_probe);
    EXPECT_FALSE(errored.load()) << "the endpoint failed instead of receiving while backed up";

    peer.expected = expected.size();
    peer.drain = true;
    peer.close_now = true;
    ASSERT_TRUE(test::pump_until(
        ev, [&] { return peer.finished.load(); }, 30s))
        << "the backed-up queue never drained";
    worker.join();
    ASSERT_FALSE(peer.failed.load()) << peer.error;
    EXPECT_EQ(peer.received, expected) << "the drained bytes are not the accepted payloads in order";
}

// Unbounded, a producer that never checks the return value grows the queue until the process dies.
// Rejecting is not a failure: the connection stays live and everything accepted still arrives.
TEST(TlsServer, tx_rejects_past_the_buffer_cap) {
    everest::lib::io::event::fd_event_handler ev;
    auto rig = test::make_echo_listener(ev);
    ASSERT_TRUE(rig.registered);
    ASSERT_GT(rig.port(), 0u) << "listener bound to port 0 unexpectedly";

    stalled_peer peer;
    std::thread worker([&]() { run_stalled_peer(rig.port(), peer); });
    struct joiner {
        stalled_peer& p;
        std::thread& t;
        ~joiner() {
            p.drain = true;
            p.close_now = true;
            if (t.joinable()) {
                t.join();
            }
        }
    } join_worker{peer, worker};

    ASSERT_GE(connect_stalled_peer(ev, rig, peer), 0)
        << "the stalled peer never reached a live connection: " << peer.error;

    std::atomic<bool> errored{false};
    rig.server_conn->set_error_handler([&errored](int, std::string const&) { errored = true; });
    std::vector<std::uint8_t> trailer;
    rig.server_conn->set_rx_handler(
        [&trailer](everest::lib::io::tls::tls_server_socket::PayloadT const& payload, auto&) {
            trailer.insert(trailer.end(), payload.begin(), payload.end());
        });

    // The loop is not driven while pushing, so nothing drains and every accepted payload stays
    // queued.
    constexpr std::size_t payload_size = 8;
    const std::size_t cap = everest::lib::io::tls::tls_server::max_buffered_tx_payloads;
    const std::size_t attempts = cap + 16;
    std::vector<std::uint8_t> expected;
    std::size_t accepted = 0;
    for (std::size_t i = 0; i < attempts; ++i) {
        auto payload = indexed_payload(i, payload_size);
        if (not rig.server_conn->tx(payload)) {
            break;
        }
        expected.insert(expected.end(), payload.begin(), payload.end());
        ++accepted;
    }

    EXPECT_EQ(accepted, cap) << "tx() accepted " << accepted << " payloads against a cap of " << cap;
    EXPECT_EQ(everest::lib::io::tls::endpoint_peek::tx_queue_depth(*rig.server_conn), accepted);
    ASSERT_FALSE(errored.load()) << "hitting the cap tore the connection down instead of rejecting";

    peer.expected = expected.size();
    peer.drain = true;
    ASSERT_TRUE(test::pump_until(
        ev, [&] { return trailer.size() >= peer_trailer.size() || peer.failed.load(); }, 30s))
        << "the accepted payloads were not delivered and acknowledged within 30 seconds";
    ASSERT_FALSE(peer.failed.load()) << peer.error;
    EXPECT_EQ(trailer, peer_trailer);
    EXPECT_FALSE(errored.load()) << "the endpoint failed while draining after a rejected tx()";

    peer.close_now = true;
    ASSERT_TRUE(test::pump_until(
        ev, [&] { return peer.finished.load(); }, 10s));
    worker.join();

    ASSERT_FALSE(peer.failed.load()) << peer.error;
    EXPECT_EQ(peer.received, expected) << "a payload accepted before the cap was not delivered";
}

// A peer that TCP-connects and never sends a ClientHello would otherwise pin the accepted
// endpoint, its fd, and its tx queue forever: the accept handshake is kicked by the incoming
// data, so a silent peer generates no event at all.
TEST(TlsServer, handshake_deadline_fails_a_silent_peer_with_etimedout) {
    int listen_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_GE(listen_fd, 0);
    int opt = 1;
    ::setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = ::inet_addr("127.0.0.1");
    addr.sin_port = 0;
    ASSERT_EQ(::bind(listen_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)), 0);
    ASSERT_EQ(::listen(listen_fd, 1), 0);
    sockaddr_in bound{};
    socklen_t bound_len = sizeof(bound);
    ASSERT_EQ(::getsockname(listen_fd, reinterpret_cast<sockaddr*>(&bound), &bound_len), 0);

    auto cfg = test::server_test_config();
    cfg.host = "127.0.0.1";
    const std::string port_str = std::to_string(ntohs(bound.sin_port));
    cfg.service = port_str.c_str();
    cfg.ipv6_only = false;
    cfg.socket = listen_fd; // bypass init_socket()
    tls::Server server;
    ASSERT_NE(server.init(cfg, nullptr), tls::Server::state_t::init_needed);

    // Plain TCP peer that never speaks: loopback connect completes without any handshake bytes.
    int silent_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_GE(silent_fd, 0);
    ASSERT_EQ(::connect(silent_fd, reinterpret_cast<sockaddr*>(&bound), sizeof(bound)), 0);

    sockaddr peer{};
    socklen_t peer_len = sizeof(peer);
    int accepted_fd = ::accept(listen_fd, &peer, &peer_len);
    ASSERT_GE(accepted_fd, 0);
    auto conn = server.wrap_accepted_fd(accepted_fd, "127.0.0.1", "0");
    ASSERT_TRUE(conn);

    everest::lib::io::event::fd_event_handler ev;
    everest::lib::io::tls::tls_server srv(std::move(conn));
    srv.set_handshake_timeout(200ms);

    std::atomic<bool> got_error{false};
    std::atomic<int> err_code{0};
    std::string err_text;
    srv.set_error_handler([&](int err, std::string const& text) {
        err_code = err;
        err_text = text;
        got_error = true;
    });
    ASSERT_TRUE(ev.register_event_handler(&srv));

    const auto armed = std::chrono::steady_clock::now();
    ASSERT_TRUE(test::pump_until(
        ev, [&] { return got_error.load(); }, 5s))
        << "the silent peer never expired the accept handshake";
    EXPECT_LT(std::chrono::steady_clock::now() - armed, 2s) << "the deadline fired far past its 200ms bound";
    EXPECT_EQ(err_code.load(), ETIMEDOUT);
    EXPECT_EQ(err_text, "handshake deadline expired");

    ::close(silent_fd);
    ::close(listen_fd);
}

// tls_server tells its owner to drop the unique_ptr on error, and the error callback is the only
// notification of one. Reporting from inside fail() made following that instruction a
// heap-use-after-free: fail() reads m_fd, m_handler and m_socket after the callback returns, and the
// connection dispatch reads m_errored after flush_rx. Only a sanitizer sees it.
TEST(TlsServer, dropping_the_server_inside_its_error_handler_is_safe) {
    everest::lib::io::event::fd_event_handler ev;
    auto rig = test::make_echo_listener(ev);
    ASSERT_TRUE(rig.registered);
    ASSERT_GT(rig.port(), 0u) << "listener bound to port 0 unexpectedly";

    ASSERT_GE(reset_peer_after_round_trip(ev, rig), 0) << "the round trip did not complete within 5 seconds";
    ASSERT_NE(rig.server_conn, nullptr) << "the listener never accepted a connection";

    std::atomic<int> reported_code{0};
    rig.server_conn->set_error_handler([&rig, &reported_code](int code, std::string const&) {
        reported_code = code;
        rig.server_conn.reset();
    });

    ASSERT_TRUE(test::pump_until(
        ev, [&] { return reported_code.load() != 0; }, 5s))
        << "the server endpoint never reported the peer reset";
    EXPECT_EQ(rig.server_conn, nullptr) << "the error handler ran but the drop did not";

    // The queued teardown runs before the report, so nothing here may touch the freed endpoint.
    for (int i = 0; i < 5; ++i) {
        ev.poll(10ms);
        ev.run_actions();
    }
}

// Same guarantee for the other deferred callback: on-ready also runs from run_actions() with no
// `this` alive across it, so it carries the same permission. The rx handler does not, since it is
// handed *this and the dispatch keeps reading members after it returns.
TEST(TlsServer, dropping_the_server_inside_its_on_ready_action_is_safe) {
    // Declared before the listener and the connection, both of which unregister from it on the way out.
    everest::lib::io::event::fd_event_handler ev;
    std::unique_ptr<everest::lib::io::tls::tls_server> server_conn;
    std::atomic<bool> dropped{false};

    everest::lib::io::tls::tls_listener listener(test::listener_test_config());
    listener.set_accept_callback(
        [&](std::unique_ptr<everest::lib::io::tls::tls_server> srv, std::string, std::uint16_t) {
            // maybe_fire_ready() consults m_on_ready once, at handshake completion, so an action
            // installed after the accept callback returns could miss its only chance to run.
            srv->set_on_ready_action([&server_conn, &dropped]() {
                server_conn.reset();
                dropped = true;
            });
            ev.register_event_handler(srv.get());
            server_conn = std::move(srv);
        });
    ASSERT_TRUE(ev.register_event_handler(&listener));
    ASSERT_GT(listener.listen_port(), 0u) << "listener bound to port 0 unexpectedly";

    everest::lib::io::tls::tls_client client(test::client_test_config(), std::string("127.0.0.1"),
                                             listener.listen_port(), 2000);
    ASSERT_TRUE(ev.register_event_handler(&client));

    ASSERT_TRUE(test::pump_until(
        ev, [&] { return dropped.load(); }, 5s))
        << "the accept handshake never completed";
    EXPECT_EQ(server_conn, nullptr) << "the on-ready action ran but the drop did not";

    for (int i = 0; i < 5; ++i) {
        ev.poll(10ms);
        ev.run_actions();
    }
    ev.unregister_event_handler(&client);
    ev.unregister_event_handler(&listener);
}
