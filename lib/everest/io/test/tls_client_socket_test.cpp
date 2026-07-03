// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest

#include "tls_test_common.hpp"

#include <everest/io/event/fd_event_handler.hpp>
#include <everest/io/tls/tls_client.hpp>
#include <everest/io/tls/tls_client_socket.hpp>
#include <everest/tls/tls.hpp>

#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

namespace {

namespace test = everest::lib::io::test;

/// Payload size that exceeds a single 4096-byte TLS-record read so the
/// client-side rx() must drain SSL_pending() records in one call.
constexpr std::size_t kLargePayload = 10000;

/// Open an ephemeral 127.0.0.1 listen socket and return its fd + bound port.
int make_listen_socket(uint16_t& bound_port) {
    int listen_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        return -1;
    }
    int opt = 1;
    ::setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = ::inet_addr("127.0.0.1");
    addr.sin_port = 0; // ephemeral
    if (::bind(listen_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 || ::listen(listen_fd, 1) != 0) {
        ::close(listen_fd);
        return -1;
    }

    sockaddr_in bound{};
    socklen_t bound_len = sizeof(bound);
    if (::getsockname(listen_fd, reinterpret_cast<sockaddr*>(&bound), &bound_len) != 0) {
        ::close(listen_fd);
        return -1;
    }
    bound_port = ntohs(bound.sin_port);
    return listen_fd;
}

/// Configure a tls::Server matching the test PKI on an already-listening socket.
tls::Server::config_t make_server_config(int listen_fd, std::string const& port_str) {
    auto scfg = test::server_test_config();
    scfg.host = "127.0.0.1";
    scfg.service = port_str.c_str();
    scfg.ipv6_only = false;
    scfg.socket = listen_fd; // bypass init_socket()
    return scfg;
}

/// Build the client Config shared by the tests.
everest::lib::io::tls::tls_client_socket::Config make_client_config() {
    return test::client_test_config(1000, "localhost");
}

/// Open a raw non-blocking IPv4 socket and start a connect to 127.0.0.1:port.
/// Returns the fd with the connect completed or in flight, or -1 on a hard
/// socket()/connect() failure.
int start_nonblocking_connect(uint16_t port) {
    int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
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

/// True if the pending connect on fd completed cleanly within wait_ms (POLLOUT
/// without POLLERR/POLLHUP). A connect the kernel never completes reports
/// nothing within the window.
bool connect_completed(int fd, int wait_ms) {
    pollfd pfd{fd, POLLOUT, 0};
    return ::poll(&pfd, 1, wait_ms) == 1 && (pfd.revents & POLLOUT) != 0 && (pfd.revents & (POLLERR | POLLHUP)) == 0;
}

/// A 127.0.0.1 listen socket whose accept queue is deliberately saturated:
/// listen(fd, 1), never accept, and raw pre-connects issued until one no
/// longer completes. With the queue full (and default syncookies) the kernel
/// no longer completes new handshakes, so any later connect to port() stays
/// pending until the caller's timeout — a deterministic loopback stand-in for
/// an unreachable host. The stalled final pre-connect doubles as the
/// saturation probe. All fds (listener + pre-connect sockets) stay open until
/// destruction so the block holds for the object's whole lifetime.
class saturated_loopback_port {
public:
    saturated_loopback_port() {
        m_listen_fd = make_listen_socket(m_port);
        if (m_listen_fd < 0) {
            return;
        }
        // Linux admits roughly backlog+1 queued connections for listen(fd, 1);
        // keep connecting until one stalls (capped as a safety margin).
        for (int i = 0; i < max_pre_connects; ++i) {
            const int fd = start_nonblocking_connect(m_port);
            if (fd < 0) {
                return;
            }
            m_fds.push_back(fd);
            if (!connect_completed(fd, 200)) {
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
    uint16_t port() const {
        return m_port;
    }

private:
    // ~3 connects saturate in practice on Linux; 8 is headroom, not a tuned value.
    static constexpr int max_pre_connects = 8;
    int m_listen_fd{-1};
    uint16_t m_port{0};
    std::vector<int> m_fds;
    bool m_saturated{false};
};

} // namespace

namespace everest::lib::io::tls {
// Test seam (no friendship): re-expose the otherwise-internal tcp_socket so the
// ownership-surrender checks below can confirm m_tcp released its fd to the BIO.
struct testable_client_socket : tls_client_socket {
    using tls_client_socket::m_tcp;
};

// Test seam: re-expose the endpoint's registered fds and socket so the teardown
// tests can confirm the handler entries are gone and the connection state.
struct testable_client : tls_client {
    using tls_client::tls_client;
    using tls_endpoint_base<tls_client_socket>::m_fd;
    using tls_endpoint_base<tls_client_socket>::m_socket;
    using tls_endpoint_base<tls_client_socket>::m_tx_notify;
};
} // namespace everest::lib::io::tls

// After connect() wraps the connected TCP fd, the connection's BIO is the
// fd's sole owner: m_tcp must have surrendered it (outcome, not timing), or
// teardown double-closes.
TEST(tls_client_socket, fd_ownership_released_after_async_wrap) {
    uint16_t bound_port = 0;
    int listen_fd = make_listen_socket(bound_port);
    ASSERT_GE(listen_fd, 0);

    everest::lib::io::tls::testable_client_socket sock;
    auto cfg = make_client_config();
    // setup() does no network I/O, so assert it before spawning the server
    // thread (no joinable thread is live yet if it fails).
    ASSERT_TRUE(sock.setup(std::move(cfg), "127.0.0.1", bound_port, 2000));

    // connect() only wraps the TCP fd (no TLS handshake), so the server thread
    // just needs to accept the TCP connection and exit. accept() is blocking;
    // the client's TCP connect unblocks it.
    std::thread server_thread([&]() {
        sockaddr peer{};
        socklen_t peer_len = sizeof(peer);
        int accepted_fd = ::accept(listen_fd, &peer, &peer_len);
        if (accepted_fd >= 0) {
            ::close(accepted_fd);
        }
    });

    bool ok = false;
    sock.connect([&](bool o, int /*fd*/) { ok = o; }); // synchronous
    const int tcp_fd_after = sock.m_tcp.get_fd();

    // Join before asserting so a fatal ASSERT cannot return with the thread
    // still joinable (std::terminate). connect() drove the TCP connect, so the
    // server's accept() has returned and the thread is exiting.
    sock.close();
    server_thread.join();
    ::close(listen_fd);

    ASSERT_TRUE(ok) << "TCP connect / fd wrap failed";
    EXPECT_EQ(tcp_fd_after, everest::lib::io::event::unique_fd::NO_DESCRIPTOR_SENTINEL)
        << "m_tcp still owns the fd after async wrap -> double-close on teardown";
}

// tls_client (the loop-driven register-interface class): register with an
// fd_event_handler, drive the loop against a background raw tls::Server. The
// on-ready action must fire after the handshake; a small tx request must elicit
// a > 4096-byte rx reply that rx() drains via SSL_pending() in one delivery.
TEST(TlsClient, HandshakeAndExchange) {
    uint16_t bound_port = 0;
    int listen_fd = make_listen_socket(bound_port);
    ASSERT_GE(listen_fd, 0);
    const std::string port_str = std::to_string(bound_port);

    tls::Server server;
    const auto state = server.init(make_server_config(listen_fd, port_str), nullptr);
    ASSERT_NE(state, tls::Server::state_t::init_needed)
        << "tls::Server::init failed — check that server_chain.pem etc. exist "
           "in the working directory (lib/everest/tls/tests in the build tree).";

    // Background server thread: accept, handshake, read the small request, reply
    // with a > 4096-byte payload so the client rx() must drain SSL_pending().
    std::atomic<bool> server_ok{false};
    std::string server_error;
    std::thread server_thread([&]() {
        sockaddr peer{};
        socklen_t peer_len = sizeof(peer);
        int accepted_fd = ::accept(listen_fd, &peer, &peer_len);
        if (accepted_fd < 0) {
            server_error = "accept failed";
            return;
        }

        char ip_buf[NI_MAXHOST] = "127.0.0.1";
        char svc_buf[NI_MAXSERV] = "0";
        ::getnameinfo(&peer, peer_len, ip_buf, sizeof ip_buf, svc_buf, sizeof svc_buf, NI_NUMERICHOST | NI_NUMERICSERV);

        auto conn = server.wrap_accepted_fd(accepted_fd, ip_buf, svc_buf);
        if (!conn) {
            server_error = "wrap_accepted_fd returned nullptr";
            return;
        }
        if (conn->accept(2000) != tls::Connection::result_t::success) {
            server_error = "server TLS handshake failed";
            return;
        }

        std::byte buf[64]{};
        std::size_t nread = 0;
        if (conn->read(buf, sizeof buf, nread, 2000) != tls::Connection::result_t::success) {
            server_error = "server read failed";
            return;
        }

        std::vector<uint8_t> reply(kLargePayload);
        for (std::size_t i = 0; i < reply.size(); ++i) {
            reply[i] = static_cast<uint8_t>(i & 0xFF);
        }
        std::size_t written = 0;
        if (conn->write(reinterpret_cast<const std::byte*>(reply.data()), reply.size(), written, 2000) !=
                tls::Connection::result_t::success ||
            written != reply.size()) {
            server_error = "server write failed";
            return;
        }

        conn->shutdown(0);
        server_ok = true;
    });

    // Client side: construct, attach handlers, register on the loop and drive it.
    // The handler must outlive the endpoint (the client dtor unregisters from the
    // handler), so declare it first.
    everest::lib::io::event::fd_event_handler ev;
    everest::lib::io::tls::tls_client client(make_client_config(), std::string("127.0.0.1"), bound_port, 2000);

    std::atomic<bool> running{true};
    std::atomic<bool> ready_fired{false};
    std::vector<std::uint8_t> rx_buf;

    client.set_on_ready_action([&client, &ready_fired]() {
        ready_fired = true;
        everest::lib::io::tls::tls_client_socket::PayloadT msg = {'h', 'i'};
        client.tx(msg);
    });
    client.set_rx_handler(
        [&rx_buf, &running](everest::lib::io::tls::tls_client_socket::PayloadT const& payload, auto&) {
            rx_buf.insert(rx_buf.end(), payload.begin(), payload.end());
            if (rx_buf.size() >= kLargePayload) {
                running = false;
            }
        });
    ASSERT_TRUE(ev.register_event_handler(&client));

    test::pump_until(
        ev, [&] { return !running; }, 5s);

    server_thread.join();
    ::close(listen_fd);

    EXPECT_TRUE(ready_fired) << "on-ready action did not fire after handshake";
    ASSERT_EQ(rx_buf.size(), kLargePayload) << "rx() did not drain all buffered TLS records in one call";
    for (std::size_t i = 0; i < rx_buf.size(); ++i) {
        ASSERT_EQ(rx_buf[i], static_cast<uint8_t>(i & 0xFF)) << "payload corruption at index " << i;
    }
    EXPECT_TRUE(server_ok) << "Server error: " << server_error;
}

// Teardown while the TCP connect is still pending. The client targets a
// loopback port whose accept queue is saturated (saturated_loopback_port), so
// the kernel never completes the handshake and the connect worker is still
// blocked inside m_socket.connect() when the client is unregistered and
// destroyed. Teardown (stop()/dtor) must join the worker before tearing down
// m_socket, so the worker cannot touch a freed socket (no use-after-free) and
// cannot outlive *this. That joined-worker/no-UAF invariant is enforced only
// under ThreadSanitizer, which flags the detached-worker race directly; the
// asserts below would all also pass with a detached worker. Without tsan this
// is a teardown smoke/liveness test on a deterministically pending connect:
// teardown returns true, the on-ready action never fired (the connect never
// completed — the canary that the saturation held), and the whole sequence
// finishes promptly with the loop healthy afterward.
TEST(TlsClient, teardown_during_pending_connect_is_clean) {
    saturated_loopback_port blocked;
    ASSERT_TRUE(blocked.saturated()) << "could not saturate the loopback accept queue";

    everest::lib::io::event::fd_event_handler ev;
    constexpr int connect_timeout_ms = 300;
    std::atomic<bool> ready_fired{false};
    bool unregistered = false;
    const auto start = std::chrono::steady_clock::now();
    {
        everest::lib::io::tls::tls_client client(make_client_config(), std::string("127.0.0.1"), blocked.port(),
                                                 connect_timeout_ms);
        client.set_on_ready_action([&ready_fired]() { ready_fired = true; });
        ASSERT_TRUE(ev.register_event_handler(&client));
        // Pump the loop so the connect worker is mid-flight (blocked on the
        // never-completing connect to the saturated port) before teardown.
        for (int i = 0; i < 3; ++i) {
            ev.poll(50ms);
            ev.run_actions();
        }
        unregistered = ev.unregister_event_handler(&client);
        // client destroyed at the closing brace below, connect still pending —
        // teardown must join the worker, not return into a UAF window.
    }
    const auto elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_TRUE(unregistered) << "unregister of the pending-connect client failed";
    EXPECT_FALSE(ready_fired) << "on-ready fired — the connect completed, so the backlog saturation did not hold";
    EXPECT_LT(elapsed, 5s) << "teardown of a pending connect must be bounded by the connect timeout";

    // Loop stays healthy after teardown; no crash/hang.
    for (int i = 0; i < 3; ++i) {
        ev.poll(10ms);
        ev.run_actions();
    }
    SUCCEED();
}

// P1: an EPOLLERR/EPOLLHUP on a live, post-handshake connection with no prior
// TLS-op failure must deliver a NONZERO error code. A peer RST (SO_LINGER{1,0}
// + close) after the handshake makes epoll report error/hungup while the socket
// still holds a live connection and m_last_error is 0, so get_error() resolves
// to 0. Every shipped example ignores the callback via `if (err != 0)`, so a 0
// code silently strands the endpoint. The handler must observe a nonzero code.
TEST(TlsClient, poll_error_delivers_nonzero_code) {
    uint16_t bound_port = 0;
    int listen_fd = make_listen_socket(bound_port);
    ASSERT_GE(listen_fd, 0);
    const std::string port_str = std::to_string(bound_port);

    tls::Server server;
    const auto state = server.init(make_server_config(listen_fd, port_str), nullptr);
    ASSERT_NE(state, tls::Server::state_t::init_needed);

    // Background server: accept, complete the handshake, read the client ping so
    // the client is fully handshaked and armed for read, then RST.
    std::thread server_thread([&]() {
        sockaddr peer{};
        socklen_t peer_len = sizeof(peer);
        int accepted_fd = ::accept(listen_fd, &peer, &peer_len);
        if (accepted_fd < 0) {
            return;
        }
        char ip_buf[NI_MAXHOST] = "127.0.0.1";
        char svc_buf[NI_MAXSERV] = "0";
        ::getnameinfo(&peer, peer_len, ip_buf, sizeof ip_buf, svc_buf, sizeof svc_buf, NI_NUMERICHOST | NI_NUMERICSERV);
        auto conn = server.wrap_accepted_fd(accepted_fd, ip_buf, svc_buf);
        if (!conn) {
            ::close(accepted_fd);
            return;
        }
        if (conn->accept(2000) != tls::Connection::result_t::success) {
            return;
        }
        std::byte buf[64]{};
        std::size_t nread = 0;
        (void)conn->read(buf, sizeof buf, nread, 2000);
        // Zero-linger close discards the send queue and emits a RST instead of a
        // graceful close_notify/FIN: the client sees EPOLLERR/EPOLLHUP with no
        // readable TLS record. The BIO (BIO_CLOSE) closes accepted_fd on reset.
        struct linger lo {
            1, 0
        };
        ::setsockopt(accepted_fd, SOL_SOCKET, SO_LINGER, &lo, sizeof(lo));
        conn.reset();
    });

    // The handler must outlive the endpoint (the client dtor unregisters from the
    // handler), so declare it first.
    everest::lib::io::event::fd_event_handler ev;
    everest::lib::io::tls::tls_client client(make_client_config(), std::string("127.0.0.1"), bound_port, 2000);

    std::atomic<bool> got_error{false};
    std::atomic<int> err_code{-1};
    client.set_on_ready_action([&client]() {
        everest::lib::io::tls::tls_client_socket::PayloadT msg = {'p', 'i', 'n', 'g'};
        client.tx(msg);
    });
    client.set_error_handler([&](int err, std::string const&) {
        err_code = err;
        got_error = true;
    });
    ASSERT_TRUE(ev.register_event_handler(&client));

    test::pump_until(
        ev, [&] { return got_error.load(); }, 5s);

    server_thread.join();
    ::close(listen_fd);

    ASSERT_TRUE(got_error) << "error handler never fired after a peer RST";
    EXPECT_NE(err_code.load(), 0) << "error handler received code 0 on the poll-error (RST) path";
}

// P2: dropping a still-registered endpoint (the README's documented teardown,
// "connections.clear()") must not leave its this-capturing lambdas registered
// in the handler. The destructor must unregister the connection fd and the
// tx-notify eventfd; otherwise the handler keeps stale entries referencing a
// freed object. After the drop, re-registering those fd numbers must succeed
// (a stale entry makes the map's exists() guard reject the register).
TEST(TlsClient, drop_registered_endpoint_unregisters_fds) {
    uint16_t bound_port = 0;
    int listen_fd = make_listen_socket(bound_port);
    ASSERT_GE(listen_fd, 0);
    const std::string port_str = std::to_string(bound_port);

    tls::Server server;
    ASSERT_NE(server.init(make_server_config(listen_fd, port_str), nullptr), tls::Server::state_t::init_needed);

    // Server: accept + handshake, then hold the connection open until released so
    // the client stays live while it is dropped.
    std::atomic<bool> release_server{false};
    std::thread server_thread([&]() {
        sockaddr peer{};
        socklen_t peer_len = sizeof(peer);
        int accepted_fd = ::accept(listen_fd, &peer, &peer_len);
        if (accepted_fd < 0) {
            return;
        }
        char ip_buf[NI_MAXHOST] = "127.0.0.1";
        char svc_buf[NI_MAXSERV] = "0";
        ::getnameinfo(&peer, peer_len, ip_buf, sizeof ip_buf, svc_buf, sizeof svc_buf, NI_NUMERICHOST | NI_NUMERICSERV);
        auto conn = server.wrap_accepted_fd(accepted_fd, ip_buf, svc_buf);
        if (!conn) {
            ::close(accepted_fd);
            return;
        }
        if (conn->accept(2000) != tls::Connection::result_t::success) {
            return;
        }
        while (!release_server.load()) {
            std::this_thread::sleep_for(10ms);
        }
        conn->shutdown(0);
    });

    everest::lib::io::event::fd_event_handler ev;
    int conn_fd = -1;
    int notify_fd = -1;
    {
        everest::lib::io::tls::testable_client client(make_client_config(), std::string("127.0.0.1"), bound_port, 2000);
        std::atomic<bool> ready{false};
        client.set_on_ready_action([&ready]() { ready = true; });
        ASSERT_TRUE(ev.register_event_handler(&client));
        test::pump_until(
            ev, [&] { return ready.load(); }, 5s);
        ASSERT_TRUE(ready) << "handshake did not complete before the drop";
        conn_fd = client.m_fd;
        notify_fd = client.m_tx_notify.get_raw_fd();
        ASSERT_GE(conn_fd, 0);
        ASSERT_GE(notify_fd, 0);
        // Drop WITHOUT unregister at the closing brace.
    }

    // Nothing here opens new fds, so conn_fd / notify_fd stay closed-but-unclaimed;
    // driving the loop after the drop must not crash.
    for (int i = 0; i < 5; ++i) {
        ev.poll(10ms);
        ev.run_actions();
    }

    auto noop = [](auto&&) {};
    EXPECT_TRUE(ev.register_event_handler(conn_fd, noop, everest::lib::io::event::poll_events::read))
        << "connection fd still registered after dropping the endpoint";
    EXPECT_TRUE(ev.register_event_handler(notify_fd, noop, everest::lib::io::event::poll_events::read))
        << "tx-notify fd still registered after dropping the endpoint";

    release_server = true;
    server_thread.join();
    ::close(listen_fd);
}

// Registering the same client twice without an intervening unregister must be
// rejected, not re-run start(). fd_event_handler forwards an interface register
// straight to register_events() with no interface-level dedup, so a second
// register would call start() again and move-assign a std::thread onto the
// still-joinable connect worker -> std::terminate. The register_events() guard
// makes the second register a clean no-op returning false.
TEST(TlsClient, double_register_is_rejected_not_fatal) {
    saturated_loopback_port blocked;
    ASSERT_TRUE(blocked.saturated()) << "could not saturate the loopback accept queue";

    everest::lib::io::event::fd_event_handler ev;
    everest::lib::io::tls::tls_client client(make_client_config(), std::string("127.0.0.1"), blocked.port(), 300);

    ASSERT_TRUE(ev.register_event_handler(&client)) << "first register should succeed and start the worker";
    // Second register without an intervening unregister: must return false and
    // must NOT re-enter start() (which would std::terminate by move-assigning
    // onto the live worker).
    EXPECT_FALSE(ev.register_event_handler(&client)) << "double register must be rejected, not re-start the endpoint";

    // Clean teardown joins the worker; the loop stays healthy afterward.
    ev.unregister_event_handler(&client);
    for (int i = 0; i < 3; ++i) {
        ev.poll(10ms);
        ev.run_actions();
    }
    SUCCEED();
}
