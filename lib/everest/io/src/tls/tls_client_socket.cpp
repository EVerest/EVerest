// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest

#include <everest/io/tls/detail/tls_result_from_libtls.hpp>
#include <everest/io/tls/tls_client_config.hpp>

#include <cerrno>
#include <string>
#include <utility>

namespace everest::lib::io::tls {

namespace {

bool init_client(std::unique_ptr<::tls::Client>& client, ::tls::Client::config_t const& tls_cfg) {
    client = std::make_unique<::tls::Client>();
    if (!client->init(tls_cfg)) {
        client.reset();
        return false;
    }
    return true;
}

// Reason text for a rejected TLS configuration. tls::Client::init drains the per-thread OpenSSL
// error queue through openssl::log, so nothing is left to read back, and it never names the input
// it rejected, which may not be a file at all. So repeat the configuration without a culprit.
// An unset field prints <unset> and an empty one '': for cipher_list and ciphersuites libtls reads
// nullptr as "use the default".
std::string init_failure_text(::tls::Client::config_t const& cfg) {
    auto field = [](char const* name, char const* value) {
        return value != nullptr ? std::string{name} + "='" + value + "'" : std::string{name} + "=<unset>";
    };
    auto flag = [](char const* name, bool value) { return std::string{name} + (value ? "=true" : "=false"); };
    return "tls::Client::init rejected this configuration and does not report which part of it: " +
           field("certificate_chain_file", cfg.certificate_chain_file) + ", " +
           field("private_key_file", cfg.private_key_file) + ", " +
           field("verify_locations_file", cfg.verify_locations_file) + ", " +
           field("verify_locations_path", cfg.verify_locations_path) + ", " + flag("verify_server", cfg.verify_server) +
           ", " + flag("verify_subject_name", cfg.verify_subject_name) +
           ", min_proto_version=" + std::to_string(cfg.min_proto_version);
}

// Pointers reference the owned Config fields and stay valid for the duration of the
// tls::Client::init call, which copies everything it needs (OpenSSL copies the cipher strings and
// loads the files inside SSL_CTX_*, trusted_ca_keys_data is copied into the client).
::tls::Client::config_t materialize(tls_client_socket::Config::tls_config const& cfg) {
    ::tls::Client::config_t out{};
    out.cipher_list = cfg.cipher_list;
    out.ciphersuites = cfg.ciphersuites;
    out.certificate_chain_file = cfg.certificate_chain_file;
    out.private_key_file = cfg.private_key_file;
    out.private_key_password = cfg.private_key_password;
    out.verify_locations_file = cfg.verify_locations_file;
    out.verify_locations_path = cfg.verify_locations_path;
    out.trusted_ca_keys_data = cfg.trusted_ca_keys_data;
    out.io_timeout_ms = cfg.io_timeout_ms;
    out.min_proto_version = cfg.min_proto_version;
    out.verify_server = cfg.verify_server;
    out.verify_subject_name = cfg.verify_subject_name;
    out.status_request = cfg.status_request;
    out.status_request_v2 = cfg.status_request_v2;
    out.trusted_ca_keys = cfg.trusted_ca_keys;
    return out;
}

} // namespace

bool tls_client_socket::setup(Config cfg, std::string const& remote_host, std::uint16_t remote_port, int timeout_ms) {
    m_host_for_sni = std::move(cfg.host_for_sni);
    auto const tls_cfg = materialize(cfg.tls);
    if (!init_client(m_client, tls_cfg)) {
        // No descriptor exists yet, so an error left at 0 sends get_error() to the TCP socket,
        // which answers EBADF: that names the probe, not the bad configuration.
        m_last_error = EINVAL;
        m_last_error_text = init_failure_text(tls_cfg);
        return false;
    }
    return m_tcp.setup(remote_host, remote_port, timeout_ms);
}

void tls_client_socket::connect(std::function<void(bool, int)> const& cb) {
    m_tcp.connect([this, cb](bool ok, int fd) {
        if (!ok) {
            cb(false, -1);
            return;
        }
        // No handshake here: the event loop drives it via step_handshake() once the fd is monitored.
        m_conn = m_client->wrap_connecting_fd(fd, m_host_for_sni.c_str());
        if (!m_conn) {
            // Same reason the setup() path records one: a code with no text leaves the consumer
            // nothing to act on. The factory reports no reason either, so name what it rules out.
            m_last_error = EPROTO;
            m_last_error_text = "tls::Client::wrap_connecting_fd returned no connection: the SSL context is "
                                "uninitialised or SSL/BIO allocation failed";
            cb(false, -1);
            return;
        }
        // The connection's BIO (BIO_CLOSE) now owns the fd, so m_tcp surrenders its claim.
        m_tcp.release();
        cb(true, m_conn->socket());
    });
}

::tls::Connection* tls_client_socket::connection() const {
    return m_conn.get();
}

io_result tls_client_socket::step_handshake() {
    return detail::to_io_result(m_conn->connect(0));
}

void tls_client_socket::reset_connection() {
    m_conn.reset();
}

int tls_client_socket::get_fd() const {
    if (m_conn) {
        return m_conn->socket();
    }
    return m_tcp.get_fd();
}

int tls_client_socket::get_error() const {
    if (m_last_error != 0) {
        return m_last_error;
    }
    if (!m_conn) {
        return m_tcp.get_error();
    }
    return 0;
}

void tls_client_socket::close() {
    tls_socket_base::close();
    m_tcp.close();
}

tls_client_socket::~tls_client_socket() {
    close();
}

} // namespace everest::lib::io::tls
