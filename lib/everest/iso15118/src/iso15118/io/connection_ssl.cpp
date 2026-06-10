// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Pionix GmbH and Contributors to EVerest
#include <iso15118/io/connection_ssl.hpp>

#include <cassert>
#include <cinttypes>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

#include <everest/tls/tls.hpp>

#include <iso15118/detail/helper.hpp>
#include <iso15118/detail/io/socket_helper.hpp>

namespace iso15118::io {

struct SSLContext {
    std::unique_ptr<tls::Server> server;
    tls::Server::ConnectionPtr connection;
    int listen_fd{-1};
    std::optional<sha512_hash_t> vehicle_cert_hash{std::nullopt};
};

namespace {

constexpr auto DEFAULT_SOCKET_BACKLOG = 4;
constexpr auto TLS_PORT = 50000;

std::string_view result_name(tls::Connection::result_t r) {
    switch (r) {
    case tls::Connection::result_t::success:
        return "success";
    case tls::Connection::result_t::closed:
        return "closed";
    case tls::Connection::result_t::timeout:
        return "timeout";
    case tls::Connection::result_t::want_read:
        return "want_read";
    case tls::Connection::result_t::want_write:
        return "want_write";
    }
    return "unknown";
}

std::vector<tls::Server::certificate_config_t> build_chain_configs(const config::SSLConfig& cfg) {
    std::vector<tls::Server::certificate_config_t> chains;
    chains.reserve(cfg.chains.size());

    for (const auto& src : cfg.chains) {
        std::vector<tls::ConfigItem> ocsp_items;
        ocsp_items.reserve(src.ocsp_response_files.size());
        for (const auto& f : src.ocsp_response_files) {
            ocsp_items.emplace_back(f);
        }

        tls::Server::certificate_config_t chain_cfg{};
        chain_cfg.certificate_chain_file = src.path_certificate_chain;
        chain_cfg.private_key_file = src.path_certificate_key;
        if (src.private_key_password.has_value()) {
            chain_cfg.private_key_password = src.private_key_password.value();
        }
        chain_cfg.ocsp_response_files = std::move(ocsp_items);
        chains.push_back(std::move(chain_cfg));
    }

    return chains;
}

tls::Server::config_t make_tls_server_config(const config::SSLConfig& cfg, const std::string& interface_name,
                                             int listen_fd, std::vector<tls::Server::certificate_config_t> chains) {
    tls::Server::config_t out{};
    out.cipher_list = "ECDHE-ECDSA-AES128-SHA256";
    out.ciphersuites = "TLS_AES_256_GCM_SHA384:TLS_CHACHA20_POLY1305_SHA256";
    out.chains = std::move(chains);
    out.verify_locations_file = cfg.path_certificate_v2g_root;
    if (not cfg.path_certificate_mo_root.empty()) {
        // The vehicle TLS leaf normally chains to the V2G root; the MO root is an
        // additional anchor for deployments where it chains to the MO PKI instead.
        out.verify_locations_additional_files = {tls::ConfigItem{cfg.path_certificate_mo_root}};
    }
    out.io_timeout_ms = -1;
    out.verify_client = false; // verify_client_on_tls13 upgrades to require a peer cert once TLS 1.3 is negotiated
    out.verify_client_on_tls13 = true;
    out.socket = listen_fd;
    out.ipv6_only = true;
    out.tls_key_logging = cfg.enable_tls_key_logging;
    out.tls_key_logging_path = cfg.tls_key_logging_path.string();
    out.enforce_tls_1_3 = cfg.enforce_tls_1_3;
    if (cfg.enable_tls_key_logging) {
        // tls::Server records cfg.host as the keylog UDP interface; mirror today's behavior
        // of binding the keylog socket on the same interface as the TLS listener.
        out.host = interface_name;
    }
    return out;
}

} // namespace

ConnectionSSL::ConnectionSSL(PollManager& poll_manager_, const std::string& interface_name_,
                             const config::SSLConfig& ssl_config) :
    poll_manager(poll_manager_), ssl(std::make_unique<SSLContext>()) {

    if (ssl_config.chains.empty()) {
        log_and_throw("SSLConfig has no chains configured");
    }

    sockaddr_in6 address;
    if (not get_first_sockaddr_in6_for_interface(interface_name_, address)) {
        const auto msg = "Failed to get ipv6 socket address for interface " + interface_name_;
        log_and_throw(msg.c_str());
    }

    end_point.port = TLS_PORT;
    memcpy(&end_point.address, &address.sin6_addr, sizeof(address.sin6_addr));

    const auto address_name = sockaddr_in6_to_name(address);
    if (not address_name) {
        const auto msg =
            "Failed to determine string representation of ipv6 socket address for interface " + interface_name_;
        log_and_throw(msg.c_str());
    }

    logf_info("Start TLS server [%s]:%" PRIu16, address_name.get(), end_point.port);

    ssl->listen_fd = socket(AF_INET6, SOCK_STREAM, 0);
    if (ssl->listen_fd == -1) {
        log_and_throw("Failed to create an ipv6 socket");
    }

    address.sin6_port = htons(end_point.port);

    int optval_tmp{1};
    const auto set_reuseaddr = setsockopt(ssl->listen_fd, SOL_SOCKET, SO_REUSEADDR, &optval_tmp, sizeof(optval_tmp));
    if (set_reuseaddr == -1) {
        log_and_throw("setsockopt(SO_REUSEADDR) failed");
    }

    const auto set_reuseport = setsockopt(ssl->listen_fd, SOL_SOCKET, SO_REUSEPORT, &optval_tmp, sizeof(optval_tmp));
    if (set_reuseport == -1) {
        log_and_throw("setsockopt(SO_REUSEPORT) failed");
    }

    const auto bind_result = bind(ssl->listen_fd, reinterpret_cast<const struct sockaddr*>(&address), sizeof(address));
    if (bind_result == -1) {
        const auto error = "Failed to bind ipv6 socket to interface " + interface_name_;
        log_and_throw(error.c_str());
    }

    const auto listen_result = listen(ssl->listen_fd, DEFAULT_SOCKET_BACKLOG);
    if (listen_result == -1) {
        log_and_throw("Listen on socket failed");
    }

    auto chains = build_chain_configs(ssl_config);
    auto cfg = make_tls_server_config(ssl_config, interface_name_, ssl->listen_fd, std::move(chains));

    ssl->server = std::make_unique<tls::Server>();
    const auto state = ssl->server->init(cfg, []() { return tls::Server::OptionalConfig{}; });
    if (state != tls::Server::state_t::init_complete) {
        log_and_throw("Failed to initialise tls::Server for ConnectionSSL");
    }

    poll_manager.register_fd(ssl->listen_fd, [this]() { this->handle_connect(); });
}

ConnectionSSL::~ConnectionSSL() = default;

void ConnectionSSL::set_event_callback(const ConnectionEventCallback& callback) {
    event_callback = callback;
}

Ipv6EndPoint ConnectionSSL::get_public_endpoint() const {
    return end_point;
}

std::optional<sha512_hash_t> ConnectionSSL::get_vehicle_cert_hash() const {
    // Cached once on the OPEN event in handle_data(); the live tls::Connection
    // is gone after close(), so the cache is the single source of truth.
    return ssl->vehicle_cert_hash;
}

void ConnectionSSL::write(const uint8_t* buf, size_t len) {
    assert(handshake_complete);
    assert(ssl->connection != nullptr);

    std::size_t writebytes = 0;
    const auto result =
        ssl->connection->write(reinterpret_cast<const std::byte*>(buf), len, writebytes, /*timeout_ms=*/-1);

    if (result != tls::Connection::result_t::success || writebytes != len) {
        std::string msg = "Failed to write on TLS connection: ";
        msg += result_name(result);
        msg += " (" + std::to_string(writebytes) + "/" + std::to_string(len) + " bytes)";
        const auto ssl_err = ssl->connection->last_error();
        if (not ssl_err.empty()) {
            msg += " openssl=";
            msg += ssl_err;
        }
        log_and_throw(msg.c_str());
    }
}

ReadResult ConnectionSSL::read(uint8_t* buf, size_t len) {
    assert(handshake_complete);
    assert(ssl->connection != nullptr);

    std::size_t readbytes = 0;
    const auto result = ssl->connection->read(reinterpret_cast<std::byte*>(buf), len, readbytes, /*timeout_ms=*/0);

    switch (result) {
    case tls::Connection::result_t::success:
        return {readbytes < len, readbytes};
    case tls::Connection::result_t::want_read:
    case tls::Connection::result_t::want_write:
    case tls::Connection::result_t::timeout:
        return {true, 0};
    case tls::Connection::result_t::closed:
        return {false, 0, true};
    default:
        break;
    }

    std::string msg = "Failed to read from TLS connection: ";
    msg += result_name(result);
    const auto ssl_err = ssl->connection->last_error();
    if (not ssl_err.empty()) {
        msg += " openssl=";
        msg += ssl_err;
    }
    log_and_throw(msg.c_str());
    return {false, 0};
}

void ConnectionSSL::handle_connect() {

    sockaddr_in6 peer_addr{};
    socklen_t peer_len = sizeof(peer_addr);
    const int accepted_fd = ::accept(ssl->listen_fd, reinterpret_cast<sockaddr*>(&peer_addr), &peer_len);
    if (accepted_fd < 0) {
        log_and_throw("Failed to accept incoming TLS connection");
    }

    char host[NI_MAXHOST] = {0};
    char service[NI_MAXSERV] = {0};
    const auto gni_result = ::getnameinfo(reinterpret_cast<sockaddr*>(&peer_addr), peer_len, host, sizeof(host),
                                          service, sizeof(service), NI_NUMERICHOST | NI_NUMERICSERV);
    if (gni_result != 0) {
        logf_warning("getnameinfo() failed for incoming connection: %s", gai_strerror(gni_result));
    }

    logf_info("Incoming connection from [%s]:%s", host, service);

    poll_manager.unregister_fd(ssl->listen_fd);
    ::close(ssl->listen_fd);
    ssl->listen_fd = -1;

    call_if_available(event_callback, ConnectionEvent::ACCEPTED);

    ssl->connection = ssl->server->wrap_accepted_fd(accepted_fd, host, service);
    if (ssl->connection == nullptr) {
        ::close(accepted_fd);
        log_and_throw("Failed to wrap accepted TLS socket");
    }

    // The ServerConnection owns the fd from here on; key the poll callback on it.
    poll_manager.register_fd(ssl->connection->socket(), [this]() { this->handle_data(); });
}

void ConnectionSSL::handle_data() {
    if (ssl->connection == nullptr) {
        return;
    }

    if (not handshake_complete) {
        const auto result = ssl->connection->accept(/*timeout_ms=*/0);
        switch (result) {
        case tls::Connection::result_t::success:
            break;
        case tls::Connection::result_t::want_read:
        case tls::Connection::result_t::want_write:
        case tls::Connection::result_t::timeout:
            return;
        case tls::Connection::result_t::closed:
            // convert() folds all fatal handshake outcomes (peer close, alert,
            // protocol error) into closed, so this teardown handles every
            // non-success, non-blocking handshake result.
            logf_error("TLS handshake failed: connection closed");
            this->close();
            return;
        default:
            log_and_throw("Failed to complete TLS handshake");
        }

        logf_info("Handshake complete!");
        ssl->vehicle_cert_hash = ssl->connection->peer_certificate_sha512();
        handshake_complete = true;
        call_if_available(event_callback, ConnectionEvent::OPEN);
        return;
    }

    call_if_available(event_callback, ConnectionEvent::NEW_DATA);
}

void ConnectionSSL::close() {
    // Idempotency / re-entry guard: the connection is reset before CLOSED is
    // delivered below, so a re-entrant or repeated close() is a no-op.
    if (ssl->connection == nullptr) {
        return;
    }

    logf_info("Closing TLS connection");

    const auto result = ssl->connection->shutdown(/*timeout_ms=*/0);
    if (result != tls::Connection::result_t::success && result != tls::Connection::result_t::closed) {
        logf_error("TLS shutdown returned non-success result");
    }

    // Unregistering from within the accept fd's own poll callback is safe: it is
    // the only non-event fd registered at this point (listen_fd was dropped in
    // handle_connect), so PollManager::poll has no further entries to visit.
    const int fd = ssl->connection->socket();
    if (fd != -1) {
        poll_manager.unregister_fd(fd);
    }

    // Destroying the ServerConnection tears down the SSL state and closes the
    // underlying socket it owns. We must do this explicitly rather than relying
    // on SSLContext's destructor so that the close happens at a deterministic
    // point relative to the CLOSED event we deliver below.
    ssl->connection.reset();

    logf_info("TLS connection closed");

    call_if_available(event_callback, ConnectionEvent::CLOSED);
}

} // namespace iso15118::io
