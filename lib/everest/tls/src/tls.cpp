// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Pionix GmbH and Contributors to EVerest

#include "extensions/status_request.hpp"
#include "extensions/trusted_ca_keys.hpp"
#include <everest/tls/openssl_util.hpp>
#include <everest/tls/tls.hpp>

#include <evse_security/crypto/openssl/openssl_provider.hpp>

#include <arpa/inet.h>
#include <array>
#include <cassert>
#include <charconv>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <string>
#include <sys/socket.h>

#include <openssl/asn1.h>
#include <openssl/bio.h>
#include <openssl/crypto.h>
#include <openssl/err.h>
#include <openssl/ocsp.h>
#include <openssl/ssl.h>
#include <openssl/tls1.h>
#include <openssl/types.h>
#include <openssl/x509_vfy.h>
#include <utility>

#ifdef UNIT_TEST
#include "extensions/helpers.hpp"
#endif

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
template <> class default_delete<BIO_ADDR> {
public:
    void operator()(BIO_ADDR* ptr) const {
        ::BIO_ADDR_free(ptr);
    }
};
template <> class default_delete<BIO_ADDRINFO> {
public:
    void operator()(BIO_ADDRINFO* ptr) const {
        ::BIO_ADDRINFO_free(ptr);
    }
};
} // namespace std

using ::openssl::log_error;
using ::openssl::log_info;
using ::openssl::log_warning;

namespace {
using evse_security::OpenSSLProvider;

/**
 * \brief signal handler that does nothing
 * \param[in] sig is the signal that was received (not used)
 * \note exists so that poll() can be interrupted
 */
void sig_int_handler(int sig) {
}

/// OpenSSL results mapped to an enum
enum class ssl_error_t : std::uint8_t {
    error,
    error_ssl,
    error_syscall,
    none,
    want_accept,
    want_async,
    want_async_job,
    want_connect,
    want_hello_cb,
    want_read,
    want_write,
    want_x509_lookup,
    zero_return,
    timeout, // not an OpenSSL result
};

/**
 * \brief convert OpenSSL result into an enum
 * \param[in] err is the OpenSSL result
 * \return enum equivalent of err
 */
constexpr ssl_error_t convert(const int err) {
    ssl_error_t res{ssl_error_t::error};
    switch (err) {
    case SSL_ERROR_NONE:
        res = ssl_error_t::none;
        break;
    case SSL_ERROR_ZERO_RETURN:
        res = ssl_error_t::zero_return;
        break;
    case SSL_ERROR_WANT_READ:
        res = ssl_error_t::want_read;
        break;
    case SSL_ERROR_WANT_WRITE:
        res = ssl_error_t::want_write;
        break;
    case SSL_ERROR_WANT_CONNECT:
        res = ssl_error_t::want_connect;
        break;
    case SSL_ERROR_WANT_ACCEPT:
        res = ssl_error_t::want_accept;
        break;
    case SSL_ERROR_WANT_X509_LOOKUP:
        res = ssl_error_t::want_x509_lookup;
        break;
    case SSL_ERROR_WANT_ASYNC:
        res = ssl_error_t::want_async;
        break;
    case SSL_ERROR_WANT_ASYNC_JOB:
        res = ssl_error_t::want_async_job;
        break;
    case SSL_ERROR_WANT_CLIENT_HELLO_CB:
        res = ssl_error_t::want_hello_cb;
        break;
    case SSL_ERROR_SYSCALL:
        res = ssl_error_t::error_syscall;
        break;
    case SSL_ERROR_SSL:
        res = ssl_error_t::error_ssl;
        break;
    default:
        log_error(std::string("Unexpected SSL_get_error: ") + std::to_string(static_cast<int>(res)));
        break;
    };
    return res;
}

/// subset of OpenSSL results to simplify error recovery
enum class ssl_result_t : std::uint8_t {
    error,         //!< error - connection still active
    error_syscall, //!< error - connection closed, socket no longer valid
    success,
    closed,
    timeout,
    want_read,  //!< non-blocking - operation waiting for read available on socket
    want_write, //!< non-blocking - operation waiting for write available on socket
};

/**
 * \brief map OpenSSL result enum to a simplified subset
 * \param[in] err is the OpenSSL result enum value
 * \return the simplified version of err
 */
constexpr ssl_result_t convert(ssl_error_t err) {
    switch (err) {
    case ssl_error_t::none:
        return ssl_result_t::success;
    case ssl_error_t::timeout:
        return ssl_result_t::timeout;
    case ssl_error_t::error_syscall:
    case ssl_error_t::error_ssl:
        return ssl_result_t::error_syscall;
    case ssl_error_t::zero_return:
        return ssl_result_t::closed;
    case ssl_error_t::want_read:
        return ssl_result_t::want_read;
    case ssl_error_t::want_write:
        return ssl_result_t::want_write;
    case ssl_error_t::error:
    case ssl_error_t::want_accept:  // only from BIO_s_accept()
    case ssl_error_t::want_connect: // only from BIO_s_connect()
    case ssl_error_t::want_async:
    case ssl_error_t::want_async_job:
    case ssl_error_t::want_hello_cb:
    case ssl_error_t::want_x509_lookup:
    default:
        return ssl_result_t::error;
    }
}

/**
 * \brief convert the simplified result into the result used in the API
 * \param[in] err is the simplified result
 * \return the mapped API result value
 */
constexpr tls::Connection::result_t convert(ssl_result_t err) {
    switch (err) {
    case ssl_result_t::success:
        return tls::Connection::result_t::success;
    case ssl_result_t::timeout:
        return tls::Connection::result_t::timeout;
    case ssl_result_t::want_read:
        return tls::Connection::result_t::want_read;
    case ssl_result_t::want_write:
        return tls::Connection::result_t::want_write;
    case ssl_result_t::closed:
    case ssl_result_t::error:
    case ssl_result_t::error_syscall:
    default:
        return tls::Connection::result_t::closed;
    }
}

/// error holds the drained OpenSSL error text; empty when there was no error
struct ssl_op_result {
    ssl_result_t rc;
    std::string error;
};

/**
 * \brief wait for an event on a socket
 * \param[in] soc is the file descriptor/socket
 * \param[in] forWrite wait for a write event when true, read event otherwise
 * \param[in] timeout_ms -1 is wait forever, 0 checks for events and returns
 *            immediately, >0 maximum time to wait in milliseconds
 * \return >0 when there are events on the socket
 *         0 for timeout
 *         -1 for error (check errno)
 *         -2 for error where errno is EINTR (interrupted call)
 */
int wait_for(int soc, bool forWrite, std::int32_t timeout_ms) {
    const std::int16_t event = (forWrite) ? POLLOUT : POLLIN;
    std::array<pollfd, 1> fds = {{{soc, event, 0}}};
    auto poll_res = poll(fds.data(), fds.size(), timeout_ms);
    if (poll_res == -1) {
        if (errno != EINTR) {
            log_error(std::string("wait_for poll: ") + std::to_string(errno));
        } else {
            poll_res = -2;
        }
    }
    return poll_res;
}

/**
 * \brief wait for an event on a socket retrying when interrupted
 * \param[in] soc is the file descriptor/socket
 * \param[in] forWrite wait for a write event when true, read event otherwise
 * \param[in] timeout_ms -1 is wait forever, 0 checks for events and returns
 *            immediately, >0 maximum time to wait in milliseconds
 * \return >0 when there are events on the socket
 *         0 for timeout
 *         -1 for error (check errno)
 */
int wait_for_loop(int soc, bool forWrite, std::int32_t timeout_ms) {
    int res{-2};
    while (res == -2) {
        res = wait_for(soc, forWrite, timeout_ms);
    }
    return res;
}

/**
 * \brief read user data from a SSL connection
 * \param[in] ctx is SSL connection data
 * \param[in] buf is where to place received data
 * \param[in] num is the size of buff (the maximum number of bytes to receive)
 * \param[out] readbytes number of bytes received
 * \returns the result of the operation
 */
[[nodiscard]] ssl_op_result ssl_read(SSL* ctx, std::byte* buf, std::size_t num, std::size_t& readbytes);
/**
 * \brief write user data to a SSL connection
 * \param[in] ctx is SSL connection data
 * \param[in] buf is the data to send
 * \param[in] num is the size of buff
 * \param[out] writebytes number of bytes sent
 * \returns the result of the operation
 */
[[nodiscard]] ssl_op_result ssl_write(SSL* ctx, const std::byte* buf, std::size_t num, std::size_t& writebytes);
/**
 * \brief accept an incoming SSL connection, runs the TLS handshake (sever)
 * \param[in] ctx is SSL connection data
 * \returns the result of the operation
 */
[[nodiscard]] ssl_op_result ssl_accept(SSL* ctx);
/**
 * \brief start a SSL connection, runs the TLS handshake (client)
 * \param[in] ctx is SSL connection data
 * \returns the result of the operation
 */
[[nodiscard]] ssl_op_result ssl_connect(SSL* ctx);
/**
 * \brief close a SSL connection
 * \param[in] ctx is SSL connection data
 * \returns the result of the operation
 */
ssl_op_result ssl_shutdown(SSL* ctx);

/// operation being performed
enum class operation_t : std::uint8_t {
    ssl_read,
    ssl_write,
    ssl_accept,
    ssl_connect,
    ssl_shutdown,
};

/**
 * \brief text representation of the operation being performed
 * \param[in] the operation
 * \return a string representing the operation
 */
const char* operation_str(operation_t operation) {
    switch (operation) {
    case operation_t::ssl_read:
        return "SSL_read: ";
    case operation_t::ssl_write:
        return "SSL_write: ";
    case operation_t::ssl_accept:
        return "SSL_accept: ";
    case operation_t::ssl_connect:
        return "SSL_connect: ";
    case operation_t::ssl_shutdown:
        return "SSL_shutdown: ";
    default:
        return "<SSL unknown>: ";
    }
}

/**
 * \brief drain the per-thread OpenSSL error queue into a string
 * \return "; "-joined error strings; empty when the queue was empty
 * \note consumes the queue (ERR_get_error). Callers that also want the errors
 *       logged should log from the returned string, since the queue is emptied.
 */
std::string drain_openssl_error_queue() {
    std::string out;
    unsigned long err{0};
    char buf[256];
    while ((err = ERR_get_error()) != 0) {
        ERR_error_string_n(err, buf, sizeof(buf));
        if (not out.empty()) {
            out += "; ";
        }
        out += buf;
    }
    return out;
}

/**
 * \brief manage the result from a SSL operation
 * \param[in] ctx is SSL connection data
 * \param[in] res is the result of the SSL operation
 * \return the simplified result mapped from res plus the drained OpenSSL
 *         error text (empty when there was no error)
 */
ssl_op_result process_result(SSL* ctx, operation_t operation, const int res) {
    ssl_error_t result{ssl_error_t::error};
    std::string error;

    if (ctx != nullptr) {
        result = ssl_error_t::none; // success
        if (res <= 0) {
            const auto sslerr_raw = SSL_get_error(ctx, res);
            result = convert(sslerr_raw);
            switch (result) {
            case ssl_error_t::none:
            case ssl_error_t::zero_return:
            case ssl_error_t::want_read:
            case ssl_error_t::want_write:
                break;
            case ssl_error_t::error_syscall:
                // no further operation permitted on the connection
                // Capture the queue before it is consumed; on a pure syscall
                // failure the queue may be empty, so fall back to errno text.
                error = drain_openssl_error_queue();
                if (errno != 0) {
                    if (error.empty()) {
                        error = "SSL_ERROR_SYSCALL " + std::to_string(errno);
                    }
                    log_error(operation_str(operation) + std::string("SSL_ERROR_SYSCALL ") + std::to_string(errno));
                }
                break;
            case ssl_error_t::error_ssl:
                error = drain_openssl_error_queue();
                if (operation != operation_t::ssl_shutdown) {
                    log_error(operation_str(operation) + std::to_string(res) + " " + std::to_string(sslerr_raw) + " " +
                              error);
                }
                break;
            case ssl_error_t::error:
            case ssl_error_t::want_accept:
            case ssl_error_t::want_async:
            case ssl_error_t::want_async_job:
            case ssl_error_t::want_connect:
            case ssl_error_t::want_hello_cb:
            case ssl_error_t::want_x509_lookup:
            default:
                error = drain_openssl_error_queue();
                log_error(operation_str(operation) + std::to_string(res) + " " + std::to_string(sslerr_raw) + " " +
                          error);
                break;
            }
        }
    }

    return {convert(result), std::move(error)};
}

ssl_op_result ssl_read(SSL* ctx, std::byte* buf, const std::size_t num, std::size_t& readbytes) {
    const auto res = SSL_read_ex(ctx, buf, num, &readbytes);
    return process_result(ctx, operation_t::ssl_read, res);
}

ssl_op_result ssl_write(SSL* ctx, const std::byte* buf, const std::size_t num, std::size_t& writebytes) {
    const auto res = SSL_write_ex(ctx, buf, num, &writebytes);
    return process_result(ctx, operation_t::ssl_write, res);
}

ssl_op_result ssl_accept(SSL* ctx) {
    const auto res = SSL_accept(ctx);
    // 0 is handshake not successful (ssl_error_t::zero_return -> ssl_result_t::closed)
    // < 0 is other error
    return process_result(ctx, operation_t::ssl_accept, res);
}

ssl_op_result ssl_connect(SSL* ctx) {
    const auto res = SSL_connect(ctx);
    // 0 is handshake not successful (ssl_error_t::zero_return -> ssl_result_t::closed)
    // < 0 is other error
    return process_result(ctx, operation_t::ssl_connect, res);
}

ssl_op_result ssl_shutdown(SSL* ctx) {
    const auto res = SSL_shutdown(ctx);
    return process_result(ctx, operation_t::ssl_shutdown, res);
}

/**
 * \brief run the SSL shutdown loop on a connection
 * \param[in] ctx is SSL connection data
 * \param[in] timeout_ms time to wait in milliseconds, -1 is wait forever, 0 is don't wait
 * \param[inout] state connection state, updated as the shutdown progresses
 * \return the result of the operation plus any drained OpenSSL error text
 */
ssl_op_result drive_ssl_shutdown(SSL* ctx, int timeout_ms, tls::Connection::state_t& state) {
    using state_t = tls::Connection::state_t;
    ssl_op_result result{ssl_result_t::error, {}};

    if (state == state_t::connected) {
        bool loop{true};
        while (loop) {
            loop = false;
            result = ssl_shutdown(ctx);
            switch (result.rc) {
            case ssl_result_t::closed:
            case ssl_result_t::success:
            case ssl_result_t::timeout:
                state = state_t::closed;
                break;
            case ssl_result_t::want_read:
            case ssl_result_t::want_write:
                if (timeout_ms != 0) {
                    const auto res = wait_for_loop(SSL_get_fd(ctx), result.rc == ssl_result_t::want_write, timeout_ms);
                    loop = res > 0; // event received
                    result.rc = ssl_result_t::timeout;
                }
                break;
            case ssl_result_t::error:
            case ssl_result_t::error_syscall:
            default:
                state = state_t::fault;
                break;
            }
        }
    }
    return result;
}

/// guard and success-state semantics of an SSL operation driven by drive_ssl_op
enum class ssl_op_kind : std::uint8_t {
    transfer,  //!< read/write: runs on a connected connection
    handshake, //!< accept/connect: runs on an idle connection, connected on success
};

/**
 * \brief drive an SSL operation, waiting and retrying on want_read/want_write
 * \param[in] ctx is SSL connection data
 * \param[in] op performs the SSL operation
 * \param[in] timeout_ms time to wait in milliseconds, -1 is wait forever, 0 is don't wait
 * \param[in] shutdown_timeout_ms timeout for the best-effort shutdown on closed/error outcomes
 * \param[in] kind see ssl_op_kind
 * \param[inout] state connection state, updated as the operation progresses
 * \param[out] last_error_out drained OpenSSL error text from the operation, empty when there was no error
 * \return the API result of the operation
 * \note on closed/error outcomes a best-effort shutdown runs; its error is
 *       discarded so the operation's own error is preserved in last_error_out
 */
tls::Connection::result_t drive_ssl_op(SSL* ctx, const std::function<ssl_op_result()>& op, int timeout_ms,
                                       int shutdown_timeout_ms, ssl_op_kind kind, tls::Connection::state_t& state,
                                       std::string& last_error_out) {
    using state_t = tls::Connection::state_t;
    ssl_op_result result{ssl_result_t::error, {}};

    const auto guard = (kind == ssl_op_kind::handshake) ? state_t::idle : state_t::connected;
    if (state == guard) {
        bool loop{true};
        while (loop) {
            loop = false;
            result = op();
            switch (result.rc) {
            case ssl_result_t::success:
                if (kind == ssl_op_kind::handshake) {
                    state = state_t::connected;
                }
                break;
            case ssl_result_t::timeout:
                break;
            case ssl_result_t::want_read:
            case ssl_result_t::want_write:
                if (timeout_ms != 0) {
                    const auto res = wait_for_loop(SSL_get_fd(ctx), result.rc == ssl_result_t::want_write, timeout_ms);
                    loop = res > 0; // event received
                    result.rc = ssl_result_t::timeout;
                }
                break;
            case ssl_result_t::error_syscall:
                state = state_t::fault;
                break;
            case ssl_result_t::closed:
                static_cast<void>(drive_ssl_shutdown(ctx, shutdown_timeout_ms, state));
                break;
            case ssl_result_t::error:
            default:
                static_cast<void>(drive_ssl_shutdown(ctx, shutdown_timeout_ms, state));
                state = state_t::fault;
                break;
            }
        }
    }
    last_error_out = std::move(result.error);
    return convert(result.rc);
}

/// options controlling how configure_ssl_ctx builds an SSL context
struct ssl_ctx_params {
    bool is_server{false};             //!< a server context is needed
    const char* ciphersuites{nullptr}; //!< TLS 1.3 cipher suites, nullptr means use default, "" disables TLS 1.3
    const char* cipher_list{nullptr};  //!< TLS 1.2 ciphers, nullptr means use default
    bool required{false};              //!< when true, fail when cert_config is missing (true for a TLS server)
    bool enforce_tls_1_3{false};       //!< when true the context requires TLS 1.3 minimum and skips the
                                       //!< TLS 1.2 cap; ignored for client contexts
};

/**
 * \brief configure SSL context with certificates and keys
 * \param[inout] ctx is SSL context data
 * \param[in] cert_config are one of more sets of key and certificates
 * \param[in] p options, see ssl_ctx_params
 * \return true when successful
 */
bool configure_ssl_ctx(SSL_CTX*& ctx, const tls::Server::certificate_config_t& cert_config, const ssl_ctx_params& p) {
    bool result{true};

    if (p.is_server) {
        OpenSSLProvider provider;

        const SSL_METHOD* method = TLS_server_method();
        ctx = SSL_CTX_new_ex(provider, provider.propquery_default(), method);
    } else {
        const SSL_METHOD* method = TLS_client_method();
        ctx = SSL_CTX_new(method);
    }

    if (ctx == nullptr) {
        log_error("server_init::SSL_CTX_new");
        result = false;
    } else {
        const auto min_version = (p.is_server && p.enforce_tls_1_3) ? TLS1_3_VERSION : TLS1_2_VERSION;
        if (SSL_CTX_set_min_proto_version(ctx, min_version) == 0) {
            log_error("SSL_CTX_set_min_proto_version");
            result = false;
        }
        if (p.is_server && p.enforce_tls_1_3) {
            // Server is configured for TLS 1.3 only. We have already pinned
            // SSL_CTX_set_min_proto_version to TLS1_3_VERSION above, so we do
            // not also need to cap the max version. We do, however, want to
            // disable the middlebox compatibility mode (RFC 8446 Appendix D.4),
            // which makes the server emit a dummy ChangeCipherSpec record so
            // the handshake looks like TLS 1.2 to legacy middleboxes — that
            // workaround is unnecessary in the SECC use-case and would only
            // confuse a strict 15118 EVCC.
            SSL_CTX_clear_options(ctx, SSL_OP_ENABLE_MIDDLEBOX_COMPAT);
        } else if ((p.ciphersuites != nullptr) && (p.ciphersuites[0] == '\0')) {
            // Caller explicitly cleared the TLS 1.3 ciphersuite list, signalling
            // "no TLS 1.3 ciphersuites configured". Cap the max protocol version
            // at TLS 1.2 so the handshake cannot fall through to TLS 1.3 with no
            // cipher available. (A nullptr ciphersuites string means "use the
            // OpenSSL defaults" and is left alone.)
            if (SSL_CTX_set_max_proto_version(ctx, TLS1_2_VERSION) == 0) {
                log_error("SSL_CTX_set_max_proto_version");
                result = false;
            }
        }
        if (p.cipher_list != nullptr) {
            if (SSL_CTX_set_cipher_list(ctx, p.cipher_list) == 0) {
                log_error("SSL_CTX_set_cipher_list");
                result = false;
            }
        }
        if (p.ciphersuites != nullptr) {
            if (SSL_CTX_set_ciphersuites(ctx, p.ciphersuites) == 0) {
                log_error("SSL_CTX_set_ciphersuites");
                result = false;
            }
        }

        if (cert_config.certificate_chain_file != nullptr) {
            if (SSL_CTX_use_certificate_chain_file(ctx, cert_config.certificate_chain_file) != 1) {
                log_error("SSL_CTX_use_certificate_chain_file");
                result = false;
            }
        } else {
            if (p.required) {
                result = false;
            }
        }

        if (cert_config.private_key_file != nullptr) {
            // the password callback uses a non-const argument
            void* pass_ptr{nullptr};
            std::string pass_str;
            if (cert_config.private_key_password != nullptr) {
                pass_str = cert_config.private_key_password;
                pass_ptr = pass_str.data();
            }
            SSL_CTX_set_default_passwd_cb_userdata(ctx, pass_ptr);

            if (SSL_CTX_use_PrivateKey_file(ctx, cert_config.private_key_file, SSL_FILETYPE_PEM) != 1) {
                log_error("SSL_CTX_use_PrivateKey_file");
                result = false;
            }
            if (SSL_CTX_check_private_key(ctx) != 1) {
                log_error("SSL_CTX_check_private_key");
                result = false;
            }
        } else {
            if (p.required) {
                result = false;
            }
        }
    }

    return result;
}

// Returns false only when the default-verify-paths fallback fails (init must abort);
// a failed explicit load_verify_locations is logged but non-fatal, matching prior behavior.
bool configure_verify_locations(SSL_CTX* ctx, const tls::Server::config_t& cfg) {
    const bool have_primary = static_cast<const char*>(cfg.verify_locations_file) != nullptr ||
                              static_cast<const char*>(cfg.verify_locations_path) != nullptr;
    const bool have_explicit = have_primary || not cfg.verify_locations_additional_files.empty();
    if (have_explicit) {
        // Loaded whenever configured, even with verify_client == false, so the anchors
        // are available for the TLS 1.3 verify-mode upgrade in handle_tls_1_3_verify_upgrade.
        if (have_primary) {
            if (SSL_CTX_load_verify_locations(ctx, cfg.verify_locations_file, cfg.verify_locations_path) != 1) {
                log_error("SSL_CTX_load_verify_locations");
            }
        }
        for (const auto& file : cfg.verify_locations_additional_files) {
            if (static_cast<const char*>(file) != nullptr) {
                if (SSL_CTX_load_verify_locations(ctx, file, nullptr) != 1) {
                    log_error("SSL_CTX_load_verify_locations additional file");
                }
            }
        }
        return true;
    }
    if (not cfg.verify_client) {
        if (SSL_CTX_set_default_verify_paths(ctx) != 1) {
            log_error("SSL_CTX_set_default_verify_paths");
            return false;
        }
    }
    return true;
}

} // namespace

namespace tls {

using SSL_ptr = std::unique_ptr<SSL>;
using SSL_CTX_ptr = std::unique_ptr<SSL_CTX>;
using OCSP_RESPONSE_ptr = std::shared_ptr<OCSP_RESPONSE>;

struct connection_ctx {
    SSL_ptr ctx;
    BIO* soc_bio{nullptr};
    int soc{0};
};

struct ocsp_cache_ctx {
    std::map<OcspCache::digest_t, OCSP_RESPONSE_ptr> cache;
};

struct server_ctx {
    SSL_CTX_ptr ctx;
};

struct client_ctx {
    SSL_CTX_ptr ctx;
};

// ----------------------------------------------------------------------------
// Connection represents a TLS connection (client and server)

Connection::Connection(SslContext* ctx, int soc, const char* ip_in, const char* service_in, std::int32_t timeout_ms) :
    m_context(std::make_unique<connection_ctx>()), m_ip(ip_in), m_service(service_in), m_timeout_ms(timeout_ms) {
    m_context->ctx = SSL_ptr(SSL_new(ctx));
    m_context->soc = soc;

    if (m_context->ctx == nullptr) {
        log_error("Connection::SSL_new");
    } else {
        // BIO_free is handled when SSL_free is done (SSL_ptr)
        m_context->soc_bio = BIO_new_socket(soc, BIO_CLOSE);
        SSL_set_bio(m_context->ctx.get(), m_context->soc_bio, m_context->soc_bio);
    }

    if (m_context->soc_bio == nullptr) {
        log_error("Connection::BIO_new_socket");
    }
}

Connection::~Connection() = default;

Connection::result_t Connection::read(std::byte* buf, std::size_t num, std::size_t& readbytes, int timeout_ms) {
    assert(m_context != nullptr);
    m_last_error.clear();
    auto* ctx = m_context->ctx.get();
    return drive_ssl_op(
        ctx, [ctx, buf, num, &readbytes] { return ssl_read(ctx, buf, num, readbytes); }, timeout_ms, m_timeout_ms,
        ssl_op_kind::transfer, m_state, m_last_error);
}

Connection::result_t Connection::write(const std::byte* buf, std::size_t num, std::size_t& writebytes, int timeout_ms) {
    assert(m_context != nullptr);
    m_last_error.clear();
    auto* ctx = m_context->ctx.get();
    return drive_ssl_op(
        ctx, [ctx, buf, num, &writebytes] { return ssl_write(ctx, buf, num, writebytes); }, timeout_ms, m_timeout_ms,
        ssl_op_kind::transfer, m_state, m_last_error);
}

Connection::result_t Connection::shutdown(int timeout_ms) {
    assert(m_context != nullptr);
    m_last_error.clear();
    auto result = drive_ssl_shutdown(m_context->ctx.get(), timeout_ms, m_state);
    m_last_error = std::move(result.error);
    return convert(result.rc);
}

Connection::result_t Connection::wait_for(result_t action, int timeout_ms) {
    result_t result{action};
    auto ctx = m_context->ctx.get();

    switch (action) {
    case result_t::want_read:
    case result_t::want_write: {
        const auto res = wait_for_loop(SSL_get_fd(ctx), action == result_t::want_write, timeout_ms);
        if (res == 0) {
            result = result_t::timeout;
        } else if (res == -1) {
            result = result_t::closed;
        } else {
            result = result_t::success;
        }
        break;
    }
    case result_t::success:
    case result_t::closed:
    case result_t::timeout:
    default:
        break;
    }

    return result;
}

int Connection::socket() const {
    return m_context->soc;
}

std::size_t Connection::pending() const {
    return (m_context && m_context->ctx) ? static_cast<std::size_t>(SSL_pending(m_context->ctx.get())) : 0;
}

const Certificate* Connection::peer_certificate() const {
    assert(m_context != nullptr);
    return SSL_get0_peer_certificate(m_context->ctx.get());
}

SSL* Connection::ssl_context() const {
    return m_context->ctx.get();
}

// ----------------------------------------------------------------------------
// ServerConnection represents a TLS server connection

std::uint32_t ServerConnection::m_count{0};
std::mutex ServerConnection::m_cv_mutex;
std::condition_variable ServerConnection::m_cv;

namespace {

int ssl_keylog_file_index{-1};
int ssl_keylog_server_index{-1};

void keylog_callback(const SSL* ssl, const char* line) {

    // The callback is registered CTX-wide, so it also fires for connections
    // that skipped the per-connection key-log server (e.g. when the service
    // string was not a valid port number) and carry no ex_data.
    auto keylog_server = static_cast<TlsKeyLoggingServer*>(SSL_get_ex_data(ssl, ssl_keylog_server_index));

    if (keylog_server != nullptr) {
        std::string key_log_msg = "TLS Handshake keys on port ";
        key_log_msg += std::to_string(keylog_server->get_port()) + ": ";
        key_log_msg += std::string(line);

        log_info(key_log_msg);

        if (keylog_server->get_fd() != -1) {
            const auto result = keylog_server->send(line);
            if (result not_eq strlen(line)) {
                log_error("key_logging_server send() failed!");
            }
        }
    }

    auto keylog_file_path =
        static_cast<std::filesystem::path*>(SSL_CTX_get_ex_data(SSL_get_SSL_CTX(ssl), ssl_keylog_file_index));

    if (not keylog_file_path->empty()) {
        std::ofstream ofs;
        ofs.open(keylog_file_path->string(), std::ofstream::out | std::ofstream::app);
        ofs << line << std::endl;
        ofs.close();
    }
}

} // namespace

ServerConnection::ServerConnection(SslContext* ctx, int soc, const char* ip_in, const char* service_in,
                                   std::int32_t timeout_ms, const ConfigItem& tls_key_interface) :
    Connection(ctx, soc, ip_in, service_in, timeout_ms), m_tck_data{m_trusted_ca_keys, m_flags} {
    {
        std::lock_guard lock(m_cv_mutex);
        m_count++;
    }
    if (m_context->soc_bio != nullptr) {
        SSL_set_accept_state(m_context->ctx.get());
        ServerStatusRequestV2::set_data(m_context->ctx.get(), &m_flags);
        ServerTrustedCaKeys::set_data(m_context->ctx.get(), &m_tck_data);

        if (tls_key_interface != nullptr) {
            // The service string comes from the accept path and may be empty or
            // non-numeric; parse it without throwing and only enable key logging
            // when it is a valid port number.
            std::uint16_t port{0};
            bool port_valid{false};
            if (service_in != nullptr) {
                const char* end = service_in + std::strlen(service_in);
                const auto [ptr, ec] = std::from_chars(service_in, end, port);
                port_valid = (ec == std::errc{}) && (ptr == end) && (end != service_in);
            }
            if (port_valid) {
                m_keylog_server = std::make_unique<TlsKeyLoggingServer>(std::string(tls_key_interface), port);
                SSL_set_ex_data(m_context->ctx.get(), ssl_keylog_server_index, m_keylog_server.get());
            } else {
                log_warning(
                    "ServerConnection: service string is not a port number; TLS key logging disabled for this "
                    "connection");
            }
        }
    }
}

ServerConnection::~ServerConnection() {
    {
        std::lock_guard lock(m_cv_mutex);
        m_count--;
    }
    m_cv.notify_all();
}

Connection::result_t ServerConnection::accept(int timeout_ms) {
    assert(m_context != nullptr);
    m_last_error.clear();
    auto* ctx = m_context->ctx.get();
    return drive_ssl_op(
        ctx, [ctx] { return ssl_accept(ctx); }, timeout_ms, m_timeout_ms, ssl_op_kind::handshake, m_state,
        m_last_error);
}

std::optional<std::array<std::uint8_t, 64>> ServerConnection::peer_certificate_sha512() const {
    assert(m_context != nullptr);
    SSL* ssl = m_context->ctx.get();
    X509* peer = SSL_get0_peer_certificate(ssl);
    if (peer == nullptr) {
        return std::nullopt;
    }
    std::array<std::uint8_t, 64> out{};
    unsigned int len{0};
    const auto rc = X509_digest(peer, EVP_sha512(), out.data(), &len);
    if (rc == 0 || len != out.size()) {
        return std::nullopt;
    }
    return out;
}

void ServerConnection::wait_all_closed() {
    std::unique_lock lock(m_cv_mutex);
    m_cv.wait(lock, [] { return m_count == 0; });
    lock.unlock();
}

// ----------------------------------------------------------------------------
// ClientConnection represents a TLS client connection

namespace {
// RFC 6066 §3 forbids literal IPv4/IPv6 addresses in the SNI extension, and
// SSL_set1_host() expects a DNS name (IP SANs are matched via the verify param).
// Detect a literal so SNI and DNS pinning apply only to real hostnames.
bool is_ip_literal(const std::string& host) {
    unsigned char buf[sizeof(struct in6_addr)];
    return ::inet_pton(AF_INET, host.c_str(), buf) == 1 || ::inet_pton(AF_INET6, host.c_str(), buf) == 1;
}
} // namespace

ClientConnection::ClientConnection(SslContext* ctx, int soc, const char* ip_in, const char* service_in,
                                   std::int32_t timeout_ms, bool verify_subject_name) :
    Connection(ctx, soc, ip_in, service_in, timeout_ms) {
    if (m_context->soc_bio == nullptr) {
        return;
    }
    SSL_set_connect_state(m_context->ctx.get());
    if (m_ip.empty()) {
        return;
    }

    auto* const ssl = m_context->ctx.get();
    const bool ip_literal = is_ip_literal(m_ip);

    // RFC 6066 §3: the SNI extension carries DNS hostnames only, never IP literals.
    if (!ip_literal) {
        SSL_set_tlsext_host_name(ssl, m_ip.c_str());
    }

    if (verify_subject_name) {
        // Pin certificate verification to the host: a DNS name via RFC-6125
        // hostname matching, an IP literal via IP-SAN matching. If the pin cannot
        // be installed, fault the connection so the handshake fails closed rather
        // than silently downgrading to chain-of-trust only.
        const int pinned = ip_literal ? X509_VERIFY_PARAM_set1_ip_asc(SSL_get0_param(ssl), m_ip.c_str())
                                      : SSL_set1_host(ssl, m_ip.c_str());
        if (pinned != 1) {
            log_error(ip_literal ? "X509_VERIFY_PARAM_set1_ip_asc" : "SSL_set1_host");
            m_state = state_t::fault;
            return;
        }
        // disallow partial wildcards (e.g. "f*.example.com") in the certificate
        SSL_set_hostflags(ssl, X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS);
    }
}

ClientConnection::~ClientConnection() = default;

Connection::result_t ClientConnection::connect(int timeout_ms) {
    assert(m_context != nullptr);
    m_last_error.clear();
    auto* ctx = m_context->ctx.get();
    return drive_ssl_op(
        ctx, [ctx] { return ssl_connect(ctx); }, timeout_ms, m_timeout_ms, ssl_op_kind::handshake, m_state,
        m_last_error);
}

// ----------------------------------------------------------------------------
// TLS Server

int Server::s_sig_int{-1};

Server::Server() : m_context(std::make_unique<server_ctx>()), m_status_request_v2(m_cache) {
}

Server::~Server() {
    stop();
    wait_stopped();
}

bool Server::init_socket(const config_t& cfg) {
    bool result = false;
    if (cfg.socket == INVALID_SOCKET) {
        BIO_ADDRINFO* addrinfo_tmp{nullptr};
        // AF_UNSPEC is another option but seems to prefer IPv4
        const int family{(cfg.ipv6_only) ? AF_INET6 : AF_INET};

        result = BIO_lookup_ex(cfg.host, cfg.service, BIO_LOOKUP_SERVER, family, SOCK_STREAM, IPPROTO_TCP,
                               &addrinfo_tmp) != 0;

        if (!result) {
            log_error("init_socket::BIO_lookup_ex");
        } else {
            std::unique_ptr<BIO_ADDRINFO> addrinfo(addrinfo_tmp);
            const auto sock_family = BIO_ADDRINFO_family(addrinfo.get());
            const auto sock_type = BIO_ADDRINFO_socktype(addrinfo.get());
            const auto sock_protocol = BIO_ADDRINFO_protocol(addrinfo.get());
            const auto* sock_address = BIO_ADDRINFO_address(addrinfo.get());
            m_socket = BIO_socket(sock_family, sock_type, sock_protocol, 0);

            if (m_socket == INVALID_SOCKET) {
                log_error("init_socket::BIO_socket");
                if (cfg.ipv6_only) {
                    log_warning("Verify that the configured interface has a valid IPv6 link local address configured.");
                }
            } else {
                int sock_options{BIO_SOCK_REUSEADDR | BIO_SOCK_NONBLOCK};
                if (cfg.ipv6_only) {
                    sock_options = BIO_SOCK_REUSEADDR | BIO_SOCK_V6_ONLY | BIO_SOCK_NONBLOCK;
                }

                result = BIO_listen(m_socket, sock_address, sock_options) != 0;
                if (!result) {
                    log_error("init_socket::BIO_listen");
                    BIO_closesocket(m_socket);
                    m_socket = INVALID_SOCKET;
                }
            }
        }
    } else {
        // the code that sets cfg.socket is responsible for
        // all socket initialisation
        m_socket = cfg.socket;
        result = true;
    }

    return result;
}

int Server::handle_tls_1_3_verify_upgrade(SSL* ssl, int* /*alert*/) {
    // The verify upgrade only takes effect when the negotiated protocol will be
    // TLS 1.3. Both sides must allow it: the client must advertise TLS 1.3 in
    // supported_versions and the server must not have capped at TLS 1.2.
    if (not m_verify_client_on_tls13) {
        return SSL_CLIENT_HELLO_SUCCESS;
    }
    auto* ctx = SSL_get_SSL_CTX(ssl);
    const auto server_max = (ctx != nullptr) ? SSL_CTX_get_max_proto_version(ctx) : 0;
    if (server_max != 0 && server_max < TLS1_3_VERSION) {
        return SSL_CLIENT_HELLO_SUCCESS;
    }

    const unsigned char* data{nullptr};
    std::size_t datalen{0};

    if (SSL_client_hello_get0_ext(ssl, TLSEXT_TYPE_supported_versions, &data, &datalen) == 1) {
        if (openssl::is_tls_1_3(data, datalen)) {
            log_info("Client supports TLS1.3: Change verify mode to SSL_VERIFY_PEER and "
                     "SSL_VERIFY_FAIL_IF_NO_PEER_CERT");
            SSL_set_verify(ssl, SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT, nullptr);
        }
    }

    return SSL_CLIENT_HELLO_SUCCESS;
}

int Server::client_hello_cb_dispatch(SSL* ssl, int* alert, void* object) {
    auto* self = static_cast<Server*>(object);
    if (self == nullptr) {
        return SSL_CLIENT_HELLO_SUCCESS;
    }

    if (const auto rc = self->handle_tls_1_3_verify_upgrade(ssl, alert); rc != SSL_CLIENT_HELLO_SUCCESS) {
        return rc;
    }
    if (const auto rc = self->m_status_request_v2.handle_client_hello(ssl, alert); rc != SSL_CLIENT_HELLO_SUCCESS) {
        return rc;
    }
    return SSL_CLIENT_HELLO_SUCCESS;
}

bool Server::init_ssl(const config_t& cfg) {
    assert(m_context != nullptr);

    bool result = (cfg.chains.size() > 0);
    if (cfg.enforce_tls_1_3 && (static_cast<const char*>(cfg.ciphersuites) == nullptr || cfg.ciphersuites[0] == '\0')) {
        // An enforce-TLS-1.3 server must pin its ciphersuites explicitly: "" would
        // fail every handshake and nullptr would silently rely on OpenSSL defaults.
        log_error("enforce_tls_1_3 requires a non-empty ciphersuites list");
        result = false;
    }
    SSL_CTX* ctx = nullptr;

    if (result) {
        // use the first server chain
        const ssl_ctx_params params{true, cfg.ciphersuites, cfg.cipher_list, true, cfg.enforce_tls_1_3};
        result = configure_ssl_ctx(ctx, cfg.chains[0], params);
        if (result) {

            if (cfg.tls_key_logging) {
                tls_key_log_file_path = std::filesystem::path(cfg.tls_key_logging_path) /= "tls_session_keys.log";

                ssl_keylog_file_index = SSL_CTX_get_ex_new_index(0, std::string("").data(), nullptr, nullptr, nullptr);
                ssl_keylog_server_index = SSL_get_ex_new_index(0, std::string("").data(), nullptr, nullptr, nullptr);

                if (ssl_keylog_file_index == -1 or ssl_keylog_server_index == -1) {
                    auto error_msg = std::string("_get_ex_new_index failed: ssl_keylog_file_index: ");
                    error_msg += std::to_string(ssl_keylog_file_index);
                    error_msg += ", ssl_keylog_server_index: " + std::to_string(ssl_keylog_server_index);
                    log_error(error_msg);
                } else {
                    SSL_CTX_set_ex_data(ctx, ssl_keylog_file_index, &tls_key_log_file_path);

                    SSL_CTX_set_keylog_callback(ctx, keylog_callback);
                    m_tls_key_interface = cfg.host;
                }
            }

            m_verify_client_on_tls13 = cfg.verify_client_on_tls13;

            // 15118-2 mandates TLS 1.2 and no client certificate; 15118-20 mandates TLS 1.3 and
            // requires a client certificate. The dispatcher upgrades verify mode to require a peer
            // certificate for TLS 1.3 connections in handle_tls_1_3_verify_upgrade so that TLS 1.2
            // connections still honor cfg.verify_client below.
            int mode = cfg.verify_client ? (SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT) : SSL_VERIFY_NONE;
            result = result && configure_verify_locations(ctx, cfg);
            SSL_CTX_set_verify(ctx, mode, nullptr);

            result = result && m_status_request_v2.init_ssl(ctx);
            result = result && m_server_trusted_ca_keys.init_ssl(ctx);

            SSL_CTX_set_client_hello_cb(ctx, &Server::client_hello_cb_dispatch, this);
        }
    }

    if (!result) {
        SSL_CTX_free(ctx);
        ctx = nullptr;
    }

    m_context->ctx = SSL_CTX_ptr(ctx);
    return ctx != nullptr;
}

void Server::deinit_ssl() {
    m_context = std::make_unique<server_ctx>();
}

bool Server::init_certificates(const std::vector<certificate_config_t>& chain_files) {
    std::vector<OcspCache::ocsp_entry_t> entries;
    openssl::chain_list chains;

    for (const auto& i : chain_files) {
        auto certs = openssl::load_certificates(i.certificate_chain_file);
        auto tas = openssl::load_certificates(i.trust_anchor_file);
        auto tas_pem = openssl::load_certificates_pem(i.trust_anchor_pem);
        auto pkey = openssl::load_private_key(i.private_key_file, i.private_key_password);

        // combine all trust anchor certificates
        std::move(tas_pem.begin(), tas_pem.end(), std::back_inserter(tas));

        if (certs.size() > 0) {
            openssl::chain_t chain;

            // update OCSP cache
            if (certs.size() == i.ocsp_response_files.size()) {
                for (std::size_t c = 0; c < certs.size(); c++) {
                    const auto& file = i.ocsp_response_files[c];
                    const auto& cert = certs[c];

                    if (file != nullptr) {
                        OcspCache::digest_t digest{};
                        if (OcspCache::digest(digest, cert.get())) {
                            entries.emplace_back(digest, file);
                        }
                    }
                }
            } else {
                log_warning("<n> certificates != <n> OCSP responses");
            }

            /*
             * If there are no trust anchors then the chain can't be verified
             * it also means that trusted_ca_keys can't be supported for the
             * chain.
             */

            if (!tas.empty()) {
                // update trusted CA keys information
                chain.chain.leaf = std::move(certs[0]);
                // remove server cert from intermediate list
                certs.erase(certs.begin());
                chain.chain.chain = std::move(certs);
                chain.chain.trust_anchors = std::move(tas);
                chain.private_key = std::move(pkey);

                if (openssl::verify_chain(chain)) {
                    chains.emplace_back(std::move(chain));
                }
            } else {
                const auto subject = openssl::certificate_subject(certs[0].get());
                std::string msg("No trust anchors for certificate:");
                for (const auto& item : subject) {
                    msg += ' ';
                    msg += item.first;
                    msg += ':';
                    msg += item.second;
                }
                log_warning(msg);
            }
        }
    }

    bool result{true};

    if (chains.empty()) {
        // continue without trusted_ca_keys support
        log_warning("trusted_ca_keys support disabled");
    }
    m_server_trusted_ca_keys.update(std::move(chains));

    // don't error when there are no OCSP cached responses
    if (!entries.empty()) {
        if (!m_cache.load(entries)) {
            result = false;
        }
    } else {
        // remove any existing entries
        (void)m_cache.load(entries);
    }

    return result;
}

void Server::deinit_certificates() {
    m_cache.load({});
    m_server_trusted_ca_keys.update({});
}

void Server::wait_for_connection(const ConnectionHandler& handler) {
    std::unique_ptr<BIO_ADDR> peer(BIO_ADDR_new());
    if (peer == nullptr) {
        log_error("serve::BIO_ADDR_new");
        m_exit = true;
    } else {
        int soc{INVALID_SOCKET};
        while ((soc < 0) && !m_exit) {
            auto poll_res = wait_for(m_socket, false, c_serve_timeout_ms);
            if (m_state == state_t::init_complete) {
                m_state = state_t::running;
            }
            if (poll_res == -1) {
                // poll() has failed
                m_exit = true;
            } else if ((poll_res == -2) || (poll_res == 0)) {
                // EINTR is -2 - triggered by stop()
                // timeout is 0
                // nothing to accept, check for m_exit
            } else {
                soc = BIO_accept_ex(m_socket, peer.get(), BIO_SOCK_NONBLOCK);
                if (BIO_sock_should_retry(soc) == 0) {
                    break;
                }
            }
        }

        if (soc >= 0) {
            bool reject{true};
            state_t tmp = m_state;
            switch (tmp) {
            case state_t::init_socket: {
                if (m_init_callback != nullptr) {
                    // attempt to get SSL configuration when not set yet
                    auto new_config = m_init_callback();
                    bool success{false};
                    if (new_config && new_config.value()) {
                        success = update(*new_config.value());
                    }
                    if (success) {
                        m_state = state_t::running;
                        reject = false;
                    }
                }
                break;
            }
            case state_t::init_complete:
            case state_t::running:
                reject = false;
                break;
            case state_t::init_needed:
            case state_t::stopped:
            default:
                break;
            }

            if (reject) {
                // updated configuration failed
                BIO_closesocket(soc);
                soc = INVALID_SOCKET;
            }
        }

        if (m_exit) {
            if (soc >= 0) {
                BIO_closesocket(soc);
            }
        } else {
            if (soc < 0) {
                log_error("serve::BIO_accept_ex");
            } else {
                // new connection, pass to handler
                auto* ip = BIO_ADDR_hostname_string(peer.get(), 1);
                auto* service = BIO_ADDR_service_string(peer.get(), 1);

                auto connection = std::make_unique<ServerConnection>(m_context->ctx.get(), soc, ip, service,
                                                                     m_timeout_ms, m_tls_key_interface);
                handler(std::move(connection));
                OPENSSL_free(ip);
                OPENSSL_free(service);
            }
        }
    }
}

Server::ConnectionPtr Server::wrap_accepted_fd(int soc, const char* ip, const char* service) {
    // without an initialized SSL_CTX the connection would null-deref on accept()
    if ((m_context == nullptr) || (m_context->ctx == nullptr)) {
        return nullptr;
    }
    return std::make_unique<ServerConnection>(m_context->ctx.get(), soc, ip, service, m_timeout_ms,
                                              m_tls_key_interface);
}

void Server::configure_signal_handler(int interrupt_signal) {
    s_sig_int = interrupt_signal;
    struct sigaction action {};
    action.sa_handler = &sig_int_handler;
    action.sa_flags = SA_RESTART;
    if (sigaction(s_sig_int, &action, nullptr) == -1) {
        log_error("Server::configure_signal_handler: " + std::to_string(errno));
    }
}

Server::state_t Server::init(const config_t& cfg, const ConfigurationCallback& init_ssl_cb) {
    // prevent multiple calls to init, or calling init whilst serve() is running
    std::lock_guard lock(m_mutex);
    m_init_callback = init_ssl_cb; // save handler for later
    m_state = state_t::init_needed;
    if (init_socket(cfg)) {
        (void)update(cfg);
    }
    return m_state;
}

bool Server::update(const config_t& cfg) {
    std::lock_guard lock(m_update_mutex);
    // does not change server socket settings, use init() if needed
    std::vector<OcspCache::ocsp_entry_t> entries;

    m_timeout_ms = cfg.io_timeout_ms;
    // always try init_certificates() and init_ssl()
    bool result = init_certificates(cfg.chains);
    if (!init_ssl(cfg)) {
        result = false;
    }
    m_state = (result) ? state_t::init_complete : state_t::init_socket;
    return result;
}

Server::state_t Server::serve(const ConnectionHandler& handler) {
    assert(m_context != nullptr);
    // prevent init() or server() being called while serve is running
    std::lock_guard lock(m_mutex);

    // check server socket configuration has been successful
    bool result{false};
    state_t tmp = m_state;
    switch (tmp) {
    case state_t::init_socket:
        if (m_init_callback != nullptr) {
            result = m_socket != INVALID_SOCKET;
        }
        break;
    case state_t::init_complete:
        result = m_socket != INVALID_SOCKET;
        break;
    case state_t::init_needed:
    case state_t::running:
    case state_t::stopped:
    default:
        break;
    }

    // set before publishing m_running so stop() never reads a stale (zero) id
    m_server_thread = pthread_self(); // for use by stop()

    // wakeup wait_running()
    {
        std::lock_guard lock(m_cv_mutex);
        m_running = true;
    }
    m_cv.notify_all();

    if (result) {
        m_exit = false;
        m_state = (m_state == state_t::init_complete) ? state_t::running : state_t::init_socket;
        while (!m_exit) {
            wait_for_connection(handler);
        }

        BIO_closesocket(m_socket);
        m_socket = INVALID_SOCKET;
        m_state = state_t::stopped;
    } else {
        // there wasn't a valid socket
        m_state = state_t::init_needed;
    }

    // wakeup wait_stopped()
    {
        std::lock_guard lock(m_cv_mutex);
        m_running = false;
    }
    m_cv.notify_all();
    return m_state;
}

bool Server::suspend() {
    bool result{false};
    std::lock_guard lock(m_update_mutex);
    state_t tmp = m_state;
    switch (tmp) {
    case state_t::running:
    case state_t::init_complete:
        m_state = state_t::init_socket;
        deinit_certificates();
        deinit_ssl();
        result = true;
        break;
    case state_t::init_socket:
    case state_t::init_needed:
    case state_t::stopped:
    default:
        break;
    }
    return result;
}

void Server::stop() {
    m_exit = true;
    // raise a signal if a hander was installed
    if (m_running && (s_sig_int != -1)) {
        pthread_kill(m_server_thread, s_sig_int);
    }
}

void Server::wait_running() {
    std::unique_lock lock(m_cv_mutex);
    m_cv.wait(lock, [this]() { return this->m_running; });
}

void Server::wait_stopped() {
    std::unique_lock lock(m_cv_mutex);
    m_cv.wait(lock, [this]() { return !this->m_running; });
}

// ----------------------------------------------------------------------------
// Client

Client::Client() :
    m_context(std::make_unique<client_ctx>()), m_status_request_v2(std::make_unique<ClientStatusRequestV2>()) {
}

Client::Client(std::unique_ptr<ClientStatusRequestV2>&& handler) :
    m_context(std::make_unique<client_ctx>()), m_status_request_v2(std::move(handler)) {
}

Client::~Client() = default;

bool Client::init(const config_t& cfg) {
    return init(cfg, default_overrides());
}

bool Client::init(const config_t& cfg, const override_t& override) {
    assert(m_context != nullptr);
    assert(override.tlsext_status_cb != nullptr);
    assert(override.status_request_v2_cb != nullptr);
    assert(override.status_request_v2_add != nullptr);
    assert(override.trusted_ca_keys_add != nullptr);
    assert(override.trusted_ca_keys_free != nullptr);

    m_timeout_ms = cfg.io_timeout_ms;
    m_verify_subject_name = cfg.verify_subject_name;
    if (cfg.verify_subject_name && !cfg.verify_server) {
        log_warning("hostname verification (verify_subject_name) requires verify_server=true to be enforced; "
                    "it will not reject a mismatched host otherwise");
    }
    m_trusted_ca_keys = cfg.trusted_ca_keys_data;
    SSL_CTX* ctx = nullptr;
    Server::certificate_config_t cert_config{};
    cert_config.certificate_chain_file = cfg.certificate_chain_file;
    cert_config.private_key_file = cfg.private_key_file;
    cert_config.private_key_password = cfg.private_key_password;
    const ssl_ctx_params params{false, cfg.ciphersuites, cfg.cipher_list, false};
    auto result = configure_ssl_ctx(ctx, cert_config, params);
    if (result && cfg.min_proto_version != 0) {
        if (SSL_CTX_set_min_proto_version(ctx, cfg.min_proto_version) == 0) {
            log_error("SSL_CTX_set_min_proto_version");
            result = false;
        }
    }
    if (result) {
        int mode = SSL_VERIFY_NONE;

        if (cfg.verify_server) {
            mode = SSL_VERIFY_PEER;
            if (SSL_CTX_load_verify_locations(ctx, cfg.verify_locations_file, cfg.verify_locations_path) != 1) {
                log_error("SSL_CTX_load_verify_locations");
            }
        } else {
            if (SSL_CTX_set_default_verify_paths(ctx) != 1) {
                log_error("SSL_CTX_set_default_verify_paths");
                result = false;
            }
        }

        SSL_CTX_set_verify(ctx, mode, nullptr);
        if (cfg.status_request) {
            if (SSL_CTX_set_tlsext_status_type(ctx, TLSEXT_STATUSTYPE_ocsp) != 1) {
                log_error("SSL_CTX_set_tlsext_status_type");
                result = false;
            }
        }

        if (cfg.status_request_v2) {
            constexpr int context = SSL_EXT_TLS_ONLY | SSL_EXT_TLS1_2_AND_BELOW_ONLY | SSL_EXT_IGNORE_ON_RESUMPTION |
                                    SSL_EXT_CLIENT_HELLO | SSL_EXT_TLS1_2_SERVER_HELLO;
            if (SSL_CTX_add_custom_ext(ctx, TLSEXT_TYPE_status_request_v2, context, override.status_request_v2_add,
                                       nullptr, nullptr, override.status_request_v2_cb,
                                       m_status_request_v2.get()) != 1) {
                log_error("SSL_CTX_add_custom_ext status_request_v2");
                result = false;
            }
        }

        if (cfg.status_request || cfg.status_request_v2) {
            if (SSL_CTX_set_tlsext_status_cb(ctx, override.tlsext_status_cb) != 1) {
                log_error("SSL_CTX_set_tlsext_status_cb");
                result = false;
            }
            if (SSL_CTX_set_tlsext_status_arg(ctx, m_status_request_v2.get()) != 1) {
                log_error("SSL_CTX_set_tlsext_status_arg");
                result = false;
            }
        }

        if (cfg.trusted_ca_keys) {
            constexpr int context = SSL_EXT_TLS_ONLY | SSL_EXT_IGNORE_ON_RESUMPTION | SSL_EXT_CLIENT_HELLO;
            if (SSL_CTX_add_custom_ext(ctx, TLSEXT_TYPE_trusted_ca_keys, context, override.trusted_ca_keys_add,
                                       override.trusted_ca_keys_free, &m_trusted_ca_keys, nullptr, nullptr) != 1) {
                log_error("SSL_CTX_add_custom_ext trusted_ca_keys");
                result = false;
            }
        }
    }

    if (result) {
        m_context->ctx = SSL_CTX_ptr(ctx);
    } else {
        SSL_CTX_free(ctx);
        ctx = nullptr;
    }

    return ctx != nullptr;
}

std::unique_ptr<ClientConnection> Client::connect(const char* host, const char* service, bool ipv6_only,
                                                  int timeout_ms) {
    BIO_ADDRINFO* addrinfo_tmp{nullptr};
    std::unique_ptr<ClientConnection> result;

    const int family = (ipv6_only) ? AF_INET6 : AF_UNSPEC;
    const bool lookup_result =
        BIO_lookup_ex(host, service, BIO_LOOKUP_CLIENT, family, SOCK_STREAM, IPPROTO_TCP, &addrinfo_tmp) != 0;

    if (!lookup_result) {
        log_error("connect::BIO_lookup_ex");
    } else {
        std::unique_ptr<BIO_ADDRINFO> addrinfo(addrinfo_tmp);
        const auto sock_family = BIO_ADDRINFO_family(addrinfo.get());
        const auto sock_type = BIO_ADDRINFO_socktype(addrinfo.get());
        const auto sock_protocol = BIO_ADDRINFO_protocol(addrinfo.get());
        const auto* sock_address = BIO_ADDRINFO_address(addrinfo.get());

        auto socket = BIO_socket(sock_family, sock_type, sock_protocol, 0);

        if (socket == INVALID_SOCKET) {
            log_error("connect::BIO_socket");
        } else {
            bool connected{true};
            constexpr int soc_opt = BIO_SOCK_NONBLOCK;

            // calls connect() - hence checking errno
            if (BIO_connect(socket, sock_address, soc_opt) != 1) {
                connected = false;
                if (errno == EINPROGRESS) {
                    // wait for write on the socket
                    auto res = wait_for_loop(socket, true, timeout_ms);
                    if (res == 0) {
                        log_error("connect::wait_for: timeout");
                    } else if (res > 0) {
                        res = BIO_sock_error(socket);
                        if (res == 0) {
                            connected = true;
                        } else {
                            log_error("connect::BIO_sock_error: " + std::to_string(res));
                        }
                    }
                } else {
                    log_error("connect::BIO_connect: " + std::to_string(errno));
                }
            }

            if (connected) {
                result = std::make_unique<ClientConnection>(m_context->ctx.get(), socket, host, service, m_timeout_ms,
                                                            m_verify_subject_name);
            }
        }
    }

    return result;
}

Client::ConnectionPtr Client::wrap_connecting_fd(int fd, const char* host_for_sni) {
    if (m_context == nullptr) {
        return nullptr;
    }
    return std::make_unique<ClientConnection>(m_context->ctx.get(), fd, (host_for_sni != nullptr) ? host_for_sni : "",
                                              "", m_timeout_ms, m_verify_subject_name);
}

Client::override_t Client::default_overrides() {
    return {
        &ClientStatusRequestV2::status_request_v2_multi_cb, &ClientStatusRequestV2::status_request_v2_add,
        &ClientStatusRequestV2::status_request_v2_cb,       &ClientTrustedCaKeys::trusted_ca_keys_add,
        &ClientTrustedCaKeys::trusted_ca_keys_free,
    };
}

// ----------------------------------------------------------------------------
// TlsKeyLoggingServer

TlsKeyLoggingServer::TlsKeyLoggingServer(const std::string& interface_name, uint16_t port_) : port(port_) {
    static constexpr auto LINK_LOCAL_MULTICAST = "ff02::1";
    bool result{true};

    fd = socket(AF_INET6, SOCK_DGRAM, 0);
    if (fd == -1) {
        log_error("Could not create socket");
        result = false;
    }

    if (result) {
        // source setup
        // find port between 49152-65535
        auto could_bind = false;
        auto source_port = 49152;
        for (; source_port < 65535; source_port++) {
            sockaddr_in6 source_address = {AF_INET6, htons(source_port), 0, {}, 0};
            if (bind(fd, reinterpret_cast<sockaddr*>(&source_address), sizeof(sockaddr_in6)) == 0) {
                could_bind = true;
                break;
            }
        }

        if (could_bind) {
            log_info("UDP socket bound to source port: " + std::to_string(source_port));
        } else {
            log_error("Could not bind");
            result = false;
        }
    }

    if (result) {
        auto mreq = ipv6_mreq{};
        const auto index = if_nametoindex(interface_name.c_str());
        mreq.ipv6mr_interface = index;
        if (inet_pton(AF_INET6, LINK_LOCAL_MULTICAST, &mreq.ipv6mr_multiaddr) <= 0) {
            log_error("Failed to setup multicast address");
            result = false;
        }

        if (setsockopt(fd, IPPROTO_IPV6, IPV6_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
            log_error("Could not add multicast group membership");
            result = false;
        }

        if (setsockopt(fd, IPPROTO_IPV6, IPV6_MULTICAST_IF, &index, sizeof(index)) < 0) {
            log_error("Could not set interface name:" + interface_name);
            result = false;
        }

        destination_address = {AF_INET6, htons(port), 0, {}, 0};
        if (inet_pton(AF_INET6, LINK_LOCAL_MULTICAST, &destination_address.sin6_addr) <= 0) {
            log_error("Failed to setup server address, reset key_log_fd");
            result = false;
        }
    }

    if (!result && fd != -1) {
        close(fd);
        fd = -1;
    }
}

TlsKeyLoggingServer::~TlsKeyLoggingServer() {
    if (fd != -1) {
        close(fd);
    }
}

ssize_t TlsKeyLoggingServer::send(const char* line) {
    return sendto(fd, line, strlen(line), 0, reinterpret_cast<const sockaddr*>(&destination_address),
                  sizeof(destination_address));
}

} // namespace tls
