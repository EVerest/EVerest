// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest

#include <everest/io/event/fd_event_handler.hpp>
#include <everest/io/tls/tls_listener.hpp>
#include <everest/io/tls/tls_server_connection.hpp>
#include <everest/tls/tls.hpp>

#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>

using namespace std::chrono_literals;

TEST(TlsListener, AcceptCallbackFires) {
    everest::lib::io::tls::tls_listener::Config lcfg;
    lcfg.tls.cipher_list = "ECDHE-ECDSA-AES128-SHA256";
    lcfg.tls.ciphersuites = "";
    auto& chain = lcfg.tls.chains.emplace_back();
    chain.certificate_chain_file = "server_chain.pem";
    chain.private_key_file = "server_priv.pem";
    chain.trust_anchor_file = "server_root_cert.pem";
    chain.ocsp_response_files = {"ocsp_response.der", "ocsp_response.der"};
    lcfg.tls.verify_client = false;
    lcfg.tls.io_timeout_ms = 1000;
    lcfg.bind_addr = "127.0.0.1";
    lcfg.bind_port = 0;

    everest::lib::io::tls::tls_listener listener(std::move(lcfg));

    const auto port = listener.listen_port();
    ASSERT_GT(port, 0u) << "listener bound to port 0 unexpectedly";

    std::atomic<bool> accept_fired{false};
    listener.set_accept_callback([&](std::unique_ptr<everest::lib::io::tls::tls_server_connection> /*conn*/,
                                     std::string /*ip*/, std::uint16_t /*peer_port*/) { accept_fired = true; });

    everest::lib::io::event::fd_event_handler handler;
    ASSERT_TRUE(handler.register_event_handler(&listener));

    std::thread client_thread([&]() {
        const int sock = ::socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) {
            return;
        }
        sockaddr_in sa{};
        sa.sin_family = AF_INET;
        sa.sin_addr.s_addr = ::inet_addr("127.0.0.1");
        sa.sin_port = htons(port);
        ::connect(sock, reinterpret_cast<sockaddr*>(&sa), sizeof(sa));
        std::this_thread::sleep_for(200ms);
        ::close(sock);
    });

    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (!accept_fired && std::chrono::steady_clock::now() < deadline) {
        handler.poll(200ms);
    }

    ASSERT_TRUE(accept_fired) << "tls_listener accept callback did not fire within 5 seconds";

    client_thread.join();
}

TEST(TlsListener, AcceptCallbackFiresIPv6) {
    everest::lib::io::tls::tls_listener::Config lcfg;
    lcfg.tls.cipher_list = "ECDHE-ECDSA-AES128-SHA256";
    lcfg.tls.ciphersuites = "";
    auto& chain = lcfg.tls.chains.emplace_back();
    chain.certificate_chain_file = "server_chain.pem";
    chain.private_key_file = "server_priv.pem";
    chain.trust_anchor_file = "server_root_cert.pem";
    chain.ocsp_response_files = {"ocsp_response.der", "ocsp_response.der"};
    lcfg.tls.verify_client = false;
    lcfg.tls.io_timeout_ms = 1000;
    lcfg.bind_addr = "::1";
    lcfg.bind_port = 0;
    lcfg.ipv6_only = true;

    everest::lib::io::tls::tls_listener listener(std::move(lcfg));

    const auto port = listener.listen_port();
    ASSERT_GT(port, 0u) << "IPv6 listener bound to port 0 unexpectedly";

    std::atomic<bool> accept_fired{false};
    listener.set_accept_callback([&](std::unique_ptr<everest::lib::io::tls::tls_server_connection> /*conn*/,
                                     std::string /*ip*/, std::uint16_t /*peer_port*/) { accept_fired = true; });

    everest::lib::io::event::fd_event_handler handler;
    ASSERT_TRUE(handler.register_event_handler(&listener));

    std::thread client_thread([&]() {
        const int sock = ::socket(AF_INET6, SOCK_STREAM, 0);
        if (sock < 0) {
            return;
        }
        sockaddr_in6 sa6{};
        sa6.sin6_family = AF_INET6;
        ::inet_pton(AF_INET6, "::1", &sa6.sin6_addr);
        sa6.sin6_port = htons(port);
        ::connect(sock, reinterpret_cast<sockaddr*>(&sa6), sizeof(sa6));
        std::this_thread::sleep_for(200ms);
        ::close(sock);
    });

    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (!accept_fired && std::chrono::steady_clock::now() < deadline) {
        handler.poll(200ms);
    }

    ASSERT_TRUE(accept_fired) << "IPv6 tls_listener accept callback did not fire within 5 seconds";

    client_thread.join();
}
