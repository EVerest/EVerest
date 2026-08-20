// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest

#include "tls_test_common.hpp"

#include <everest/io/event/fd_event_handler.hpp>
#include <everest/io/tls/tls_client.hpp>
#include <everest/io/tls/tls_client_config.hpp>
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

/// Exceeds the 4096-byte rx() chunk buffer, so rx() must drain the remainder via has_pending().
constexpr std::size_t kLargePayload = 10000;

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
    addr.sin_port = 0;
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

tls::Server::config_t make_server_config(int listen_fd, std::string const& port_str) {
    auto scfg = test::server_test_config();
    scfg.host = "127.0.0.1";
    scfg.service = port_str.c_str();
    scfg.ipv6_only = false;
    scfg.socket = listen_fd; // bypass init_socket()
    return scfg;
}

everest::lib::io::tls::tls_client_socket::Config make_client_config() {
    return test::client_test_config(1000, "localhost");
}

/// Returns the fd with the connect completed or still in flight, or -1 on a hard failure.
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

/// True on POLLOUT without POLLERR/POLLHUP; a connect the kernel never completes reports nothing.
bool connect_completed(int fd, int wait_ms) {
    pollfd pfd{fd, POLLOUT, 0};
    return ::poll(&pfd, 1, wait_ms) == 1 && (pfd.revents & POLLOUT) != 0 && (pfd.revents & (POLLERR | POLLHUP)) == 0;
}

/// A 127.0.0.1 listen socket whose accept queue is deliberately saturated: listen(fd, 1), never
/// accept, and raw pre-connects until one no longer completes. The kernel then stops completing
/// handshakes, so a later connect to port() stays pending until the caller's timeout, a
/// deterministic loopback stand-in for an unreachable host.
class saturated_loopback_port {
public:
    saturated_loopback_port() {
        m_listen_fd = make_listen_socket(m_port);
        if (m_listen_fd < 0) {
            return;
        }
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
// Test seam without friendship: re-expose the otherwise-internal tcp_socket.
struct testable_client_socket : tls_client_socket {
    using tls_client_socket::m_tcp;
};
} // namespace everest::lib::io::tls

namespace {
/// get_raw_handler() is the only public window on the policy; the client does not expose the fd.
int connection_fd(everest::lib::io::tls::tls_client& client) {
    auto const& handle = client.get_raw_handler();
    return handle ? handle->get_fd() : -1;
}
} // namespace

// The connection's BIO becomes the fd's sole owner, so m_tcp must have surrendered it or teardown
// double-closes.
TEST(tls_client_socket, fd_ownership_released_after_async_wrap) {
    uint16_t bound_port = 0;
    int listen_fd = make_listen_socket(bound_port);
    ASSERT_GE(listen_fd, 0);

    everest::lib::io::tls::testable_client_socket sock;
    auto cfg = make_client_config();
    ASSERT_TRUE(sock.setup(std::move(cfg), "127.0.0.1", bound_port, 2000));

    // connect() only wraps the TCP fd, so the server thread just accepts and exits.
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

    // Join before asserting: a fatal ASSERT returning with the thread joinable is std::terminate.
    sock.close();
    server_thread.join();
    ::close(listen_fd);

    ASSERT_TRUE(ok) << "TCP connect / fd wrap failed";
    EXPECT_EQ(tcp_fd_after, everest::lib::io::event::unique_fd::NO_DESCRIPTOR_SENTINEL)
        << "m_tcp still owns the fd after async wrap -> double-close on teardown";
}

TEST(TlsClient, HandshakeAndExchange) {
    uint16_t bound_port = 0;
    int listen_fd = make_listen_socket(bound_port);
    ASSERT_GE(listen_fd, 0);
    const std::string port_str = std::to_string(bound_port);

    tls::Server server;
    const auto state = server.init(make_server_config(listen_fd, port_str), nullptr);
    ASSERT_NE(state, tls::Server::state_t::init_needed)
        << "tls::Server::init failed: check that server_chain.pem etc. exist "
           "in the working directory (lib/everest/tls/tests in the build tree).";

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

    // The handler must outlive the client, whose destructor drops its registration.
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

// Teardown neither joins nor waits for the connect thread. Destruction clears the shared connect
// state's active flag, so the thread returns without publishing its result and destroys the policy
// it moved in on its own stack. It never captures the client.
TEST(TlsClient, teardown_during_pending_connect_is_clean) {
    saturated_loopback_port blocked;
    ASSERT_TRUE(blocked.saturated()) << "could not saturate the loopback accept queue";

    everest::lib::io::event::fd_event_handler ev;
    // Long enough that a teardown waiting for the connect could not meet the bound below.
    constexpr int connect_timeout_ms = 400;
    std::atomic<bool> ready_fired{false};
    std::atomic<int> error_calls{0};
    int poll_fd = -1;
    std::chrono::steady_clock::time_point teardown_start;
    {
        everest::lib::io::tls::tls_client client(make_client_config(), std::string("127.0.0.1"), blocked.port(),
                                                 connect_timeout_ms);
        client.set_on_ready_action([&ready_fired]() { ready_fired = true; });
        client.set_error_handler([&error_calls](int err, std::string const&) {
            if (err != 0) {
                ++error_calls;
            }
        });
        ASSERT_TRUE(ev.register_event_handler(&client));
        // Drain the deferred handler registrations and leave the connect thread mid-flight.
        for (int i = 0; i < 3; ++i) {
            ev.poll(50ms);
            ev.run_actions();
        }
        ASSERT_EQ(connection_fd(client), -1) << "the connect completed, so the backlog saturation did not hold";
        poll_fd = client.get_poll_fd();
        ASSERT_GE(poll_fd, 0);

        teardown_start = std::chrono::steady_clock::now();
        ev.unregister_event_handler(&client);
        // client destroyed at the closing brace below, connect still pending.
    }
    const auto teardown_elapsed = std::chrono::steady_clock::now() - teardown_start;

    EXPECT_LT(teardown_elapsed, 200ms) << "teardown waited for the pending connect instead of abandoning it";

    // A wall-clock window, not a completion signal: under heavy load it can close before the thread
    // finishes, which makes the checks below vacuous rather than flaky.
    const auto drain_deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(connect_timeout_ms) + 500ms;
    while (std::chrono::steady_clock::now() < drain_deadline) {
        ev.poll(10ms);
        ev.run_actions();
    }

    EXPECT_FALSE(ready_fired) << "the ready action was released for a client that never connected";
    EXPECT_EQ(error_calls.load(), 0) << "no error was reported while the connect was still pending";
    EXPECT_FALSE(ev.is_registered(poll_fd))
        << "the client's nested poll fd is still in the handler map after unregister";
}

// A peer RST leaves the socket with m_last_error 0, and every shipped consumer ignores the callback
// via `if (err != 0)`, so an unresolved 0 strands the client.
TEST(TlsClient, poll_error_delivers_nonzero_code) {
    uint16_t bound_port = 0;
    int listen_fd = make_listen_socket(bound_port);
    ASSERT_GE(listen_fd, 0);
    const std::string port_str = std::to_string(bound_port);

    tls::Server server;
    const auto state = server.init(make_server_config(listen_fd, port_str), nullptr);
    ASSERT_NE(state, tls::Server::state_t::init_needed);

    // Reads the ping first, so the client is handshaked and monitored for read before the RST.
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
        // Zero linger emits an RST instead of a graceful close_notify, so the client sees
        // EPOLLERR/EPOLLHUP with no readable TLS record. The BIO closes accepted_fd on reset.
        struct linger lo {
            1, 0
        };
        ::setsockopt(accepted_fd, SOL_SOCKET, SO_LINGER, &lo, sizeof(lo));
        conn.reset();
    });

    // Declared before the client, whose dtor unregisters from it.
    everest::lib::io::event::fd_event_handler ev;
    everest::lib::io::tls::tls_client client(make_client_config(), std::string("127.0.0.1"), bound_port, 2000);

    std::atomic<bool> got_error{false};
    std::atomic<bool> ready_fired{false};
    std::atomic<int> err_code{0};
    client.set_on_ready_action([&client, &ready_fired]() {
        ready_fired = true;
        everest::lib::io::tls::tls_client_socket::PayloadT msg = {'p', 'i', 'n', 'g'};
        client.tx(msg);
    });
    // A cleared error also arrives as code 0 once the connection comes up. Reporting on 0 would stop
    // the loop before the handshake and hang the server thread on its blocking accept().
    client.set_error_handler([&](int err, std::string const&) {
        if (err != 0) {
            err_code = err;
            got_error = true;
        }
    });
    ASSERT_TRUE(ev.register_event_handler(&client));

    test::pump_until(
        ev, [&] { return got_error.load(); }, 5s);

    server_thread.join();
    ::close(listen_fd);

    ASSERT_TRUE(ready_fired) << "the handshake never completed, so this is not the live-connection path";
    ASSERT_TRUE(got_error) << "error handler never fired after a peer RST";
    EXPECT_NE(err_code.load(), 0) << "error handler received code 0 on the poll-error (RST) path";
}

// setup() returns false before any descriptor exists, so a get_error() left at 0 falls through to
// the tcp socket and answers EBADF, naming the probe and not the configuration. The trigger is an
// unreadable client certificate; an unreadable trust anchor is not one, libtls logs and carries on.
TEST(tls_client_socket, setup_failure_records_error) {
    everest::lib::io::tls::tls_client_socket sock;
    auto cfg = make_client_config();
    cfg.tls.certificate_chain_file = "no_such_cert.pem";

    EXPECT_FALSE(sock.setup(std::move(cfg), "127.0.0.1", 4711, 100));
    EXPECT_NE(sock.get_error(), 0) << "setup() failed without recording an error";
    EXPECT_NE(sock.get_error(), EBADF) << "get_error() probed a descriptor that was never assigned";
    EXPECT_FALSE(sock.get_error_string().empty()) << "setup() failure reported no reason";
    EXPECT_NE(sock.get_error_string().find("no_such_cert.pem"), std::string::npos)
        << "setup() failure did not name the offending file: " << sock.get_error_string();
}

// tls::Client::init also rejects inputs that are not files, so the reason text must not read as if a
// path were at fault. An unset path must render as visibly unset, not as an empty one, which libtls
// treats as a different configuration.
TEST(tls_client_socket, setup_failure_text_describes_the_configuration) {
    everest::lib::io::tls::tls_client_socket sock;
    everest::lib::io::tls::tls_client_socket::Config cfg{};
    cfg.tls.verify_server = false;
    cfg.tls.verify_subject_name = true;

    EXPECT_FALSE(sock.setup(std::move(cfg), "127.0.0.1", 4711, 100));
    EXPECT_EQ(sock.get_error(), EINVAL) << "a rejected configuration is an invalid argument, not a socket error";
    auto const text = sock.get_error_string();
    EXPECT_NE(text.find("certificate_chain_file=<unset>"), std::string::npos)
        << "an unset path did not render as unset: " << text;
    EXPECT_NE(text.find("verify_server=false"), std::string::npos)
        << "the reason text omits the non-path input that was rejected: " << text;
    EXPECT_NE(text.find("verify_subject_name=true"), std::string::npos)
        << "the reason text omits the non-path input that was rejected: " << text;
}

// Both registrations resolve to the same nested poll fd, so fd_event_handler's exists() guard is
// what makes the second call a clean false rather than a duplicate entry.
TEST(TlsClient, double_register_is_rejected_not_fatal) {
    saturated_loopback_port blocked;
    ASSERT_TRUE(blocked.saturated()) << "could not saturate the loopback accept queue";

    everest::lib::io::event::fd_event_handler ev;
    everest::lib::io::tls::tls_client client(make_client_config(), std::string("127.0.0.1"), blocked.port(), 300);

    ASSERT_TRUE(ev.register_event_handler(&client)) << "first register should succeed";
    EXPECT_FALSE(ev.register_event_handler(&client)) << "double register must be rejected, not re-start the endpoint";

    ev.unregister_event_handler(&client);
    for (int i = 0; i < 3; ++i) {
        ev.poll(10ms);
        ev.run_actions();
    }
    SUCCEED();
}

namespace {

// The Config must own every string it carries: the generic client copies it at construction and
// replays setup() on the first poll and on every reconnect, long after the caller's buffers died.
everest::lib::io::tls::tls_client_socket::Config config_from_dying_locals() {
    // All locals, all destroyed when this function returns.
    std::string cipher_list{"ECDHE-ECDSA-AES128-SHA256"};
    std::string ciphersuites{}; // set-but-empty disables TLS 1.3, matching the test server
    std::string trust_anchor{"server_root_cert.pem"};
    std::string sni{"localhost"};

    everest::lib::io::tls::tls_client_socket::Config cfg{};
    cfg.tls.cipher_list = cipher_list;
    cfg.tls.ciphersuites = ciphersuites;
    cfg.tls.verify_locations_file = trust_anchor;
    cfg.tls.io_timeout_ms = 2000;
    cfg.tls.verify_server = true;
    cfg.host_for_sni = sni;
    return cfg;
}

} // namespace

TEST(TlsClient, config_built_from_dead_locals_still_handshakes) {
    everest::lib::io::event::fd_event_handler ev;
    auto rig = test::make_echo_listener(ev);
    ASSERT_TRUE(rig.registered);
    ASSERT_GT(rig.port(), 0u);

    everest::lib::io::tls::tls_client client(config_from_dying_locals(), std::string("127.0.0.1"), rig.port(), 2000);

    std::atomic<bool> done{false};
    client.set_on_ready_action([&client]() {
        everest::lib::io::tls::tls_client_socket::PayloadT msg = {'h', 'i'};
        client.tx(msg);
    });
    client.set_rx_handler([&done](everest::lib::io::tls::tls_client_socket::PayloadT const& payload, auto&) {
        if (!payload.empty()) {
            done = true;
        }
    });
    ASSERT_TRUE(ev.register_event_handler(&client));

    EXPECT_TRUE(test::pump_until(
        ev, [&] { return done.load(); }, 5s))
        << "handshake/echo did not complete from a Config built entirely from destroyed locals";
}
