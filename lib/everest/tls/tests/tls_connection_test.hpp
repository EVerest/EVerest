// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Pionix GmbH and Contributors to EVerest

#ifndef TLS_CONNECTION_TEST_HPP_
#define TLS_CONNECTION_TEST_HPP_

#include "extensions/helpers.hpp"
#include <everest/tls/openssl_util.hpp>

#include <array>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <everest/tls/tls.hpp>
#include <gtest/gtest.h>
#include <memory>
#include <openssl/ssl.h>
#include <thread>
#include <unistd.h>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>

#include <everest/util/enum/EnumFlags.hpp>

using namespace std::chrono_literals;

// The shared fixture lives in a named namespace (not an anonymous one) so that
// the TlsTest type has external linkage and a single identity across every
// translation unit that includes this header. GoogleTest requires all TEST_F
// cases in one test suite to use the exact same fixture class; an anonymous
// namespace would give each TU its own distinct TlsTest type and abort the
// suite at runtime once the cases are split across files.
namespace tls_test {
using tls::status_request::ClientStatusRequestV2;

// ----------------------------------------------------------------------------
// set up code

struct ClientStatusRequestV2Test : public ClientStatusRequestV2 {
    enum class flags_t : std::uint8_t {
        status_request_cb,
        status_request,
        status_request_v2,
        connected,
        last = connected,
    };

    everest::lib::util::AtomicEnumFlags<flags_t>& flags;

    ClientStatusRequestV2Test() = delete;
    explicit ClientStatusRequestV2Test(everest::lib::util::AtomicEnumFlags<flags_t>& flag_ref) : flags(flag_ref) {
    }

    int status_request_cb(tls::Ssl* ctx) override {
        /*
         * This callback is called when status_request or status_request_v2 extensions
         * were present in the Client Hello. It doesn't mean that the extension is in
         * the Server Hello SSL_get_tlsext_status_ocsp_resp() returns -1 in that case
         */

        int result{1};
        const unsigned char* response{nullptr};
        const auto total_length = SSL_get_tlsext_status_ocsp_resp(ctx, &response);
        flags.set(flags_t::status_request_cb);
        if ((response != nullptr) && (total_length > 0) && (total_length <= std::numeric_limits<std::int32_t>::max())) {
            switch (response[0]) {
            case 0x30: // a status_request response
                flags.set(flags_t::status_request);
                if (!print_ocsp_response(stdout, response, total_length)) {
                    result = 0;
                }
                break;
            case 0x00: // a status_request_v2 response
            {
                flags.set(flags_t::status_request_v2);

                // multiple responses
                auto remaining = static_cast<std::int32_t>(total_length);
                const unsigned char* ptr{response};

                while (remaining >= 3) {
                    const auto len = tls::uint24(ptr);
                    tls::update_position(ptr, remaining, 3);
                    // print_ocsp_response updates tmp_p
                    auto* tmp_p = ptr;
                    const auto res = print_ocsp_response(stdout, tmp_p, len);
                    tls::update_position(ptr, remaining, len);
                    if (!res || (ptr != tmp_p)) {
                        result = 0;
                        remaining = -1;
                    }
                }

                if (remaining != 0) {
                    result = 0;
                }
                break;
            }
            default:
                break;
            }
        }
        return result;
    }
};

struct ClientTest : public tls::Client {
    using flags_t = ClientStatusRequestV2Test::flags_t;
    everest::lib::util::AtomicEnumFlags<flags_t> flags;

    ClientTest() : tls::Client(std::unique_ptr<ClientStatusRequestV2>(new ClientStatusRequestV2Test(flags))) {
    }

    void reset() {
        flags.reset();
    }
};

inline void handler(tls::Server::ConnectionPtr&& con) {
    if (con->accept() == tls::Connection::result_t::success) {
        std::uint32_t count{0};
        std::array<std::byte, 1024> buffer{};
        bool bExit = false;
        while (!bExit) {
            std::size_t readbytes = 0;
            std::size_t writebytes = 0;

            switch (con->read(buffer.data(), buffer.size(), readbytes)) {
            case tls::Connection::result_t::success:
                switch (con->write(buffer.data(), readbytes, writebytes)) {
                case tls::Connection::result_t::success:
                    break;
                case tls::Connection::result_t::timeout:
                case tls::Connection::result_t::closed:
                default:
                    bExit = true;
                    break;
                }
                break;
            case tls::Connection::result_t::timeout:
                count++;
                if (count > 10) {
                    bExit = true;
                }
                break;
            case tls::Connection::result_t::closed:
            default:
                bExit = true;
                break;
            }
        }
        con->shutdown();
    }
}

inline void run_server(tls::Server& server) {
    server.serve(&handler);
}

// ----------------------------------------------------------------------------
// socket-level helpers shared by the wrap_accepted_fd / wrap_connecting_fd
// tests. These return error values instead of using GoogleTest assertions
// (which require a void return type); callers assert on the results.

// A test-owned loopback listen socket on a kernel-assigned ephemeral port.
// fd is -1 when any setup step failed.
struct LoopbackListener {
    int fd{-1};
    std::uint16_t port{0};
};

inline LoopbackListener make_loopback_listener() {
    LoopbackListener listener;
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return listener;
    }
    int reuse = 1;
    (void)::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;

    sockaddr_in bound{};
    socklen_t bound_len = sizeof(bound);
    if ((::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) || (::listen(fd, 1) != 0) ||
        (::getsockname(fd, reinterpret_cast<sockaddr*>(&bound), &bound_len) != 0)) {
        (void)::close(fd);
        return listener;
    }
    listener.fd = fd;
    listener.port = ntohs(bound.sin_port);
    return listener;
}

// Accept one pending TCP connection on listen_fd, resolve the peer's numeric
// address, and wrap the accepted fd as a server-side TLS connection.
// Returns nullptr when accept(2), name resolution, or the wrap fails.
inline tls::Server::ConnectionPtr accept_and_wrap(tls::Server& server, int listen_fd) {
    sockaddr_in peer_addr{};
    socklen_t peer_len = sizeof(peer_addr);
    const int accepted_fd = ::accept(listen_fd, reinterpret_cast<sockaddr*>(&peer_addr), &peer_len);
    if (accepted_fd < 0) {
        return nullptr;
    }
    char ip_buf[INET_ADDRSTRLEN]{};
    char service_buf[NI_MAXSERV]{};
    if (::getnameinfo(reinterpret_cast<sockaddr*>(&peer_addr), peer_len, ip_buf, sizeof(ip_buf), service_buf,
                      sizeof(service_buf), NI_NUMERICHOST | NI_NUMERICSERV) != 0) {
        (void)::close(accepted_fd);
        return nullptr;
    }
    return server.wrap_accepted_fd(accepted_fd, ip_buf, service_buf);
}

// Open a non-blocking TCP socket and connect it to the loopback address on
// the given port, polling out an in-flight EINPROGRESS connect. Returns the
// connecting fd, or -1 on failure.
inline int connect_loopback_nonblocking(std::uint16_t port) {
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }
    const int flags = ::fcntl(fd, F_GETFL, 0);
    if ((flags == -1) || (::fcntl(fd, F_SETFL, flags | O_NONBLOCK) != 0)) {
        (void)::close(fd);
        return -1;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port);
    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        if (errno != EINPROGRESS) {
            (void)::close(fd);
            return -1;
        }
        pollfd pfd{fd, POLLOUT, 0};
        int err = 0;
        socklen_t err_len = sizeof(err);
        if ((::poll(&pfd, 1, 2000) <= 0) || (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &err_len) != 0) ||
            (err != 0)) {
            (void)::close(fd);
            return -1;
        }
    }
    return fd;
}

// Outcome of drive_client_handshake: the final connect() result plus how many
// want_read / want_write readiness signals were observed along the way.
struct HandshakeDrive {
    tls::Connection::result_t result{tls::Connection::result_t::timeout};
    int want_read{0};
    int want_write{0};
};

// Drive the client-side TLS handshake on a wrapped non-blocking fd, polling
// for socket readiness whenever connect() reports want_read / want_write.
// Stops early on success or closed.
inline HandshakeDrive drive_client_handshake(tls::ClientConnection& conn, int fd, int connect_timeout_ms = 1000,
                                             int max_attempts = 50, int poll_timeout_ms = 1000) {
    using result_t = tls::Connection::result_t;
    HandshakeDrive drive;
    for (int i = 0; i < max_attempts && drive.result != result_t::success && drive.result != result_t::closed; ++i) {
        drive.result = conn.connect(connect_timeout_ms);
        if (drive.result == result_t::want_read) {
            ++drive.want_read;
            pollfd pfd{fd, POLLIN, 0};
            (void)::poll(&pfd, 1, poll_timeout_ms);
        } else if (drive.result == result_t::want_write) {
            ++drive.want_write;
            pollfd pfd{fd, POLLOUT, 0};
            (void)::poll(&pfd, 1, poll_timeout_ms);
        }
    }
    return drive;
}

class TlsTest : public testing::Test {
protected:
    using flags_t = ClientTest::flags_t;

    tls::Server server;
    tls::Server::config_t server_config;
    std::thread server_thread;
    ClientTest client;
    tls::Client::config_t client_config;

    static void SetUpTestSuite() {
        struct sigaction action;
        std::memset(&action, 0, sizeof(action));
        action.sa_handler = SIG_IGN;
        sigaction(SIGPIPE, &action, nullptr);
        tls::Server::configure_signal_handler(SIGUSR1);
    }

    void SetUp() override {
        server_config.cipher_list = "ECDHE-ECDSA-AES128-SHA256";
        // server_config.ciphersuites = "TLS_AES_128_GCM_SHA256:TLS_AES_256_GCM_SHA384";
        server_config.ciphersuites = "";
        auto& ref0 = server_config.chains.emplace_back();
        ref0.certificate_chain_file = "server_chain.pem";
        ref0.private_key_file = "server_priv.pem";
        ref0.trust_anchor_file = "server_root_cert.pem";
        ref0.ocsp_response_files = {"ocsp_response.der", "ocsp_response.der"};
        auto& ref1 = server_config.chains.emplace_back();
        ref1.certificate_chain_file = "alt_server_chain.pem";
        ref1.private_key_file = "alt_server_priv.pem";
        ref1.trust_anchor_file = "alt_server_root_cert.pem";
        ref1.ocsp_response_files = {"ocsp_response.der", "ocsp_response.der"};
        // server_config.verify_locations_file = "client_root_cert.pem";
        server_config.host = "127.0.0.1";
        server_config.service = "8444";
        server_config.ipv6_only = false;
        server_config.verify_client = false;
        server_config.io_timeout_ms = 1000; // no lower than 200ms, valgrind need much higher

        client_config.cipher_list = "ECDHE-ECDSA-AES128-SHA256";
        // client_config.ciphersuites = "TLS_AES_128_GCM_SHA256:TLS_AES_256_GCM_SHA384";
        // client_config.certificate_chain_file = "client_chain.pem";
        // client_config.private_key_file = "client_priv.pem";
        client_config.verify_locations_file = "server_root_cert.pem";
        client_config.io_timeout_ms = 1000;
        client_config.verify_server = true;
        client_config.status_request = false;
        client_config.status_request_v2 = false;
        client.reset();
    }

    void TearDown() override {
        server.stop();
        server.wait_stopped();
        if (server_thread.joinable()) {
            server_thread.join();
        }
    }

    void start(const tls::Server::ConfigurationCallback& init_ssl = nullptr) {
        using state_t = tls::Server::state_t;
        const auto res = server.init(server_config, init_ssl);
        if ((res == state_t::init_complete) || (res == state_t::init_socket)) {
            server_thread = std::thread(&run_server, std::ref(server));
            server.wait_running();
        }
    }

    void start(const std::function<void(tls::Server::ConnectionPtr&& con)>& handler) {
        using state_t = tls::Server::state_t;
        const auto res = server.init(server_config, nullptr);
        if ((res == state_t::init_complete) || (res == state_t::init_socket)) {
            server_thread = std::thread([this, handler]() { this->server.serve(handler); });
            server.wait_running();
        }
    }

    void connect(const std::function<void(tls::Client::ConnectionPtr& con)>& handler = nullptr) {
        client.init(client_config);
        client.reset();
        // localhost works in some cases but not in the CI pipeline for IPv6
        // use ip6-localhost
        auto connection = client.connect("127.0.0.1", "8444", false, 1000);
        if (handler == nullptr) {
            if (connection) {
                if (connection->connect() == tls::Connection::result_t::success) {
                    set(ClientTest::flags_t::connected);
                    connection->shutdown();
                }
            }
        } else {
            handler(connection);
        }
    }

    void set(flags_t flag) {
        client.flags.set(flag);
    }

    [[nodiscard]] bool is_set(flags_t flag) const {
        return client.flags.is_set(flag);
    }

    [[nodiscard]] bool is_reset(flags_t flag) const {
        return client.flags.is_reset(flag);
    }

    void add_ta_cert_hash(const char* filename) {
        openssl::sha_1_digest_t digest;
        auto certs = openssl::load_certificates(filename);
        for (const auto& ta : certs) {
            if (openssl::certificate_sha_1(digest, ta.get())) {
                client_config.trusted_ca_keys_data.cert_sha1_hash.push_back(digest);
            }
        }
    }
    void add_ta_key_hash(const char* filename) {
        openssl::sha_1_digest_t digest;
        auto certs = openssl::load_certificates(filename);
        for (const auto& ta : certs) {
            if (openssl::certificate_subject_public_key_sha_1(digest, ta.get())) {
                client_config.trusted_ca_keys_data.key_sha1_hash.push_back(digest);
            }
        }
    }
    void add_ta_name(const char* filename) {
        auto certs = openssl::load_certificates(filename);
        for (const auto& ta : certs) {
            auto res = openssl::certificate_subject_der(ta.get());
            if (res) {
                client_config.trusted_ca_keys_data.x509_name.emplace_back(std::move(res));
            }
        }
    }
};

class TlsTestTpm : public TlsTest {
protected:
    void SetUp() override {
        server_config.cipher_list = "ECDHE-ECDSA-AES128-SHA256";
        // server_config.ciphersuites = "TLS_AES_128_GCM_SHA256:TLS_AES_256_GCM_SHA384";
        server_config.ciphersuites = "";
        auto& ref0 = server_config.chains.emplace_back();
        ref0.certificate_chain_file = "tpm_pki/server_chain.pem";
        ref0.private_key_file = "tpm_pki/server_priv.pem";
        ref0.trust_anchor_file = "tpm_pki/server_root_cert.pem";
        ref0.ocsp_response_files = {"ocsp_response.der", "ocsp_response.der"};
        // auto& ref1 = server_config.chains.emplace_back();
        // ref1.certificate_chain_file = "alt_server_chain.pem";
        // ref1.private_key_file = "alt_server_priv.pem";
        // ref1.trust_anchor_file = "alt_server_root_cert.pem";
        // ref1.ocsp_response_files = {"ocsp_response.der", "ocsp_response.der"};
        server_config.host = "127.0.0.1";
        server_config.service = "8444";
        server_config.ipv6_only = false;
        server_config.verify_client = false;
        server_config.io_timeout_ms = 1000; // no lower than 200ms, valgrind need much higher

        client_config.cipher_list = "ECDHE-ECDSA-AES128-SHA256";
        // client_config.ciphersuites = "TLS_AES_128_GCM_SHA256:TLS_AES_256_GCM_SHA384";
        // client_config.certificate_chain_file = "client_chain.pem";
        // client_config.private_key_file = "client_priv.pem";
        client_config.verify_locations_file = "tpm_pki/server_root_cert.pem";
        client_config.io_timeout_ms = 1000;
        client_config.verify_server = true;
        client_config.status_request = false;
        client_config.status_request_v2 = false;
        client.reset();
    }
};

} // namespace tls_test

// Bring the fixture and helpers into scope so the test translation units can
// keep referring to TlsTest / ClientTest / flags_t unqualified. This header is
// only ever included by the TLS test executables.
using namespace tls_test; // NOLINT(google-build-using-namespace)

#endif // TLS_CONNECTION_TEST_HPP_
