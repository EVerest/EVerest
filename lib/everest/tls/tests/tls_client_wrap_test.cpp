// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "tls_connection_test.hpp"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <poll.h>
#include <set>
#include <sstream>
#include <string>
#include <tuple>
#include <unistd.h>
#include <utility>
#include <vector>

// Empty derived fixture: gives these cases their own suite name so they stay
// addressable via --gtest_filter.
struct TlsClientWrapTest : TlsTest {};

namespace {

// A fresh directory, removed with everything in it.
class KeyLogDir {
public:
    KeyLogDir() {
        std::string tmpl = (std::filesystem::temp_directory_path() / "everest_tls_keylog_XXXXXX").string();
        if (::mkdtemp(tmpl.data()) != nullptr) {
            m_path = tmpl;
        }
    }
    ~KeyLogDir() {
        if (!m_path.empty()) {
            std::error_code ec;
            std::filesystem::remove_all(m_path, ec);
        }
    }
    KeyLogDir(const KeyLogDir&) = delete;
    KeyLogDir& operator=(const KeyLogDir&) = delete;

    const std::filesystem::path& path() const {
        return m_path;
    }

private:
    std::filesystem::path m_path;
};

std::mutex keylog_logs_mutex;
std::vector<std::pair<openssl::log_level_t, std::string>> keylog_logs;

void capture_keylog_log(openssl::log_level_t level, const std::string& str) {
    const std::lock_guard lock(keylog_logs_mutex);
    keylog_logs.emplace_back(level, str);
}

// Captures library log output while alive, from any thread.
class KeyLogCapture {
public:
    KeyLogCapture() : m_previous(openssl::set_log_handler(&capture_keylog_log)) {
        const std::lock_guard lock(keylog_logs_mutex);
        keylog_logs.clear();
    }
    ~KeyLogCapture() {
        openssl::set_log_handler(m_previous);
    }
    KeyLogCapture(const KeyLogCapture&) = delete;
    KeyLogCapture& operator=(const KeyLogCapture&) = delete;

    static bool contains(openssl::log_level_t level, const std::string& needle) {
        const std::lock_guard lock(keylog_logs_mutex);
        return std::any_of(keylog_logs.begin(), keylog_logs.end(), [&](const auto& entry) {
            return entry.first == level && entry.second.find(needle) != std::string::npos;
        });
    }

private:
    openssl::log_handler_t m_previous;
};

// One accepted and completed TLS handshake against @p target. Returns the client's result.
tls::Connection::result_t handshake_once(tls::Server& target, int listen_fd, std::uint16_t port, ClientTest& client,
                                         const tls::Client::config_t& client_config) {
    using result_t = tls::Connection::result_t;
    const int client_fd = connect_loopback_nonblocking(port);
    if (client_fd < 0) {
        return result_t::closed;
    }
    auto server_conn = accept_and_wrap(target, listen_fd);
    if (!server_conn) {
        std::ignore = ::close(client_fd);
        return result_t::closed;
    }
    std::thread server_side([&]() {
        std::ignore = server_conn->accept(1000);
        std::ignore = server_conn->shutdown(1000);
    });
    client.init(client_config);
    auto client_conn = client.wrap_connecting_fd(client_fd, "localhost");
    auto result = result_t::closed;
    if (client_conn) {
        result = drive_client_handshake(*client_conn, client_fd).result;
        std::ignore = client_conn->shutdown(1000);
    } else {
        std::ignore = ::close(client_fd);
    }
    server_side.join();
    return result;
}

std::string read_text(const std::filesystem::path& path) {
    std::ifstream in(path);
    std::stringstream content;
    content << in.rdbuf();
    return content.str();
}

// Makes @p dir the working directory while alive, restoring the previous one on exit.
class ScopedCwd {
public:
    explicit ScopedCwd(const std::filesystem::path& dir) : m_previous(std::filesystem::current_path()) {
        std::filesystem::current_path(dir);
    }
    ~ScopedCwd() {
        std::error_code ec;
        std::filesystem::current_path(m_previous, ec);
    }
    ScopedCwd(const ScopedCwd&) = delete;
    ScopedCwd& operator=(const ScopedCwd&) = delete;

private:
    std::filesystem::path m_previous;
};

} // namespace

TEST_F(TlsClientWrapTest, WrapConnectingFdHandshake) {
    using state_t = tls::Server::state_t;
    using result_t = tls::Connection::result_t;

    // Passed via server_config.socket so init() skips its own bind/listen path.
    const auto listener = make_loopback_listener();
    ASSERT_GE(listener.fd, 0);

    server_config.socket = listener.fd;
    const auto init_state = server.init(server_config, nullptr);
    ASSERT_TRUE(init_state == state_t::init_complete || init_state == state_t::init_socket);

    std::thread server_side([&]() {
        auto server_conn = accept_and_wrap(server, listener.fd);
        if (server_conn) {
            std::ignore = server_conn->accept(1000);
            std::ignore = server_conn->shutdown(1000);
        }
    });

    const int client_fd = connect_loopback_nonblocking(listener.port);
    ASSERT_GE(client_fd, 0);

    ClientTest local_client;
    local_client.init(client_config);

    auto conn = local_client.wrap_connecting_fd(client_fd, "localhost");
    ASSERT_TRUE(conn);
    EXPECT_EQ(conn->socket(), client_fd);

    const auto drive = drive_client_handshake(*conn, client_fd);
    EXPECT_EQ(drive.result, result_t::success);
    EXPECT_NE(conn->peer_certificate(), nullptr);

    std::ignore = conn->shutdown(1000);

    if (server_side.joinable()) {
        server_side.join();
    }
    std::ignore = ::close(listener.fd);
}

// The PKI server leaf carries DNS:localhost, so pinning to "localhost" passes.
TEST_F(TlsClientWrapTest, WrapConnectingFdVerifiesHostname) {
    using state_t = tls::Server::state_t;
    using result_t = tls::Connection::result_t;

    const auto listener = make_loopback_listener();
    ASSERT_GE(listener.fd, 0);

    server_config.socket = listener.fd;
    const auto init_state = server.init(server_config, nullptr);
    ASSERT_TRUE(init_state == state_t::init_complete || init_state == state_t::init_socket);

    std::thread server_side([&]() {
        auto server_conn = accept_and_wrap(server, listener.fd);
        if (server_conn) {
            std::ignore = server_conn->accept(1000);
            std::ignore = server_conn->shutdown(1000);
        }
    });

    const int client_fd = connect_loopback_nonblocking(listener.port);
    ASSERT_GE(client_fd, 0);

    tls::Client::config_t verify_config = client_config;
    verify_config.verify_subject_name = true;

    ClientTest local_client;
    local_client.init(verify_config);

    auto conn = local_client.wrap_connecting_fd(client_fd, "localhost");
    ASSERT_TRUE(conn);

    const auto drive = drive_client_handshake(*conn, client_fd);
    EXPECT_EQ(drive.result, result_t::success);
    EXPECT_NE(conn->peer_certificate(), nullptr);

    std::ignore = conn->shutdown(1000);

    if (server_side.joinable()) {
        server_side.join();
    }
    std::ignore = ::close(listener.fd);
}

// Covers the IP-literal pinning branch, which is otherwise never executed: the peer
// is named by an IP so the pin goes through X509_VERIFY_PARAM_set1_ip_asc and must
// match the leaf's iPAddress SAN entry. A negative IP test is deliberately absent, it
// would fail whichever pinning call the code made and so could not tell them apart.
TEST_F(TlsClientWrapTest, WrapConnectingFdVerifiesIpLiteral) {
    using state_t = tls::Server::state_t;
    using result_t = tls::Connection::result_t;

    const auto listener = make_loopback_listener();
    ASSERT_GE(listener.fd, 0);

    server_config.socket = listener.fd;
    const auto init_state = server.init(server_config, nullptr);
    ASSERT_TRUE(init_state == state_t::init_complete || init_state == state_t::init_socket);

    std::thread server_side([&]() {
        auto server_conn = accept_and_wrap(server, listener.fd);
        if (server_conn) {
            std::ignore = server_conn->accept(1000);
            std::ignore = server_conn->shutdown(1000);
        }
    });

    const int client_fd = connect_loopback_nonblocking(listener.port);
    ASSERT_GE(client_fd, 0);

    tls::Client::config_t verify_config = client_config;
    verify_config.verify_subject_name = true;

    ClientTest local_client;
    local_client.init(verify_config);

    auto conn = local_client.wrap_connecting_fd(client_fd, "127.0.0.1");
    ASSERT_TRUE(conn);

    const auto drive = drive_client_handshake(*conn, client_fd);
    EXPECT_EQ(drive.result, result_t::success);
    EXPECT_NE(conn->peer_certificate(), nullptr);

    std::ignore = conn->shutdown(1000);

    if (server_side.joinable()) {
        server_side.join();
    }
    std::ignore = ::close(listener.fd);
}

// The server leaf carries DNS:localhost and DNS:evse.pionix.de, so
// "wrong.example" matches no subject/SAN entry.
TEST_F(TlsClientWrapTest, WrapConnectingFdRejectsWrongHostname) {
    using state_t = tls::Server::state_t;
    using result_t = tls::Connection::result_t;

    const auto listener = make_loopback_listener();
    ASSERT_GE(listener.fd, 0);

    server_config.socket = listener.fd;
    const auto init_state = server.init(server_config, nullptr);
    ASSERT_TRUE(init_state == state_t::init_complete || init_state == state_t::init_socket);

    std::thread server_side([&]() {
        auto server_conn = accept_and_wrap(server, listener.fd);
        if (server_conn) {
            std::ignore = server_conn->accept(1000);
            std::ignore = server_conn->shutdown(1000);
        }
    });

    const int client_fd = connect_loopback_nonblocking(listener.port);
    ASSERT_GE(client_fd, 0);

    tls::Client::config_t verify_config = client_config;
    verify_config.verify_subject_name = true;

    ClientTest local_client;
    local_client.init(verify_config);

    auto conn = local_client.wrap_connecting_fd(client_fd, "wrong.example");
    ASSERT_TRUE(conn);

    const auto drive = drive_client_handshake(*conn, client_fd);
    EXPECT_NE(drive.result, result_t::success);

    std::ignore = conn->shutdown(1000);

    if (server_side.joinable()) {
        server_side.join();
    }
    std::ignore = ::close(listener.fd);
}

// A non-numeric service string must not throw out of ServerConnection; only key
// logging is skipped. The CTX-wide keylog callback still fires for such a
// connection, so the handshake is driven to completion to cover that path.
TEST_F(TlsClientWrapTest, WrapAcceptedFdToleratesNonNumericService) {
    using state_t = tls::Server::state_t;
    using result_t = tls::Connection::result_t;

    const auto listener = make_loopback_listener();
    ASSERT_GE(listener.fd, 0);

    server_config.socket = listener.fd;
    server_config.tls_key_logging = true;
    server_config.tls_key_logging_path = ::testing::TempDir();
    const auto init_state = server.init(server_config, nullptr);
    ASSERT_TRUE(init_state == state_t::init_complete || init_state == state_t::init_socket);

    // Empty service string.
    {
        const int client_fd = connect_loopback_nonblocking(listener.port);
        ASSERT_GE(client_fd, 0);

        const int accepted_fd = ::accept(listener.fd, nullptr, nullptr);
        ASSERT_GE(accepted_fd, 0);

        tls::Server::ConnectionPtr conn;
        ASSERT_NO_THROW(conn = server.wrap_accepted_fd(accepted_fd, "::1", ""));
        ASSERT_TRUE(conn);

        auto server_result = result_t::timeout;
        std::thread server_side([&]() {
            server_result = conn->accept(1000);
            std::ignore = conn->shutdown(1000);
        });

        client.init(client_config);
        auto client_conn = client.wrap_connecting_fd(client_fd, "localhost");
        ASSERT_TRUE(client_conn);

        const auto drive = drive_client_handshake(*client_conn, client_fd);
        EXPECT_EQ(drive.result, result_t::success);

        std::ignore = client_conn->shutdown(1000);

        if (server_side.joinable()) {
            server_side.join();
        }
        EXPECT_EQ(server_result, result_t::success);
    }

    // Garbage service string.
    {
        const int client_fd = connect_loopback_nonblocking(listener.port);
        ASSERT_GE(client_fd, 0);

        const int accepted_fd = ::accept(listener.fd, nullptr, nullptr);
        ASSERT_GE(accepted_fd, 0);

        tls::Server::ConnectionPtr conn;
        ASSERT_NO_THROW(conn = server.wrap_accepted_fd(accepted_fd, "::1", "not-a-port"));
        EXPECT_TRUE(conn);

        // conn owns accepted_fd, only the client fd needs closing here.
        std::ignore = ::close(client_fd);
    }

    std::ignore = ::close(listener.fd);
}

// A reader draining a message must be able to tell that decrypted plaintext is
// still buffered. One TLS record can carry more plaintext than the caller's
// buffer holds, so a short read leaves the remainder in the record layer, and
// the socket will not necessarily become readable again to deliver it. A drain
// loop keyed only on socket readability would strand that remainder.
TEST_F(TlsClientWrapTest, WrapConnectingFdReportsBufferedPlaintext) {
    using state_t = tls::Server::state_t;
    using result_t = tls::Connection::result_t;

    const auto listener = make_loopback_listener();
    ASSERT_GE(listener.fd, 0);

    server_config.socket = listener.fd;
    const auto init_state = server.init(server_config, nullptr);
    ASSERT_TRUE(init_state == state_t::init_complete || init_state == state_t::init_socket);

    // Sent with a single write(), hence a single TLS record, and read back in
    // chunks far smaller than the record's plaintext.
    constexpr std::size_t payload_size = 512;
    constexpr std::size_t chunk_size = 64;
    std::array<std::byte, payload_size> payload{};
    for (std::size_t i = 0; i < payload.size(); ++i) {
        payload[i] = static_cast<std::byte>('a' + (i % 26));
    }

    std::thread server_side([&]() {
        auto server_conn = accept_and_wrap(server, listener.fd);
        if (!server_conn) {
            return;
        }
        if (server_conn->accept(1000) == result_t::success) {
            std::size_t sent = 0;
            std::ignore = server_conn->write(payload.data(), payload.size(), sent, 1000);
        }
        std::ignore = server_conn->shutdown(1000);
    });

    const int client_fd = connect_loopback_nonblocking(listener.port);
    ASSERT_GE(client_fd, 0);

    ClientTest local_client;
    local_client.init(client_config);

    auto conn = local_client.wrap_connecting_fd(client_fd, "localhost");
    ASSERT_TRUE(conn);

    const auto drive = drive_client_handshake(*conn, client_fd);
    ASSERT_EQ(drive.result, result_t::success);

    std::array<std::byte, payload_size> received{};
    std::size_t got = 0;
    EXPECT_EQ(conn->read(received.data(), chunk_size, got, 1000), result_t::success);
    EXPECT_EQ(got, chunk_size);
    EXPECT_LT(got, payload.size());

    // The record was decrypted in full, so its remainder is held by the record
    // layer rather than by the socket.
    EXPECT_TRUE(conn->has_pending());

    std::size_t total = got;
    auto drain_result = result_t::success;
    while (total < payload.size() && drain_result == result_t::success) {
        std::size_t chunk = 0;
        const auto want = std::min(chunk_size, payload.size() - total);
        drain_result = conn->read(received.data() + total, want, chunk, 1000);
        total += chunk;
    }
    EXPECT_EQ(drain_result, result_t::success);
    EXPECT_EQ(total, payload.size());
    EXPECT_EQ(std::memcmp(received.data(), payload.data(), payload.size()), 0);

    EXPECT_FALSE(conn->has_pending());

    std::ignore = conn->shutdown(1000);

    if (server_side.joinable()) {
        server_side.join();
    }
    std::ignore = ::close(listener.fd);
}

TEST_F(TlsClientWrapTest, WrapConnectingFdNonBlocking) {
    using state_t = tls::Server::state_t;
    using result_t = tls::Connection::result_t;

    const auto listener = make_loopback_listener();
    ASSERT_GE(listener.fd, 0);

    server_config.socket = listener.fd;
    const auto init_state = server.init(server_config, nullptr);
    ASSERT_TRUE(init_state == state_t::init_complete || init_state == state_t::init_socket);

    // Echo 4 bytes back so the client's non-blocking write/read paths run.
    std::thread server_side([&]() {
        auto server_conn = accept_and_wrap(server, listener.fd);
        if (!server_conn) {
            return;
        }
        if (server_conn->accept(1000) == result_t::success) {
            std::array<std::byte, 4> buf{};
            std::size_t total = 0;
            int timeouts = 0;
            while (total < buf.size() && timeouts <= 10) {
                std::size_t got = 0;
                const auto rres = server_conn->read(buf.data() + total, buf.size() - total, got, 1000);
                if (rres == result_t::success) {
                    total += got;
                } else if (rres == result_t::timeout) {
                    ++timeouts;
                } else {
                    break;
                }
            }
            if (total == buf.size()) {
                std::size_t sent = 0;
                std::ignore = server_conn->write(buf.data(), buf.size(), sent, 1000);
            }
        }
        std::ignore = server_conn->shutdown(1000);
    });

    const int client_fd = connect_loopback_nonblocking(listener.port);
    ASSERT_GE(client_fd, 0);

    // timeout 0 forces purely non-blocking operation.
    tls::Client::config_t nb_config = client_config;
    nb_config.io_timeout_ms = 0;

    ClientTest local_client;
    local_client.init(nb_config);

    auto conn = local_client.wrap_connecting_fd(client_fd, "localhost");
    ASSERT_TRUE(conn);

    const auto drive = drive_client_handshake(*conn, client_fd, 0, 200, 500);
    ASSERT_EQ(drive.result, result_t::success);

    const std::array<std::byte, 4> ping{std::byte{'p'}, std::byte{'i'}, std::byte{'n'}, std::byte{'g'}};
    std::size_t written = 0;
    for (int i = 0; i < 50 && written < ping.size(); ++i) {
        const auto wres = conn->write(ping.data(), ping.size(), written, 0);
        if (wres == result_t::want_write) {
            pollfd pfd{client_fd, POLLOUT, 0};
            std::ignore = ::poll(&pfd, 1, 100);
        } else if (wres == result_t::want_read) {
            pollfd pfd{client_fd, POLLIN, 0};
            std::ignore = ::poll(&pfd, 1, 100);
        } else if (wres != result_t::success) {
            break;
        }
    }
    ASSERT_EQ(written, ping.size());

    std::array<std::byte, 4> echo{};
    std::size_t echoed = 0;
    for (int i = 0; i < 50 && echoed < echo.size(); ++i) {
        std::size_t got = 0;
        const auto rres = conn->read(echo.data() + echoed, echo.size() - echoed, got, 0);
        if (rres == result_t::success) {
            echoed += got;
        } else if (rres == result_t::want_read) {
            pollfd pfd{client_fd, POLLIN, 0};
            std::ignore = ::poll(&pfd, 1, 500);
        } else if (rres == result_t::want_write) {
            pollfd pfd{client_fd, POLLOUT, 0};
            std::ignore = ::poll(&pfd, 1, 500);
        } else {
            break;
        }
    }
    ASSERT_EQ(echoed, echo.size());
    EXPECT_EQ(std::memcmp(echo.data(), ping.data(), ping.size()), 0);

    result_t sr = result_t::timeout;
    for (int i = 0; i < 50 && sr != result_t::closed; ++i) {
        sr = conn->shutdown(0);
        if (sr == result_t::want_read) {
            pollfd pfd{client_fd, POLLIN, 0};
            std::ignore = ::poll(&pfd, 1, 200);
        } else if (sr == result_t::want_write) {
            pollfd pfd{client_fd, POLLOUT, 0};
            std::ignore = ::poll(&pfd, 1, 200);
        }
    }

    if (server_side.joinable()) {
        server_side.join();
    }
    std::ignore = ::close(listener.fd);
}

// A non-blocking connect() hands control back instead of waiting for a peer that has not answered yet.
TEST_F(TlsClientWrapTest, WrapConnectingFdConnectDoesNotWaitForPeer) {
    using result_t = tls::Connection::result_t;

    // Never accepted: the kernel completes the TCP handshake, but nobody answers the ClientHello.
    const auto listener = make_loopback_listener();
    ASSERT_GE(listener.fd, 0);

    const int client_fd = connect_loopback_nonblocking(listener.port);
    ASSERT_GE(client_fd, 0);

    tls::Client::config_t nb_config = client_config;
    nb_config.io_timeout_ms = 0;

    ClientTest local_client;
    local_client.init(nb_config);

    auto conn = local_client.wrap_connecting_fd(client_fd, "localhost");
    ASSERT_TRUE(conn);

    const auto result = conn->connect(0);
    EXPECT_TRUE(result == result_t::want_read || result == result_t::want_write);

    conn.reset();
    std::ignore = ::close(listener.fd);
}

// An empty key-log path drops only the file sink: the network sink still gets the secrets.
TEST_F(TlsClientWrapTest, KeyLoggingWithAnEmptyPathKeepsTheNetworkSink) {
    const KeyLogDir cwd;
    ASSERT_FALSE(cwd.path().empty());
    const auto listener = make_loopback_listener();
    ASSERT_GE(listener.fd, 0);

    KeyLogCapture logs;
    server_config.socket = listener.fd;
    server_config.host = "lo"; // the network sink's interface
    server_config.tls_key_logging = true;
    server_config.tls_key_logging_path = "";
    const auto init_state = server.init(server_config, nullptr);
    ASSERT_TRUE(init_state == tls::Server::state_t::init_complete || init_state == tls::Server::state_t::init_socket);
    const auto server_root = std::filesystem::absolute("server_root_cert.pem").string();
    client_config.verify_locations_file = server_root.c_str();

    {
        const ScopedCwd in_cwd(cwd.path());
        EXPECT_EQ(handshake_once(server, listener.fd, listener.port, client, client_config),
                  tls::Connection::result_t::success);
    }

    EXPECT_TRUE(KeyLogCapture::contains(openssl::log_level_t::error, "network key-log sink stays on"));
    // Sent, or refused by the network where lo carries no IPv6 multicast: either way a send was made.
    EXPECT_TRUE(KeyLogCapture::contains(openssl::log_level_t::info, "TLS handshake keys sent to port") ||
                KeyLogCapture::contains(openssl::log_level_t::error, "key-log send to port"))
        << "the network key-log sink never sent";
    EXPECT_TRUE(std::filesystem::is_empty(cwd.path())) << "a key log was written to the working directory";
    std::ignore = ::close(listener.fd);
}

// Two key-logging servers in one process each keep their own key-log file.
TEST_F(TlsClientWrapTest, KeyLoggingWorksWithTwoServers) {
    const KeyLogDir first_dir;
    const KeyLogDir second_dir;
    ASSERT_FALSE(first_dir.path().empty());
    ASSERT_FALSE(second_dir.path().empty());
    const auto first_listener = make_loopback_listener();
    const auto second_listener = make_loopback_listener();
    ASSERT_GE(first_listener.fd, 0);
    ASSERT_GE(second_listener.fd, 0);

    auto first_config = server_config;
    first_config.socket = first_listener.fd;
    first_config.tls_key_logging = true;
    first_config.tls_key_logging_path = first_dir.path().string();
    auto second_config = first_config;
    second_config.socket = second_listener.fd;
    second_config.tls_key_logging_path = second_dir.path().string();

    tls::Server second_server;
    ASSERT_NE(server.init(first_config, nullptr), tls::Server::state_t::init_needed);
    ASSERT_NE(second_server.init(second_config, nullptr), tls::Server::state_t::init_needed);

    EXPECT_EQ(handshake_once(server, first_listener.fd, first_listener.port, client, client_config),
              tls::Connection::result_t::success);

    EXPECT_NE(read_text(first_dir.path() / "tls_session_keys.log").find("CLIENT_RANDOM"), std::string::npos);
    EXPECT_FALSE(std::filesystem::exists(second_dir.path() / "tls_session_keys.log"));

    second_server.stop();
    second_server.wait_stopped();
    std::ignore = ::close(first_listener.fd);
    std::ignore = ::close(second_listener.fd);
}

namespace {

std::set<std::string> lines_of(const std::filesystem::path& path) {
    std::ifstream in(path);
    std::set<std::string> lines;
    for (std::string line; std::getline(in, line);) {
        lines.insert(line);
    }
    return lines;
}

} // namespace

// A config reload replaces the SSL_CTX while handshakes made from the previous one still run.
// Each SSL_CTX keeps the key-log path it was made with, so every secret lands in a file and
// nothing crashes. A smoke test: the path the old shared member pointed to stayed valid too.
TEST_F(TlsClientWrapTest, KeyLoggingSurvivesConfigReloadsDuringHandshakes) {
    using result_t = tls::Connection::result_t;
    const KeyLogDir first_dir;
    const KeyLogDir client_dir;
    ASSERT_FALSE(first_dir.path().empty());
    ASSERT_FALSE(client_dir.path().empty());
    // Longer than the first, so a path rewritten under a handshake would reallocate.
    const auto second_dir = first_dir.path() / "reloaded-configuration-with-a-longer-key-log-directory";
    ASSERT_TRUE(std::filesystem::create_directory(second_dir));
    const auto listener = make_loopback_listener();
    ASSERT_GE(listener.fd, 0);

    server_config.socket = listener.fd;
    server_config.tls_key_logging = true;
    server_config.tls_key_logging_path = first_dir.path().string();
    auto reloaded_config = server_config;
    reloaded_config.tls_key_logging_path = second_dir.string();
    ASSERT_NE(server.init(server_config, nullptr), tls::Server::state_t::init_needed);
    client_config.tls_key_logging = true;
    client_config.tls_key_logging_path = client_dir.path().string();

    for (int round = 0; round < 20; ++round) {
        const int client_fd = connect_loopback_nonblocking(listener.port);
        ASSERT_GE(client_fd, 0);
        auto server_conn = accept_and_wrap(server, listener.fd);
        ASSERT_TRUE(server_conn);
        auto server_result = result_t::timeout;
        std::thread server_side([&]() {
            server_result = server_conn->accept(1000);
            std::ignore = server_conn->shutdown(1000);
        });
        // Reloads run on while the handshake does.
        std::atomic_bool handshake_done{false};
        std::thread reloader([&]() {
            for (int reload = 0; not handshake_done; ++reload) {
                std::ignore = server.update((reload % 2 == 0) ? reloaded_config : server_config);
            }
        });

        client.init(client_config);
        auto client_conn = client.wrap_connecting_fd(client_fd, "localhost");
        if (client_conn) {
            EXPECT_EQ(drive_client_handshake(*client_conn, client_fd).result, result_t::success);
            std::ignore = client_conn->shutdown(1000);
        } else {
            ADD_FAILURE() << "round " << round << ": no client connection";
            std::ignore = ::close(client_fd);
        }
        server_side.join();
        handshake_done = true;
        reloader.join();
        EXPECT_EQ(server_result, result_t::success) << "round " << round;
    }

    const auto client_lines = lines_of(client_dir.path() / "tls_session_keys_client.log");
    auto server_lines = lines_of(first_dir.path() / "tls_session_keys.log");
    server_lines.merge(lines_of(second_dir / "tls_session_keys.log"));
    EXPECT_EQ(client_lines.size(), 20u);
    for (const auto& line : client_lines) {
        EXPECT_EQ(server_lines.count(line), 1u) << "not in a server key log: " << line;
    }
    std::ignore = ::close(listener.fd);
}
