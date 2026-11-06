// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
//
// End-to-end: a tls_listener and a tls_client on one fd_event_handler, on one thread. Only the
// client's TCP connect runs off-loop, on a detached worker; the listen backlog buffers the SYN so
// the loop accepts it once polling starts. Both TLS handshakes then run on that one loop.

#include "tls_test_common.hpp"

#include <everest/io/event/fd_event_handler.hpp>
#include <everest/io/tcp/tcp_socket.hpp>
#include <everest/io/tls/tls_client.hpp>
#include <everest/io/tls/tls_listener.hpp>
#include <everest/io/tls/tls_server.hpp>
#include <everest/io/tls/tls_server_socket.hpp>
#include <everest/tls/openssl_util.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include <cstdlib>

#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

using namespace std::chrono_literals;

namespace {

namespace io = everest::lib::io;
namespace test = everest::lib::io::test;

using tls_payload = io::tls::tls_client_socket::PayloadT;

/// Exceeds the 4096-byte rx() chunk buffer, so rx() must drain the remainder via has_pending().
constexpr std::size_t kLargePayload = 10000;

std::vector<std::uint8_t> make_large_payload() {
    std::vector<std::uint8_t> payload(kLargePayload);
    for (std::size_t i = 0; i < payload.size(); ++i) {
        payload[i] = static_cast<std::uint8_t>((i * 31 + 7) & 0xFF);
    }
    return payload;
}

// An arbitrary high range; what is under test is that the client binds inside whatever
// range it is given, not the particular numbers.
constexpr io::tcp::SourcePortRange kTestSourcePorts{49152, 65535};

std::uint16_t local_port_of(int fd) {
    sockaddr_in6 addr{};
    socklen_t len = sizeof(addr);
    if (::getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len) != 0) {
        return 0;
    }
    if (addr.sin6_family == AF_INET6) {
        return ntohs(addr.sin6_port);
    }
    return ntohs(reinterpret_cast<sockaddr_in*>(&addr)->sin_port);
}

/// Round-trips one 'ping' through the echo listener, so a test only asserts on its own concern.
/// Returns the client's fd once the echo arrived, -1 on timeout.
int ping_pong(io::event::fd_event_handler& ev, io::tls::tls_client& client) {
    std::atomic<bool> running{true};
    const std::vector<std::uint8_t> ping = {'p', 'i', 'n', 'g'};
    std::vector<std::uint8_t> echo;

    client.set_on_ready_action([&client, &ping]() {
        tls_payload msg(ping.begin(), ping.end());
        client.tx(msg);
    });
    client.set_rx_handler([&echo, &ping, &running](tls_payload const& payload, io::tls::tls_client_interface&) {
        echo.insert(echo.end(), payload.begin(), payload.end());
        if (echo.size() >= ping.size()) {
            running = false;
        }
    });
    ev.register_event_handler(&client);

    test::pump_until(
        ev, [&] { return !running; }, 5s);

    if (echo != ping) {
        return -1;
    }
    auto const& handle = client.get_raw_handler();
    return handle ? handle->get_fd() : -1;
}

} // namespace

TEST(TlsE2E, RoundTripLargePayloadSingleLoop) {
    io::event::fd_event_handler ev;
    auto rig = test::make_echo_listener(ev);
    const auto port = rig.port();
    ASSERT_GT(port, 0u) << "listener bound to port 0 unexpectedly";
    ASSERT_TRUE(rig.registered);

    const auto expected = make_large_payload();

    io::tls::tls_client client(test::client_test_config(), std::string("127.0.0.1"), port, 2000);

    std::atomic<bool> running{true};
    std::vector<std::uint8_t> echo;
    echo.reserve(kLargePayload);

    client.set_on_ready_action([&client, &expected]() {
        tls_payload msg(expected.begin(), expected.end());
        client.tx(msg);
    });
    client.set_rx_handler([&echo, &running](tls_payload const& payload, io::tls::tls_client_interface&) {
        echo.insert(echo.end(), payload.begin(), payload.end());
        if (echo.size() >= kLargePayload) {
            running = false;
        }
    });
    ev.register_event_handler(&client);

    test::pump_until(
        ev, [&] { return !running; }, 5s);

    ASSERT_EQ(echo.size(), kLargePayload) << "round-trip did not complete within 5 seconds (drain or echo failed)";
    EXPECT_EQ(echo, expected) << "echoed payload differs from sent payload";
}

// The server leaf cert carries a DNS:localhost SAN, so RFC-6125 verification passes even though the
// TCP connect target is the 127.0.0.1 literal: SNI is the matched name, not the connect target.
TEST(TlsE2E, HostnameVerificationSucceeds) {
    io::event::fd_event_handler ev;
    auto rig = test::make_echo_listener(ev);
    const auto port = rig.port();
    ASSERT_GT(port, 0u) << "listener bound to port 0 unexpectedly";
    ASSERT_TRUE(rig.registered);

    auto cfg = test::client_test_config(2000, "localhost");
    cfg.tls.verify_subject_name = true;

    io::tls::tls_client client(cfg, std::string("127.0.0.1"), port, 2000);

    std::atomic<bool> running{true};
    std::atomic<bool> got_echo{false};
    const std::vector<std::uint8_t> ping = {'p', 'i', 'n', 'g'};
    std::vector<std::uint8_t> echo;

    client.set_on_ready_action([&client, &ping]() {
        tls_payload msg(ping.begin(), ping.end());
        client.tx(msg);
    });
    client.set_rx_handler(
        [&echo, &ping, &got_echo, &running](tls_payload const& payload, io::tls::tls_client_interface&) {
            echo.insert(echo.end(), payload.begin(), payload.end());
            if (echo.size() >= ping.size()) {
                got_echo = true;
                running = false;
            }
        });
    ev.register_event_handler(&client);

    test::pump_until(
        ev, [&] { return !running; }, 5s);

    EXPECT_TRUE(got_echo) << "round-trip did not complete with verify_subject_name + correct SNI within 5 seconds";
    EXPECT_EQ(echo, ping) << "echoed payload differs from sent payload";
}

// host_for_sni matches no SAN on the server leaf cert, so verification must fail and report.
TEST(TlsE2E, WrongHostnameVerificationFails) {
    io::event::fd_event_handler ev;
    auto rig = test::make_echo_listener(ev);
    const auto port = rig.port();
    ASSERT_GT(port, 0u) << "listener bound to port 0 unexpectedly";
    ASSERT_TRUE(rig.registered);

    auto cfg = test::client_test_config(2000, "wrong.example");
    cfg.tls.verify_subject_name = true;

    io::tls::tls_client client(cfg, std::string("127.0.0.1"), port, 2000);

    std::atomic<bool> running{true};
    std::atomic<bool> got_error{false};
    std::atomic<bool> got_echo{false};
    const std::vector<std::uint8_t> ping = {'p', 'i', 'n', 'g'};
    // Written on the loop thread, read on the same thread below, so no atomic is needed.
    std::string error_text;

    client.set_error_handler([&got_error, &running, &error_text](int err, std::string const& msg) {
        if (err != 0) {
            got_error = true;
            error_text = msg;
            running = false;
        }
    });
    client.set_on_ready_action([&client, &ping]() {
        // Should never fire: the handshake fails verification before ready.
        tls_payload msg(ping.begin(), ping.end());
        client.tx(msg);
    });
    client.set_rx_handler([&got_echo, &running](tls_payload const& /*payload*/, io::tls::tls_client_interface&) {
        got_echo = true;
        running = false;
    });
    ev.register_event_handler(&client);

    // Short deadline: a mismatch must surface promptly rather than hang.
    test::pump_until(
        ev, [&] { return !running; }, 3s);

    EXPECT_FALSE(got_echo) << "round-trip completed despite a hostname mismatch";
    EXPECT_TRUE(got_error) << "client error handler did not fire on a hostname mismatch within 3 seconds";
    EXPECT_FALSE(error_text.empty()) << "error handler received an empty string instead of the OpenSSL error text";
}

// The client's rx fails inside the connection-fd dispatch lambda, which must NOT remove its own fd
// synchronously from that pass: it would erase the executing std::function and resize the pollfds
// mid-iteration. The teardown is deferred to a queued action.
TEST(TlsE2E, ServerCloseTearsDownClientWithoutCrash) {
    io::event::fd_event_handler ev;
    auto rig = test::make_echo_listener(ev);
    const auto port = rig.port();
    ASSERT_GT(port, 0u) << "listener bound to port 0 unexpectedly";
    ASSERT_TRUE(rig.registered);

    io::tls::tls_client client(test::client_test_config(), std::string("127.0.0.1"), port, 2000);

    std::atomic<bool> got_error{false};
    std::atomic<bool> got_echo{false};
    const std::vector<std::uint8_t> ping = {'p', 'i', 'n', 'g'};

    client.set_on_ready_action([&client, &ping]() {
        tls_payload msg(ping.begin(), ping.end());
        client.tx(msg);
    });
    client.set_rx_handler(
        [&got_echo](tls_payload const& /*payload*/, io::tls::tls_client_interface&) { got_echo = true; });
    client.set_error_handler([&got_error](int err, std::string const& /*msg*/) {
        if (err != 0) {
            got_error = true;
        }
    });
    ev.register_event_handler(&client);

    bool dropped = false;
    test::pump_until(
        ev,
        [&] {
            if (got_echo && not dropped) {
                // Closes the peer fd, so the client rx fails and routes through fail().
                ev.unregister_event_handler(rig.server_conn.get());
                rig.server_conn.reset();
                dropped = true;
            }
            return got_error.load();
        },
        5s);

    EXPECT_TRUE(got_echo) << "round-trip did not complete before the server was dropped";
    EXPECT_TRUE(got_error) << "client error handler did not fire after the server closed within 5 seconds";

    // No crash here is the assertion.
    for (int i = 0; i < 5; ++i) {
        ev.poll(10ms);
        ev.run_actions();
    }
}

// The accept callback runs before the accepted connection's handshake, so a payload queued there
// consumes its one-shot tx-notify while the fd is monitored for the handshake alone. Without a
// re-notify on handshake completion the payload never leaves the queue.
TEST(TlsE2E, server_tx_queued_before_accept_handshake_is_flushed) {
    // Declared first so it outlives every endpoint, whose destructor unregisters from it.
    io::event::fd_event_handler ev;

    io::tls::tls_listener listener(test::listener_test_config());
    const auto port = listener.listen_port();
    ASSERT_GT(port, 0u) << "listener bound to port 0 unexpectedly";

    const std::vector<std::uint8_t> greeting = {'h', 'e', 'l', 'l', 'o'};

    std::unique_ptr<io::tls::tls_server> server_conn;
    listener.set_accept_callback(
        [&](std::unique_ptr<io::tls::tls_server> srv, std::string /*ip*/, std::uint16_t /*peer_port*/) {
            // Queued before the accept handshake has even started.
            EXPECT_TRUE(srv->tx(greeting));
            ev.register_event_handler(srv.get());
            server_conn = std::move(srv);
        });
    ASSERT_TRUE(ev.register_event_handler(&listener));

    io::tls::tls_client client(test::client_test_config(), std::string("127.0.0.1"), port, 2000);

    std::vector<std::uint8_t> received;
    client.set_rx_handler([&received](tls_payload const& payload, io::tls::tls_client_interface&) {
        received.insert(received.end(), payload.begin(), payload.end());
    });
    ASSERT_TRUE(ev.register_event_handler(&client));

    test::pump_until(
        ev, [&] { return received.size() >= greeting.size(); }, 5s);

    ASSERT_EQ(received.size(), greeting.size())
        << "payload queued before the accept handshake was never flushed within 5 seconds";
    EXPECT_EQ(received, greeting) << "delivered payload differs from the queued one";
}

// Two distinct buffering windows. Before the transport is connected, the generic client accepts for
// an async connect policy until an attempt has actually failed. Once connected, the tx-notify fires
// while the fd is monitored for the handshake alone, so the completion path must re-arm write.
TEST(TlsE2E, tx_queued_before_handshake_is_flushed_after_ready) {
    io::event::fd_event_handler ev;
    auto rig = test::make_echo_listener(ev);
    const auto port = rig.port();
    ASSERT_GT(port, 0u) << "listener bound to port 0 unexpectedly";
    ASSERT_TRUE(rig.registered);

    io::tls::tls_client client(test::client_test_config(), std::string("127.0.0.1"), port, 2000);

    std::atomic<bool> running{true};
    const std::vector<std::uint8_t> ping = {'p', 'i', 'n', 'g'};
    const std::vector<std::uint8_t> pong = {'p', 'o', 'n', 'g'};
    std::vector<std::uint8_t> echo;

    client.set_rx_handler([&echo, &ping, &pong, &running](tls_payload const& payload, io::tls::tls_client_interface&) {
        echo.insert(echo.end(), payload.begin(), payload.end());
        if (echo.size() >= ping.size() + pong.size()) {
            running = false;
        }
    });
    ASSERT_TRUE(ev.register_event_handler(&client));

    // Before the connect: no connection fd exists yet, so this can only be buffered.
    tls_payload first(ping.begin(), ping.end());
    ASSERT_TRUE(client.tx(first)) << "tx() rejected a payload queued before the connect completed";

    // A cleared error state means the connect result was processed. The handshake cannot be done
    // yet: its first step only sends the ClientHello, and the server shares this loop.
    ASSERT_TRUE(test::pump_until(
        ev, [&] { return client.get_raw_handler() != nullptr && !client.on_error(); }, 5s))
        << "the TCP connect never completed";
    ASSERT_NE(client.get_raw_handler(), nullptr);
    ASSERT_FALSE(client.get_raw_handler()->handshake_complete())
        << "the handshake already completed, so this is no longer the queue-across-the-handshake window";

    // During the handshake: a second window, distinct from the one above.
    tls_payload second(pong.begin(), pong.end());
    ASSERT_TRUE(client.tx(second)) << "tx() rejected a payload queued during the handshake";

    test::pump_until(
        ev, [&] { return !running; }, 5s);

    std::vector<std::uint8_t> expected = ping;
    expected.insert(expected.end(), pong.begin(), pong.end());
    ASSERT_EQ(echo.size(), expected.size()) << "payloads queued across the handshake were not both flushed";
    EXPECT_EQ(echo, expected) << "echoed payloads differ from the sent ones, or arrived out of order";
}

// Observed through is_registered(), because unregister_event_handler() returns true either way.
// The destructor drops the registration too, so this is the explicit path, not the only one.
TEST(TlsE2E, UnregisterClientRemovesPollFd) {
    io::event::fd_event_handler ev;
    auto rig = test::make_echo_listener(ev);
    const auto port = rig.port();
    ASSERT_GT(port, 0u) << "listener bound to port 0 unexpectedly";
    ASSERT_TRUE(rig.registered);

    int poll_fd = -1;
    {
        io::tls::tls_client client(test::client_test_config(), std::string("127.0.0.1"), port, 2000);

        std::atomic<bool> ready{false};
        client.set_on_ready_action([&ready]() { ready = true; });
        ASSERT_TRUE(ev.register_event_handler(&client));

        test::pump_until(
            ev, [&] { return ready.load(); }, 5s);
        ASSERT_TRUE(ready) << "client handshake did not complete within 5 seconds";

        poll_fd = client.get_poll_fd();
        ASSERT_GE(poll_fd, 0);
        EXPECT_TRUE(ev.unregister_event_handler(&client));
    } // client destroyed here, while ev outlives it.

    for (int i = 0; i < 5; ++i) {
        ev.poll(10ms);
        ev.run_actions();
    }

    EXPECT_FALSE(ev.is_registered(poll_fd))
        << "the client's nested poll fd is still in the handler map after unregister";
}

// libtls reports a peer close as result_t::closed whether or not the session was shut down, so the
// discriminator is last_error(), empty only on a graceful close and surfaced as the error text.
TEST(TlsE2E, client_teardown_closes_the_tls_session) {
    io::event::fd_event_handler ev;
    auto rig = test::make_echo_listener(ev);
    const auto port = rig.port();
    ASSERT_GT(port, 0u) << "listener bound to port 0 unexpectedly";
    ASSERT_TRUE(rig.registered);

    std::atomic<bool> server_closed{false};
    std::string server_error_text{"unset"};

    const std::vector<std::uint8_t> ping = {'p', 'i', 'n', 'g'};
    {
        io::tls::tls_client client(test::client_test_config(), std::string("127.0.0.1"), port, 2000);

        std::atomic<bool> echoed{false};
        client.set_rx_handler([&echoed](tls_payload const&, io::tls::tls_client_interface&) { echoed = true; });
        ASSERT_TRUE(ev.register_event_handler(&client));

        // The accepted endpoint only exists once the listener has run its accept callback.
        ASSERT_TRUE(test::pump_until(
            ev, [&] { return rig.server_conn != nullptr; }, 5s))
            << "the listener never accepted a connection";
        rig.server_conn->set_error_handler([&](int, std::string const& text) {
            server_error_text = text;
            server_closed = true;
        });

        // Proves both sides finished the handshake, so the close below is not an aborted negotiation.
        tls_payload payload(ping.begin(), ping.end());
        ASSERT_TRUE(client.tx(payload));
        ASSERT_TRUE(test::pump_until(
            ev, [&] { return echoed.load(); }, 5s))
            << "the round trip never completed";
    } // client destroyed here, while ev and the listener outlive it.

    ASSERT_TRUE(test::pump_until(
        ev, [&] { return server_closed.load(); }, 5s))
        << "the server never observed the client going away";
    EXPECT_EQ(server_error_text, std::string{})
        << "the client dropped the socket without closing the TLS session, so the peer cannot tell "
           "this from a truncated stream: "
        << server_error_text;
}

// The optional device and source port range reach tcp_socket through the variadic constructor.
TEST(TlsE2E, DeviceAndSourcePortRangeAreForwardedToTheTcpSocket) {
    io::event::fd_event_handler ev;
    auto rig = test::make_echo_listener(ev);
    const auto port = rig.port();
    ASSERT_GT(port, 0u) << "listener bound to port 0 unexpectedly";
    ASSERT_TRUE(rig.registered);

    io::tls::tls_client client(test::client_test_config(), std::string("127.0.0.1"), port, 2000, std::string("lo"),
                               std::optional<io::tcp::SourcePortRange>{kTestSourcePorts});

    const int fd = ping_pong(ev, client);
    ASSERT_GE(fd, 0) << "round-trip over a device-bound, port-restricted client failed within 5 seconds";

    const auto source_port = local_port_of(fd);
    EXPECT_GE(source_port, kTestSourcePorts.min);
    EXPECT_LE(source_port, kTestSourcePorts.max);
}

namespace {

// A fresh directory per test run, removed with everything in it.
class unique_temp_dir {
public:
    unique_temp_dir() {
        std::string tmpl = (std::filesystem::temp_directory_path() / "everest_io_tls_keylog_XXXXXX").string();
        if (::mkdtemp(tmpl.data()) != nullptr) {
            m_path = tmpl;
        }
    }
    ~unique_temp_dir() {
        if (not m_path.empty()) {
            std::error_code ec;
            std::filesystem::remove_all(m_path, ec);
        }
    }
    unique_temp_dir(unique_temp_dir const&) = delete;
    unique_temp_dir& operator=(unique_temp_dir const&) = delete;

    std::filesystem::path const& path() const {
        return m_path;
    }

private:
    std::filesystem::path m_path;
};

std::string absolute(char const* file) {
    return std::filesystem::absolute(file).string();
}

// One key-logging round trip into @p log_dir, run from @p cwd when it is set. Returns the client
// fd, -1 on failure.
int ping_pong_with_key_log(std::string const& log_dir, std::filesystem::path const& cwd = {}) {
    auto lcfg = test::listener_test_config();
    auto& chain = lcfg.tls.chains.front();
    chain.certificate_chain_file = absolute(chain.certificate_chain_file);
    chain.private_key_file = absolute(chain.private_key_file);
    chain.trust_anchor_file = absolute(chain.trust_anchor_file);
    for (auto& ocsp : chain.ocsp_response_files) {
        ocsp = absolute(ocsp);
    }
    auto cfg = test::client_test_config();
    cfg.tls.verify_locations_file = absolute(cfg.tls.verify_locations_file);
    cfg.tls.tls_key_logging = true;
    cfg.tls.tls_key_logging_path = log_dir;

    // Restored on every exit: later tests find the test PKI relative to the working directory.
    struct cwd_guard {
        std::filesystem::path previous = std::filesystem::current_path();
        ~cwd_guard() {
            std::error_code ec;
            std::filesystem::current_path(previous, ec);
        }
    } const guard;
    if (not cwd.empty()) {
        std::filesystem::current_path(cwd);
    }
    io::event::fd_event_handler ev;
    auto rig = test::make_echo_listener(ev, lcfg);
    if (rig.port() == 0 or not rig.registered) {
        return -1;
    }
    io::tls::tls_client client(cfg, std::string("127.0.0.1"), rig.port(), 2000);
    return ping_pong(ev, client);
}

std::string read_file(std::filesystem::path const& path) {
    std::ifstream in(path);
    std::stringstream content;
    content << in.rdbuf();
    return content.str();
}

std::mutex captured_logs_mutex;
std::vector<std::pair<openssl::log_level_t, std::string>> captured_logs;

void capture_log(openssl::log_level_t level, std::string const& str) {
    std::lock_guard const lock(captured_logs_mutex);
    captured_logs.emplace_back(level, str);
}

// Captures the TLS library's log output while alive.
class log_capture {
public:
    log_capture() : m_previous(openssl::set_log_handler(&capture_log)) {
        std::lock_guard const lock(captured_logs_mutex);
        captured_logs.clear();
    }
    ~log_capture() {
        openssl::set_log_handler(m_previous);
    }
    log_capture(log_capture const&) = delete;
    log_capture& operator=(log_capture const&) = delete;

    static int count(openssl::log_level_t level, std::string const& needle) {
        std::lock_guard const lock(captured_logs_mutex);
        int n = 0;
        for (auto const& [entry_level, entry] : captured_logs) {
            if (entry_level == level and entry.find(needle) != std::string::npos) {
                ++n;
            }
        }
        return n;
    }

private:
    openssl::log_handler_t m_previous;
};

} // namespace

// tls_key_logging writes the handshake secrets in SSLKEYLOGFILE format, readable by the owner only.
TEST(TlsE2E, KeyLoggingWritesTheSessionKeys) {
    unique_temp_dir const log_dir;
    ASSERT_FALSE(log_dir.path().empty()) << "mkdtemp failed";

    ASSERT_GE(ping_pong_with_key_log(log_dir.path().string()), 0)
        << "round-trip with key logging enabled failed within 5 seconds";

    auto const log_file = log_dir.path() / "tls_session_keys_client.log";
    ASSERT_TRUE(std::filesystem::exists(log_file)) << "no key log file was written";
    auto const content = read_file(log_file);
    EXPECT_NE(content.find("CLIENT_RANDOM"), std::string::npos) << "key log holds no CLIENT_RANDOM line: " << content;

    struct stat st {};
    ASSERT_EQ(::stat(log_file.c_str(), &st), 0);
    EXPECT_EQ(st.st_mode & 0777, 0600) << "key log mode is " << std::oct << (st.st_mode & 0777);
}

// A symlink in place of the key log is not followed, so the secrets cannot be redirected.
TEST(TlsE2E, KeyLoggingRefusesASymlinkedLogFile) {
    unique_temp_dir const log_dir;
    ASSERT_FALSE(log_dir.path().empty()) << "mkdtemp failed";
    auto const target = log_dir.path() / "redirected.log";
    { std::ofstream(target).flush(); }
    std::filesystem::create_symlink(target, log_dir.path() / "tls_session_keys_client.log");

    ASSERT_GE(ping_pong_with_key_log(log_dir.path().string()), 0)
        << "round-trip with key logging enabled failed within 5 seconds";

    EXPECT_EQ(read_file(target), std::string{}) << "key log was written through the symlink";
}

// A file left by an older build keeps no wider mode than the one the sink creates.
TEST(TlsE2E, KeyLoggingTightensAnOwnExistingLogFile) {
    unique_temp_dir const log_dir;
    ASSERT_FALSE(log_dir.path().empty()) << "mkdtemp failed";
    auto const log_file = log_dir.path() / "tls_session_keys_client.log";
    { std::ofstream(log_file).flush(); }
    ASSERT_EQ(::chmod(log_file.c_str(), 0644), 0);

    ASSERT_GE(ping_pong_with_key_log(log_dir.path().string()), 0)
        << "round-trip with key logging enabled failed within 5 seconds";

    struct stat st {};
    ASSERT_EQ(::stat(log_file.c_str(), &st), 0);
    EXPECT_EQ(st.st_mode & 0777, 0600) << "key log mode is " << std::oct << (st.st_mode & 0777);
    EXPECT_NE(read_file(log_file).find("CLIENT_RANDOM"), std::string::npos);
}

// The warning names each file once per process, not once per reconnect.
TEST(TlsE2E, KeyLoggingWarnsOncePerFile) {
    unique_temp_dir const log_dir;
    ASSERT_FALSE(log_dir.path().empty()) << "mkdtemp failed";
    log_capture const logs;

    ASSERT_GE(ping_pong_with_key_log(log_dir.path().string()), 0);
    ASSERT_GE(ping_pong_with_key_log(log_dir.path().string()), 0);

    auto const log_file = (log_dir.path() / "tls_session_keys_client.log").string();
    EXPECT_EQ(log_capture::count(openssl::log_level_t::warning, "TLS key logging enabled: session secrets are "
                                                                "written to " +
                                                                    log_file),
              1);
}

// A refused key log is reported once per process, not once per secret.
TEST(TlsE2E, KeyLoggingReportsAWriteFailureOnce) {
    unique_temp_dir const log_dir;
    ASSERT_FALSE(log_dir.path().empty()) << "mkdtemp failed";
    auto const log_file = log_dir.path() / "tls_session_keys_client.log";
    std::filesystem::create_symlink(log_dir.path() / "redirected.log", log_file);
    log_capture const logs;

    ASSERT_GE(ping_pong_with_key_log(log_dir.path().string()), 0);
    ASSERT_GE(ping_pong_with_key_log(log_dir.path().string()), 0);

    EXPECT_EQ(log_capture::count(openssl::log_level_t::error, log_file.string()), 1);
    EXPECT_EQ(log_capture::count(openssl::log_level_t::error, "further failures not logged"), 1);
}

// A FIFO in place of the key log is refused, so the secrets never reach whoever reads it.
TEST(TlsE2E, KeyLoggingRefusesAFifoWithAReader) {
    unique_temp_dir const log_dir;
    ASSERT_FALSE(log_dir.path().empty()) << "mkdtemp failed";
    auto const log_file = log_dir.path() / "tls_session_keys_client.log";
    ASSERT_EQ(::mkfifo(log_file.c_str(), 0600), 0);
    int const reader = ::open(log_file.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    ASSERT_GE(reader, 0);
    log_capture const logs;

    EXPECT_GE(ping_pong_with_key_log(log_dir.path().string()), 0) << "a refused key log must not break the connection";

    char buffer[256];
    EXPECT_LE(::read(reader, buffer, sizeof(buffer)), 0) << "key log lines were written into the FIFO";
    ::close(reader);
    EXPECT_EQ(log_capture::count(openssl::log_level_t::error, log_file.string() + " not written: not a regular file"),
              1);
}

// With nobody reading the FIFO, opening the key log must not block the handshake.
TEST(TlsE2E, KeyLoggingDoesNotBlockOnAFifoWithoutAReader) {
    unique_temp_dir const log_dir;
    ASSERT_FALSE(log_dir.path().empty()) << "mkdtemp failed";
    auto const log_file = log_dir.path() / "tls_session_keys_client.log";
    ASSERT_EQ(::mkfifo(log_file.c_str(), 0600), 0);
    log_capture const logs;

    EXPECT_GE(ping_pong_with_key_log(log_dir.path().string()), 0) << "a refused key log must not break the connection";

    EXPECT_EQ(log_capture::count(openssl::log_level_t::error, log_file.string() + " not written"), 1);
}

// An empty path is a configuration error: nothing is logged, in particular not to the cwd.
TEST(TlsE2E, KeyLoggingWithAnEmptyPathWritesNothing) {
    unique_temp_dir const cwd;
    ASSERT_FALSE(cwd.path().empty()) << "mkdtemp failed";
    ASSERT_GE(ping_pong_with_key_log(std::string{}, cwd.path()), 0)
        << "an unusable key log path must not break the connection";
    EXPECT_TRUE(std::filesystem::is_empty(cwd.path())) << "a key log was written to the working directory";
}
