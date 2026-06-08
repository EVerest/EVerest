// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest

#include <everest/io/event/fd_event_handler.hpp>
#include <everest/io/tls/tls_client.hpp>
#include <everest/io/tls/tls_client_socket.hpp>
#include <everest/tls/tls.hpp>

#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

namespace {

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
    tls::Server::config_t scfg;
    scfg.cipher_list = "ECDHE-ECDSA-AES128-SHA256";
    scfg.ciphersuites = "";
    auto& chain = scfg.chains.emplace_back();
    chain.certificate_chain_file = "server_chain.pem";
    chain.private_key_file = "server_priv.pem";
    chain.trust_anchor_file = "server_root_cert.pem";
    chain.ocsp_response_files = {"ocsp_response.der", "ocsp_response.der"};
    scfg.host = "127.0.0.1";
    scfg.service = port_str.c_str();
    scfg.ipv6_only = false;
    scfg.verify_client = false;
    scfg.io_timeout_ms = 1000;
    scfg.socket = listen_fd; // bypass init_socket()
    return scfg;
}

/// Build the client Config shared by the tests.
everest::lib::io::tls::tls_client_socket::Config make_client_config() {
    everest::lib::io::tls::tls_client_socket::Config cfg;
    cfg.tls.cipher_list = "ECDHE-ECDSA-AES128-SHA256";
    cfg.tls.ciphersuites = "";
    cfg.tls.verify_locations_file = "server_root_cert.pem";
    cfg.tls.io_timeout_ms = 1000;
    cfg.tls.verify_server = true;
    cfg.host_for_sni = "localhost";
    return cfg;
}

} // namespace

namespace everest::lib::io::tls {
// Test seam (no friendship): re-expose the otherwise-internal tcp_socket so the
// ownership-surrender checks below can confirm m_tcp released its fd to the BIO.
struct testable_client_socket : tls_client_socket {
    using tls_client_socket::m_tcp;
};
} // namespace everest::lib::io::tls

// After the async TCP connect + fd wrap, connect() releases m_tcp once it
// returns; the connection owns the fd, m_tcp must have surrendered it. This
// exercises the UNCHANGED synchronous tls_client_socket::connect() wrap path.
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
    sock.connect([&](bool o, int /*fd*/) { ok = o; }); // synchronous; returns AFTER release
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
    everest::lib::io::tls::tls_client client(make_client_config(), std::string("127.0.0.1"), bound_port, 2000);

    everest::lib::io::event::fd_event_handler ev;

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

    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (running && std::chrono::steady_clock::now() < deadline) {
        ev.poll(50ms);
        ev.run_actions();
    }
    running = false;

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
// non-routable TEST-NET-2 address so the connect never resolves within the
// test window; the connect worker is still blocked inside m_socket.connect()
// when the client is unregistered and destroyed. Destruction must join the
// worker before tearing down m_socket, so the worker cannot touch a freed
// socket (no use-after-free) and cannot outlive *this. Under ThreadSanitizer
// this would flag the prior detached-worker race directly; without tsan the
// test pins the deterministic invariant that destruction blocks until the
// worker has joined: with the connect still pending (bounded by the connect
// timeout), tearing down must not return before the worker stops touching the
// socket. The old detached-worker path returned immediately while the worker
// kept running; the joined path returns only after the worker is done.
TEST(TlsClient, teardown_during_pending_connect_is_clean) {
    everest::lib::io::event::fd_event_handler ev;
    constexpr int connect_timeout_ms = 500;
    std::chrono::steady_clock::time_point teardown_start{};
    {
        everest::lib::io::tls::tls_client client(make_client_config(), std::string("198.51.100.1"), 4433,
                                                 connect_timeout_ms);
        ASSERT_TRUE(ev.register_event_handler(&client));
        // Pump the loop so the connect worker is mid-flight (blocked inside
        // m_socket.connect() on the unreachable host) before teardown.
        for (int i = 0; i < 3; ++i) {
            ev.poll(50ms);
            ev.run_actions();
        }
        // Start the clock just before teardown begins: unregister runs stop()
        // and the closing brace runs the destructor — both must wait for the
        // worker to join while the connect is still pending.
        teardown_start = std::chrono::steady_clock::now();
        ev.unregister_event_handler(&client);
        // client destroyed at the closing brace below, connect still pending —
        // teardown must join the worker, not return into a UAF window.
    }
    const auto teardown_elapsed = std::chrono::steady_clock::now() - teardown_start;

    // The connect worker is blocked inside m_socket.connect() on the unreachable
    // host for ~connect_timeout_ms; a joined teardown cannot return until that
    // blocking connect finishes. The old detached-worker path returned at once
    // (the worker raced on free'd state). Assert teardown actually waited for the
    // worker, i.e. it blocked for a meaningful fraction of the connect timeout.
    EXPECT_GE(teardown_elapsed, std::chrono::milliseconds(connect_timeout_ms / 2))
        << "teardown returned before the pending connect worker joined "
           "(detached/non-joined worker can outlive the client and touch freed state)";

    // Loop stays healthy after teardown; no crash/hang.
    for (int i = 0; i < 3; ++i) {
        ev.poll(10ms);
        ev.run_actions();
    }
    SUCCEED();
}

// Registering the same client twice without an intervening unregister must be
// rejected, not re-run start(). fd_event_handler forwards an interface register
// straight to register_events() with no interface-level dedup, so a second
// register would call start() again and move-assign a std::thread onto the
// still-joinable connect worker -> std::terminate. The register_events() guard
// makes the second register a clean no-op returning false.
TEST(TlsClient, double_register_is_rejected_not_fatal) {
    everest::lib::io::event::fd_event_handler ev;
    everest::lib::io::tls::tls_client client(make_client_config(), std::string("198.51.100.1"), 4433, 500);

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
