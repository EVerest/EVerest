// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/io/connection_client_ssl.hpp>

#include <algorithm>
#include <cassert>
#include <cstring>
#include <filesystem>
#include <fstream>

#include <arpa/inet.h>
#include <net/if.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/ssl.h>

#include <iso15118/detail/helper.hpp>
#include <iso15118/detail/io/helper_ssl.hpp>
#include <iso15118/detail/io/socket_helper.hpp>
#include <iso15118/io/sdp_server.hpp>
#include <iso15118/io/tls_ciphers.hpp>

namespace std {
template <> class default_delete<SSL> {
public:
    void operator()(SSL* ptr) const {
        ::SSL_free(ptr);
    }
};
template <> class default_delete<SSL_CTX> {
public:
    void operator()(SSL_CTX* ptr) const {
        ::SSL_CTX_free(ptr);
    }
};
} // namespace std

namespace iso15118::io {

struct SSLClientContext {
    std::unique_ptr<SSL_CTX> ssl_ctx;
    std::unique_ptr<SSL> ssl;
    int fd{-1};
    std::string interface_name;
    bool enable_key_logging{false};
    std::filesystem::path tls_key_log_file_path{};
    std::unique_ptr<io::TlsKeyLoggingServer> key_server;
    bool enforce_tls_1_3{false};
};

namespace {

constexpr auto CONNECT_TIMEOUT_MS = 5000;

int ssl_keylog_server_index{-1};
int ssl_keylog_file_index{-1};

void keylog_callback(const SSL* ssl, const char* line) {
    auto key_logging_server = static_cast<io::TlsKeyLoggingServer*>(SSL_get_ex_data(ssl, ssl_keylog_server_index));

    std::string key_log_msg = "TLS Handshake keys on port ";
    key_log_msg += std::to_string(key_logging_server->get_port()) + ": ";
    key_log_msg += std::string(line);

    logf_info(key_log_msg.c_str());

    if (key_logging_server->get_fd() != -1) {
        const auto result = key_logging_server->send(line);
        if (not cmp_equal(result, strlen(line))) {
            const auto error_msg = adding_err_msg("key_logging_server send() failed");
            logf_error(error_msg.c_str());
        }
    }

    const auto keylog_file_path =
        static_cast<std::filesystem::path*>(SSL_CTX_get_ex_data(SSL_get_SSL_CTX(ssl), ssl_keylog_file_index));

    if (not keylog_file_path->empty()) {
        std::ofstream ofs;
        ofs.open(keylog_file_path->string(), std::ofstream::out | std::ofstream::app);
        ofs << line << std::endl;
        ofs.close();
    }
}

int private_key_callback(char* buf, int size, [[maybe_unused]] int rwflag, void* userdata) {
    const auto* password = static_cast<const std::string*>(userdata);
    const std::size_t max_pass_len = (size - 1); // we exclude the endline
    const std::size_t max_copy_chars =
        std::min(max_pass_len, password->length()); // truncate if pass is too large and buffer too small

    std::memset(buf, 0, size);
    std::memcpy(buf, password->c_str(), max_copy_chars);

    return max_copy_chars;
}

SSL_CTX* init_ssl(const config::SSLConfig& ssl_config, bool verify_server_certificate) {

    const SSL_METHOD* method = TLS_client_method();
    const auto ctx = SSL_CTX_new(method);

    if (ctx == nullptr) {
        log_and_raise_openssl_error("Failed in SSL_CTX_new()");
    }

    // ISO 15118-20 pins TLS 1.3 (min == max); ISO 15118-2 uses TLS 1.2, so cap both min and max at 1.2
    // to avoid silently negotiating 1.3 despite the 1.3 ciphersuites configured below.
    const auto pinned_version = ssl_config.enforce_tls_1_3 ? TLS1_3_VERSION : TLS1_2_VERSION;

    const int result_set_min_proto_version = SSL_CTX_set_min_proto_version(ctx, pinned_version);
    const int result_set_max_proto_version = SSL_CTX_set_max_proto_version(ctx, pinned_version);

    if (ssl_config.enforce_tls_1_3) {
        SSL_CTX_clear_options(ctx, SSL_OP_ENABLE_MIDDLEBOX_COMPAT);
    }

    if (result_set_min_proto_version == 0) {
        log_and_raise_openssl_error("Failed in SSL_CTX_set_min_proto_version()");
    }

    if (result_set_max_proto_version == 0) {
        log_and_raise_openssl_error("Failed in SSL_CTX_set_max_proto_version()");
    }

    if (not ssl_config.enforce_tls_1_3 and SSL_CTX_set_cipher_list(ctx, TLS1_2_CIPHERSUITES) == 0) {
        log_and_raise_openssl_error("Failed in SSL_CTX_set_cipher_list()");
    }

    if (SSL_CTX_set_ciphersuites(ctx, TLS1_3_CIPHERSUITES) == 0) {
        log_and_raise_openssl_error("Failed in SSL_CTX_set_ciphersuites()");
    }

    // The EV presents its OEM/vehicle leaf certificate to the SECC. mTLS is mandatory for ISO 15118-20
    // (enforce_tls_1_3), so the client cert/key are required there; for TLS 1.2 (ISO 15118-2) the SECC
    // does not request a client certificate, so absent paths are tolerated.
    const bool have_client_cert = not ssl_config.path_certificate_chain.empty();
    const bool have_client_key = not ssl_config.path_certificate_key.empty();

    if (have_client_cert != have_client_key) {
        log_and_throw("Exactly one of the client certificate chain and key is configured; both (mTLS) or "
                      "neither are required");
    }

    if (ssl_config.enforce_tls_1_3 and (not have_client_cert or not have_client_key)) {
        log_and_throw("A client certificate chain and key are required for TLS 1.3 (mTLS) but not configured");
    }

    if (have_client_cert and SSL_CTX_use_certificate_chain_file(ctx, ssl_config.path_certificate_chain.c_str()) != 1) {
        log_and_raise_openssl_error("Failed in SSL_CTX_use_certificate_chain_file()");
    }

    // INFO: the password callback uses a non-const argument
    if (ssl_config.private_key_password.has_value()) {
        // Lifetime of the password is important because using a callback we'll require a valid ref
        SSL_CTX_set_default_passwd_cb_userdata(
            ctx, &const_cast<config::SSLConfig&>(ssl_config).private_key_password.value());
        SSL_CTX_set_default_passwd_cb(ctx, private_key_callback);
    }

    if (have_client_key and
        SSL_CTX_use_PrivateKey_file(ctx, ssl_config.path_certificate_key.c_str(), SSL_FILETYPE_PEM) != 1) {
        log_and_raise_openssl_error("Failed in SSL_CTX_use_PrivateKey_file()");
    }

    if (verify_server_certificate) {
        // Loading the V2G root certificate to verify the SECC leaf certificate
        if (SSL_CTX_load_verify_file(ctx, ssl_config.path_certificate_v2g_root.c_str()) == 0) {
            logf_error("Verify V2G root not found!");
        }
        // NOTE: no hostname check is performed (Josev parity)
        SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, nullptr);
    } else {
        SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, nullptr);
    }

    if (ssl_config.enable_tls_key_logging) {
        ssl_keylog_file_index = SSL_CTX_get_ex_new_index(0, nullptr, nullptr, nullptr, nullptr);
        ssl_keylog_server_index = SSL_get_ex_new_index(0, nullptr, nullptr, nullptr, nullptr);

        if (ssl_keylog_file_index == -1 or ssl_keylog_server_index == -1) {
            auto error_msg = std::string("_get_ex_new_index failed: ssl_keylog_file_index: ");
            error_msg += std::to_string(ssl_keylog_file_index);
            error_msg += ", ssl_keylog_server_index: " + std::to_string(ssl_keylog_server_index);
            logf_error(error_msg.c_str());
        } else {
            SSL_CTX_set_keylog_callback(ctx, keylog_callback);
        }
    }

    return ctx;
}
} // namespace

ConnectionClientSSL::ConnectionClientSSL(PollManager& poll_manager_, const std::string& interface_name_,
                                         const config::SSLConfig& ssl_config, const Ipv6EndPoint& secc_endpoint,
                                         bool verify_server_certificate) :
    poll_manager(poll_manager_), ssl(std::make_unique<SSLClientContext>()), end_point(secc_endpoint) {

    ssl->interface_name = interface_name_;
    ssl->enable_key_logging = ssl_config.enable_tls_key_logging;
    ssl->enforce_tls_1_3 = ssl_config.enforce_tls_1_3;

    const auto ssl_ctx = init_ssl(ssl_config, verify_server_certificate);
    ssl->ssl_ctx = std::unique_ptr<SSL_CTX>(ssl_ctx);

    if (ssl_keylog_file_index != -1) {
        ssl->tls_key_log_file_path = ssl_config.tls_key_logging_path / "tls_session_keys.log";
        SSL_CTX_set_ex_data(ssl->ssl_ctx.get(), ssl_keylog_file_index, &ssl->tls_key_log_file_path);
    }

    ssl->fd = socket(AF_INET6, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (ssl->fd == -1) {
        log_and_throw("Failed to create an ipv6 socket");
    }

    sockaddr_in6 address{};
    address.sin6_family = AF_INET6;
    address.sin6_port = htons(secc_endpoint.port);
    memcpy(&address.sin6_addr, secc_endpoint.address, sizeof(secc_endpoint.address));
    address.sin6_scope_id = if_nametoindex(interface_name_.c_str());

    const auto connect_result = ::connect(ssl->fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address));
    if (connect_result == -1 and errno != EINPROGRESS) {
        const auto error_msg = adding_err_msg("Failed to connect() to SECC");
        log_and_throw(error_msg.c_str());
    }

    // NOTE: the PollManager only monitors POLLIN, so the non-blocking connect is completed here by
    // waiting for the socket to become writable
    if (connect_result == -1) {
        struct pollfd pfd {
            ssl->fd, POLLOUT, 0
        };

        const auto poll_result = ::poll(&pfd, 1, CONNECT_TIMEOUT_MS);
        if (poll_result == -1) {
            const auto error_msg = adding_err_msg("Failed to poll() during connect");
            log_and_throw(error_msg.c_str());
        }
        if (poll_result == 0) {
            log_and_throw("Timeout while connecting to SECC");
        }

        int so_error{0};
        socklen_t len = sizeof(so_error);
        if (getsockopt(ssl->fd, SOL_SOCKET, SO_ERROR, &so_error, &len) == -1) {
            const auto error_msg = adding_err_msg("Failed to getsockopt(SO_ERROR)");
            log_and_throw(error_msg.c_str());
        }
        if (so_error != 0) {
            errno = so_error;
            const auto error_msg = adding_err_msg("Failed to connect() to SECC");
            log_and_throw(error_msg.c_str());
        }
    }

    ssl->ssl = std::unique_ptr<SSL>(SSL_new(ssl->ssl_ctx.get()));
    const auto socket_bio = BIO_new_socket(ssl->fd, BIO_CLOSE);

    const auto ssl_ptr = ssl->ssl.get();

    SSL_set_bio(ssl_ptr, socket_bio, socket_bio);
    SSL_set_connect_state(ssl_ptr);
    SSL_set_app_data(ssl_ptr, this);

    if (ssl->enable_key_logging) {
        ssl->key_server = std::make_unique<io::TlsKeyLoggingServer>(ssl->interface_name, secc_endpoint.port);
        SSL_set_ex_data(ssl_ptr, ssl_keylog_server_index, ssl->key_server.get());
    }

    poll_manager.register_fd(ssl->fd, [this]() { this->handle_data(); });
}

ConnectionClientSSL::~ConnectionClientSSL() = default;

void ConnectionClientSSL::set_event_callback(const ConnectionEventCallback& callback) {
    event_callback = callback;

    // kick off the handshake, the client sends the first flight (ClientHello)
    if (not handshake_complete) {
        drive_handshake();
    }
}

Ipv6EndPoint ConnectionClientSSL::get_public_endpoint() const {
    return end_point;
}

void ConnectionClientSSL::write(const uint8_t* buf, size_t len) {
    assert(handshake_complete);

    size_t writebytes = 0;
    const auto ssl_ptr = ssl->ssl.get();

    const auto ssl_write_result = SSL_write_ex(ssl_ptr, buf, len, &writebytes);

    if (ssl_write_result <= 0) {
        const auto ssl_err_raw = SSL_get_error(ssl_ptr, ssl_write_result);
        log_and_raise_openssl_error("Failed to SSL_write_ex(): " + std::to_string(ssl_err_raw));
    } else if (writebytes != len) {
        log_and_throw("Didn't complete to write");
    }
}

ReadResult ConnectionClientSSL::read(uint8_t* buf, size_t len) {
    assert(handshake_complete);

    size_t readbytes = 0;
    const auto ssl_ptr = ssl->ssl.get();

    const auto ssl_read_result = SSL_read_ex(ssl_ptr, buf, len, &readbytes);

    if (ssl_read_result > 0) {
        const auto would_block = (readbytes < len);
        return {would_block, readbytes};
    }

    const auto ssl_error = SSL_get_error(ssl_ptr, ssl_read_result);

    if ((ssl_error == SSL_ERROR_WANT_READ) or (ssl_error == SSL_ERROR_WANT_WRITE)) {
        return {true, 0};
    }

    log_and_raise_openssl_error("Failed to SSL_read_ex(): " + std::to_string(ssl_error));

    return {false, 0};
}

bool ConnectionClientSSL::drive_handshake() {
    const auto ssl_ptr = ssl->ssl.get();

    const auto ssl_handshake_result = SSL_connect(ssl_ptr);

    if (ssl_handshake_result <= 0) {
        const auto ssl_error = SSL_get_error(ssl_ptr, ssl_handshake_result);

        if ((ssl_error == SSL_ERROR_WANT_READ) or (ssl_error == SSL_ERROR_WANT_WRITE)) {
            return false;
        }
        log_and_raise_openssl_error("Failed to SSL_connect(): " + std::to_string(ssl_error));
    }

    logf_info("Handshake complete!");

    if (SSL_get_verify_mode(ssl_ptr) != SSL_VERIFY_NONE) {
        const auto verify_result = SSL_get_verify_result(ssl_ptr);
        if (verify_result == X509_V_OK) {
            logf_info("Verify certificate result is okay");
        } else {
            logf_error("Verify certificate result is not okay");
        }
    }

    handshake_complete = true;
    if (ssl->enable_key_logging) {
        ssl->key_server.reset();
    }

    call_if_available(event_callback, ConnectionEvent::ACCEPTED);
    call_if_available(event_callback, ConnectionEvent::OPEN);

    return true;
}

void ConnectionClientSSL::handle_data() {
    if (not handshake_complete) {
        if (not drive_handshake()) {
            return;
        }
        return;
    }

    call_if_available(event_callback, ConnectionEvent::NEW_DATA);
}

void ConnectionClientSSL::close() {
    /* tear down TLS connection gracefully */
    logf_info("Closing TLS connection");

    const auto ssl_ptr = ssl->ssl.get();

    const auto ssl_close_result = SSL_shutdown(ssl_ptr);

    if (ssl_close_result < 0) {
        const auto ssl_error = SSL_get_error(ssl_ptr, ssl_close_result);
        if ((ssl_error != SSL_ERROR_WANT_READ) and (ssl_error != SSL_ERROR_WANT_WRITE)) {
            logf_error("%s", log_openssl_error("Failed to SSL_shutdown(): " + std::to_string(ssl_error)).c_str());
        }
    }

    poll_manager.unregister_fd(ssl->fd);
    ::close(ssl->fd);

    logf_info("TLS connection closed gracefully");

    call_if_available(event_callback, ConnectionEvent::CLOSED);
}

} // namespace iso15118::io
