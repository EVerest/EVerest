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
#include <openssl/ocsp.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>

#include <extensions/trusted_ca_keys.hpp>

#include <iso15118/detail/helper.hpp>
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
    // [V2G2-651]: the trusted-root hint advertised in the trusted_ca_keys ClientHello extension. Referenced
    // by pointer from the SSL_CTX custom-ext callback for the lifetime of this connection, so it must
    // outlive ssl_ctx (declared here, not on the stack of init_ssl).
    tls::trusted_ca_keys::trusted_ca_keys_t trusted_ca_keys{};
};

namespace {

constexpr auto CONNECT_TIMEOUT_MS = 5000;

// openssl error-queue helpers, formerly in detail/io/helper_ssl.hpp (deleted when the SECC side
// moved onto tls::Server); this hand-rolled client is their last user.
int add_error_str(const char* str, std::size_t len, void* u) {
    assert(u);
    auto& text = *static_cast<std::string*>(u);
    text += ": " + std::string(str, len);
    return 0;
}

std::string log_openssl_error(const std::string& error_msg) {
    std::string error_message{error_msg};
    ERR_print_errors_cb(&add_error_str, &error_message);
    return error_message;
}

void log_and_raise_openssl_error(const std::string& error_msg) {
    throw std::runtime_error(log_openssl_error(error_msg));
}

int ssl_keylog_server_index{-1};
int ssl_keylog_file_index{-1};

void keylog_callback(const SSL* ssl, const char* line) {
    const auto key_logging_server =
        static_cast<io::TlsKeyLoggingServer*>(SSL_get_ex_data(ssl, ssl_keylog_server_index));

    // The key-logging server is destroyed when the handshake completes, but this callback stays
    // registered on the SSL_CTX for the connection's lifetime and can re-fire afterwards (e.g. a
    // TLS 1.3 KeyUpdate). The ex_data is cleared when the server is destroyed, so a nullptr here
    // is expected -- skip the server path instead of dereferencing the freed server.
    if (key_logging_server != nullptr) {
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
    }

    const auto keylog_file_path =
        static_cast<std::filesystem::path*>(SSL_CTX_get_ex_data(SSL_get_SSL_CTX(ssl), ssl_keylog_file_index));

    if (keylog_file_path != nullptr and not keylog_file_path->empty()) {
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

// [V2G2-070]: OCSP stapling. The SECC is asked to staple an OCSP response (SSL_CTX_set_tlsext_status_type
// below). Here we only decode-check the staple for well-formedness and fail the handshake on a malformed
// response. TODO: full revocation verification (OCSP_basic_verify + OCSP_check_validity + single-cert
// status) is deferred — the EV module currently has no evse_security/issuer material to verify against.
int ocsp_status_callback(SSL* ssl, void* /*arg*/) {
    const unsigned char* resp_der = nullptr;
    const long resp_len = SSL_get_tlsext_status_ocsp_resp(ssl, &resp_der);
    if (resp_der == nullptr or resp_len <= 0) {
        // The SECC provided no staple. We requested but do not (yet) mandate one.
        logf_info("No OCSP staple provided by the SECC");
        return 1; // SSL_TLSEXT_ERR_OK
    }
    OCSP_RESPONSE* resp = d2i_OCSP_RESPONSE(nullptr, &resp_der, resp_len);
    if (resp == nullptr) {
        logf_error("Failed to decode the stapled OCSP response");
        return 0; // SSL_TLSEXT_ERR_ALERT_FATAL - abort the handshake on a malformed staple
    }
    OCSP_RESPONSE_free(resp);
    return 1; // SSL_TLSEXT_ERR_OK
}

// [V2G2-875]: the SECC (V2G leaf) certificate identifies a charge point operator via a
// DomainComponent=="CPO" RDN in its subject. Returns true if such an RDN is present.
bool peer_leaf_has_cpo_domain_component(SSL* ssl) {
    std::unique_ptr<X509, decltype(&X509_free)> cert(SSL_get1_peer_certificate(ssl), &X509_free);
    if (not cert) {
        return false;
    }
    X509_NAME* subject = X509_get_subject_name(cert.get());
    if (subject == nullptr) {
        return false;
    }
    int idx = -1;
    while ((idx = X509_NAME_get_index_by_NID(subject, NID_domainComponent, idx)) >= 0) {
        const X509_NAME_ENTRY* entry = X509_NAME_get_entry(subject, idx);
        const ASN1_STRING* data = X509_NAME_ENTRY_get_data(entry);
        if (data == nullptr) {
            continue;
        }
        const std::string dc(reinterpret_cast<const char*>(ASN1_STRING_get0_data(data)),
                             static_cast<size_t>(ASN1_STRING_length(data)));
        if (dc == "CPO") {
            return true;
        }
    }
    return false;
}

// [V2G2-651]: build the trusted_ca_keys hint from the configured V2G root file. Each root certificate is
// advertised by its SHA-1 hash (cert_sha1_hash identifier, matching captured PEV ClientHello traces) so the
// SECC can select a chain the EV trusts. Returns an empty structure (no extension emitted) when the file is
// absent or holds no certificate.
tls::trusted_ca_keys::trusted_ca_keys_t build_trusted_ca_keys(const std::string& v2g_root_path) {
    tls::trusted_ca_keys::trusted_ca_keys_t tck{};
    if (v2g_root_path.empty()) {
        return tck;
    }
    std::unique_ptr<BIO, decltype(&BIO_free)> bio(BIO_new_file(v2g_root_path.c_str(), "r"), &BIO_free);
    if (not bio) {
        logf_warning("trusted_ca_keys: could not open the V2G root file %s", v2g_root_path.c_str());
        return tck;
    }
    while (true) {
        std::unique_ptr<X509, decltype(&X509_free)> cert(PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr),
                                                         &X509_free);
        if (not cert) {
            break;
        }
        tls::trusted_ca_keys::digest_t digest{};
        if (tls::trusted_ca_keys::certificate_digest(digest, cert.get())) {
            tck.cert_sha1_hash.push_back(digest);
        }
    }
    // PEM_read_bio_X509 leaves a benign "no start line" error on the queue at EOF; clear it so it does not
    // surface in later handshake diagnostics.
    ERR_clear_error();
    return tck;
}

SSL_CTX* init_ssl(const config::SSLConfig& ssl_config, bool verify_server_certificate,
                  tls::trusted_ca_keys::trusted_ca_keys_t& trusted_ca_keys) {

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

    // The EV presents its OEM/vehicle leaf certificate to the SECC (a single chain; the multi-chain
    // shape of SSLConfig exists for the SECC's TLS 1.3 chain selection). mTLS is mandatory for
    // ISO 15118-20 (enforce_tls_1_3), so the client chain is required there; for TLS 1.2
    // (ISO 15118-2) the SECC does not request a client certificate, so no chain is tolerated.
    if (ssl_config.chains.size() > 1) {
        log_and_throw("The EV TLS client supports exactly one client certificate chain");
    }

    const config::ChainConfig* chain = ssl_config.chains.empty() ? nullptr : &ssl_config.chains.front();
    const bool have_client_cert = chain != nullptr and not chain->path_certificate_chain.empty();
    const bool have_client_key = chain != nullptr and not chain->path_certificate_key.empty();

    if (have_client_cert != have_client_key) {
        log_and_throw("Exactly one of the client certificate chain and key is configured; both (mTLS) or "
                      "neither are required");
    }

    if (ssl_config.enforce_tls_1_3 and (not have_client_cert or not have_client_key)) {
        log_and_throw("A client certificate chain and key are required for TLS 1.3 (mTLS) but not configured");
    }

    if (have_client_cert and SSL_CTX_use_certificate_chain_file(ctx, chain->path_certificate_chain.c_str()) != 1) {
        log_and_raise_openssl_error("Failed in SSL_CTX_use_certificate_chain_file()");
    }

    // INFO: the password callback uses a non-const argument
    if (chain != nullptr and chain->private_key_password.has_value()) {
        // Lifetime of the password is important because using a callback we'll require a valid ref
        SSL_CTX_set_default_passwd_cb_userdata(ctx,
                                               &const_cast<config::ChainConfig&>(*chain).private_key_password.value());
        SSL_CTX_set_default_passwd_cb(ctx, private_key_callback);
    }

    if (have_client_key and
        SSL_CTX_use_PrivateKey_file(ctx, chain->path_certificate_key.c_str(), SSL_FILETYPE_PEM) != 1) {
        log_and_raise_openssl_error("Failed in SSL_CTX_use_PrivateKey_file()");
    }

    if (verify_server_certificate) {
        // [V2G2-875]: verify the SECC (V2G leaf) certificate chain against the V2G root and abort on
        // failure. A missing root is fatal (fail closed) rather than a silent log — without it no chain
        // can be verified.
        if (SSL_CTX_load_verify_file(ctx, ssl_config.path_certificate_v2g_root.c_str()) == 0) {
            log_and_raise_openssl_error("Failed to load the V2G root certificate for SECC verification");
        }
        // No hostname check (the SECC certificate carries none); the CPO DomainComponent is checked after
        // the handshake instead (peer_leaf_has_cpo_domain_component). Chain verification is mandatory.
        SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, nullptr);

        // [V2G2-070]: request an OCSP staple from the SECC (decode-checked in ocsp_status_callback).
        if (SSL_CTX_set_tlsext_status_type(ctx, TLSEXT_STATUSTYPE_ocsp) != 1) {
            logf_warning("Failed to request OCSP stapling (status_request extension)");
        } else {
            SSL_CTX_set_tlsext_status_cb(ctx, ocsp_status_callback);
        }
    } else {
        // Verification disabled by configuration: a spoofed SECC would be accepted. This is NOT ISO 15118
        // conformant and must only be used for testing.
        logf_warning("verify_server_certificate is disabled: the SECC certificate will NOT be verified "
                     "(insecure, non-conformant - test use only)");
        SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, nullptr);
    }

    // [V2G2-651]: advertise the trusted V2G root(s) via the trusted_ca_keys ClientHello extension so the
    // SECC can pick a matching chain. This is a TLS 1.2 (ISO 15118-2) extension only; ISO 15118-20 pins
    // TLS 1.3, which uses the certificate_authorities extension instead. Best-effort: on any failure the
    // handshake still proceeds, just without the hint.
    if (not ssl_config.enforce_tls_1_3) {
        trusted_ca_keys = build_trusted_ca_keys(ssl_config.path_certificate_v2g_root);
        if (not trusted_ca_keys.cert_sha1_hash.empty()) {
            constexpr int context =
                SSL_EXT_TLS_ONLY | SSL_EXT_TLS1_2_AND_BELOW_ONLY | SSL_EXT_IGNORE_ON_RESUMPTION | SSL_EXT_CLIENT_HELLO;
            if (SSL_CTX_add_custom_ext(ctx, TLSEXT_TYPE_trusted_ca_keys, context,
                                       &tls::trusted_ca_keys::ClientTrustedCaKeys::trusted_ca_keys_add,
                                       &tls::trusted_ca_keys::ClientTrustedCaKeys::trusted_ca_keys_free,
                                       &trusted_ca_keys, nullptr, nullptr) != 1) {
                logf_warning("Failed to add the trusted_ca_keys ClientHello extension");
            }
        }
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

    const auto ssl_ctx = init_ssl(ssl_config, verify_server_certificate, ssl->trusted_ca_keys);
    ssl->ssl_ctx = std::unique_ptr<SSL_CTX>(ssl_ctx);

    if (ssl_keylog_file_index != -1) {
        ssl->tls_key_log_file_path = ssl_config.tls_key_logging_path / "tls_session_keys.log";
        SSL_CTX_set_ex_data(ssl->ssl_ctx.get(), ssl_keylog_file_index, &ssl->tls_key_log_file_path);
    }

    ssl->fd = socket(AF_INET6, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (ssl->fd == -1) {
        log_and_throw("Failed to create an ipv6 socket");
    }

    restrict_source_port_range(ssl->fd);

    sockaddr_in6 address{};
    address.sin6_family = AF_INET6;
    address.sin6_port = htons(secc_endpoint.port);
    memcpy(&address.sin6_addr, secc_endpoint.address, sizeof(secc_endpoint.address));
    address.sin6_scope_id = if_nametoindex(interface_name_.c_str());

    const auto connect_result = ::connect(ssl->fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address));
    try {
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
    } catch (...) {
        // The destructor does not run for a throwing constructor -- close the socket here so a
        // failed connection attempt does not leak the fd.
        ::close(ssl->fd);
        throw;
    }

    ssl->ssl = std::unique_ptr<SSL>(SSL_new(ssl->ssl_ctx.get()));
    // BIO_CLOSE: the BIO (freed by SSL_free when this object is destroyed) owns and closes the fd. close()
    // therefore must NOT ::close(fd) as well, or the descriptor would be closed twice (fd-reuse race).
    const auto socket_bio = BIO_new_socket(ssl->fd, BIO_CLOSE);
    if (not ssl->ssl or socket_bio == nullptr) {
        // The fd is not yet owned by an SSL object on these paths; a created socket BIO owns it
        // (BIO_CLOSE), so free the BIO and only close the fd ourselves when no BIO exists.
        if (socket_bio != nullptr) {
            BIO_free(socket_bio);
        } else {
            ::close(ssl->fd);
        }
        log_and_raise_openssl_error("Failed to set up the SSL object");
    }

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

ConnectionClientSSL::~ConnectionClientSSL() {
    // Silence the event callback (it targets the dying session) and make sure the fd is
    // unregistered from the PollManager even when close() was never called -- otherwise the poll
    // manager keeps a [this]-capturing callback keyed by a closed (and possibly reused) fd.
    // The fd itself is owned by the BIO_CLOSE socket BIO and is closed when the SSL object is
    // freed during member destruction. No CLOSED event is fired during destruction.
    event_callback = nullptr;
    if (not closed) {
        closed = true;
        poll_manager.unregister_fd(ssl->fd);
    }
}

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

    // SSL_ERROR_ZERO_RETURN is the peer's orderly close_notify; SSL_ERROR_SYSCALL without OpenSSL
    // error detail is an EOF without one. Both are regular ways the SECC ends the connection
    // (ISO 15118-2 [V2G2-025], DIN [V2G-DC-937]) -- report them as a closed connection (mirrors
    // the server-side ConnectionSSL) instead of throwing on a normal termination.
    if ((ssl_error == SSL_ERROR_ZERO_RETURN) or (ssl_error == SSL_ERROR_SYSCALL)) {
        logf_info("TLS connection closed by the peer");
        return {false, 0, true};
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
        // [V2G2-875]: abort on any chain-verification failure (with SSL_VERIFY_PEER the handshake would
        // already have failed; this is the fail-closed backstop).
        const auto verify_result = SSL_get_verify_result(ssl_ptr);
        if (verify_result != X509_V_OK) {
            log_and_throw(
                (std::string("SECC certificate verification failed: ") + X509_verify_cert_error_string(verify_result))
                    .c_str());
        }
        // [V2G2-875]: the SECC leaf must identify a CPO (DomainComponent=="CPO"). Logged as a warning
        // rather than fatal to remain compatible with test PKIs that omit the RDN; elevate to fatal once
        // the deployed cert profile guarantees it.
        if (not peer_leaf_has_cpo_domain_component(ssl_ptr)) {
            logf_warning("SECC certificate has no DomainComponent=CPO RDN (ISO 15118-2 Annex F)");
        }
        logf_info("SECC certificate verified");
    }

    handshake_complete = true;
    if (ssl->enable_key_logging) {
        // Disarm the keylog callback before destroying the server: it stays registered on the
        // SSL_CTX for the connection's lifetime and could otherwise re-fire on a freed pointer
        // (e.g. a post-handshake TLS 1.3 KeyUpdate).
        SSL_set_ex_data(ssl_ptr, ssl_keylog_server_index, nullptr);
        ssl->key_server.reset();
    }

    call_if_available(event_callback, ConnectionEvent::ACCEPTED);
    call_if_available(event_callback, ConnectionEvent::OPEN);

    return true;
}

void ConnectionClientSSL::handle_data() {
    if (not handshake_complete) {
        try {
            if (not drive_handshake()) {
                return;
            }
        } catch (const std::runtime_error& e) {
            // A failed handshake (certificate verification, fatal alert, malformed flight) must NOT
            // escape this poll callback: PollManager::poll() would rethrow it into the controller
            // loop, whose catch exits the loop permanently and bricks all future charging. Fail
            // just this connection; the session treats CLOSED before session end as terminal and
            // the controller reaps it like any other failed attempt.
            logf_error("TLS handshake with the SECC failed: %s", e.what());
            close();
            return;
        }
        return;
    }

    call_if_available(event_callback, ConnectionEvent::NEW_DATA);
}

void ConnectionClientSSL::close() {
    if (closed) {
        // keep close() idempotent: the session driver may close on peer-EOF and again at teardown
        return;
    }
    closed = true;

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
    // Do NOT ::close(ssl->fd) here: the socket BIO was created with BIO_CLOSE and closes the fd when the
    // SSL object is freed on destruction. Closing it here as well caused a double close of the descriptor.

    logf_info("TLS connection closed gracefully");

    call_if_available(event_callback, ConnectionEvent::CLOSED);
}

} // namespace iso15118::io
