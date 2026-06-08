// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest

#include <everest/io/event/fd_event_handler.hpp>
#include <everest/io/tls/tls_listener.hpp>
#include <everest/io/tls/tls_server.hpp>
#include <everest/io/tls/tls_server_socket.hpp>
#include <everest/tls/tls.hpp>

#include <gtest/gtest.h>

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

/// Bytes the client sends and expects echoed back through the server.
const std::vector<std::uint8_t> kPayload = {'p', 'i', 'n', 'g'};

/// Populate a tls_listener::Config with the test PKI and the given bind target.
io::tls::tls_listener::Config make_listener_config(std::string bind_addr, bool ipv6_only) {
    io::tls::tls_listener::Config lcfg;
    lcfg.tls.cipher_list = "ECDHE-ECDSA-AES128-SHA256";
    lcfg.tls.ciphersuites = "";
    auto& chain = lcfg.tls.chains.emplace_back();
    chain.certificate_chain_file = "server_chain.pem";
    chain.private_key_file = "server_priv.pem";
    chain.trust_anchor_file = "server_root_cert.pem";
    chain.ocsp_response_files = {"ocsp_response.der", "ocsp_response.der"};
    lcfg.tls.verify_client = false;
    lcfg.tls.io_timeout_ms = 1000;
    lcfg.bind_addr = std::move(bind_addr);
    lcfg.bind_port = 0;
    lcfg.ipv6_only = ipv6_only;
    return lcfg;
}

/// Blocking TLS client: connect, write kPayload, read the echo, verify it.
/// Runs on a background thread while the main thread drives the event loop.
void run_blocking_client(std::string const& host, std::uint16_t port, std::atomic<bool>& client_ok,
                         std::string& client_error) {
    tls::Client client;
    tls::Client::config_t ccfg;
    ccfg.cipher_list = "ECDHE-ECDSA-AES128-SHA256";
    ccfg.verify_locations_file = "server_root_cert.pem";
    ccfg.io_timeout_ms = 2000;
    ccfg.verify_server = true;
    if (!client.init(ccfg)) {
        client_error = "client.init failed";
        return;
    }

    const std::string port_str = std::to_string(port);
    auto conn = client.connect(host.c_str(), port_str.c_str(), false, 2000);
    if (!conn) {
        client_error = "client.connect returned nullptr";
        return;
    }
    if (conn->connect() != tls::Connection::result_t::success) {
        client_error = "TLS handshake failed on client side";
        return;
    }

    std::size_t written = 0;
    if (conn->write(reinterpret_cast<const std::byte*>(kPayload.data()), kPayload.size(), written, 2000) !=
            tls::Connection::result_t::success ||
        written != kPayload.size()) {
        client_error = "client write failed";
        return;
    }

    std::vector<std::uint8_t> echo;
    echo.reserve(kPayload.size());
    while (echo.size() < kPayload.size()) {
        std::byte buf[256]{};
        std::size_t nread = 0;
        if (conn->read(buf, sizeof buf, nread, 2000) != tls::Connection::result_t::success) {
            client_error = "client read failed";
            return;
        }
        echo.insert(echo.end(), reinterpret_cast<std::uint8_t*>(buf), reinterpret_cast<std::uint8_t*>(buf) + nread);
    }

    if (echo != kPayload) {
        client_error = "client echo mismatch";
        return;
    }

    conn->shutdown();
    client_ok = true;
}

/// Drive a real TLS round-trip through the listener bound to bind_addr.
///
/// A blocking TLS client (on a worker thread) connects, sends kPayload and
/// expects it echoed back. The listener accepts the connection, the accept
/// callback echoes received bytes and registers the yielded tls_server with the
/// same fd_event_handler. The main thread polls the loop until the server-side
/// rx fires AND the client confirms the round-trip, or the 5s deadline expires.
void run_round_trip(std::string const& bind_addr, bool ipv6_only) {
    io::tls::tls_listener listener(make_listener_config(bind_addr, ipv6_only));

    const auto port = listener.listen_port();
    ASSERT_GT(port, 0u) << "listener bound to port 0 unexpectedly";

    io::event::fd_event_handler handler;

    std::atomic<bool> server_rx_fired{false};
    std::atomic<std::uint16_t> captured_peer_port{0};
    // The yielded tls_server must outlive the accept callback; the loop drives
    // its handshake/rx/tx after register_event_handler().
    std::unique_ptr<io::tls::tls_server> server_conn;

    listener.set_accept_callback(
        [&](std::unique_ptr<io::tls::tls_server> srv, std::string /*ip*/, std::uint16_t peer_port) {
            captured_peer_port = peer_port;
            srv->set_rx_handler([&server_rx_fired](io::tls::tls_server_socket::PayloadT const& payload, auto& self) {
                server_rx_fired = true;
                self.tx(payload); // echo
            });
            handler.register_event_handler(srv.get());
            server_conn = std::move(srv);
        });

    ASSERT_TRUE(handler.register_event_handler(&listener));

    std::atomic<bool> client_ok{false};
    std::string client_error;
    std::thread client_thread([&]() { run_blocking_client(bind_addr, port, client_ok, client_error); });

    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while ((!server_rx_fired || !client_ok) && std::chrono::steady_clock::now() < deadline) {
        handler.poll(50ms);
    }

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
