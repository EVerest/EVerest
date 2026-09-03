// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
//
// Regression gate: every client alias must still resolve, instantiate, and stay on the side of
// event_client_async_policy_v it was written for. That trait decides whether tx() buffers until the
// connection resolves or rejects while the client is down, so a policy silently changing sides
// changes the contract of its alias.

#include <everest/io/can/socket_can.hpp>
#include <everest/io/event/fd_event_client.hpp>
#include <everest/io/mdns/mdns_client.hpp>
#include <everest/io/raw/raw_client.hpp>
#include <everest/io/serial/event_pty.hpp>
#include <everest/io/tcp/tcp_client.hpp>
#include <everest/io/tcp/tcp_socket.hpp>
#include <everest/io/tun_tap/tap_client.hpp>
#include <everest/io/udp/udp_client.hpp>
#include <everest/io/udp/udp_dualstack_server.hpp>
#include <everest/io/udp/udp_server.hpp>
#include <everest/io/udp/udp_unconnected_client.hpp>
#include <everest/io/utilities/event_client_async_policy.hpp>

#ifdef EVEREST_IO_ENABLE_TLS
#include <everest/io/tls/tls_client.hpp>
#endif

#include <atomic>
#include <chrono>
#include <thread>
#include <type_traits>

#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <gtest/gtest.h>

using namespace std::chrono_literals;

// Every alias is pinned three ways: it resolves, it instantiates and its policy keeps its connect
// behavior. is_class_v accepts an incomplete type, so sizeof is what forces the client template to
// instantiate on the policy and run the client's own internal assertions.

static_assert(std::is_class_v<everest::lib::io::can::socket_can>, "socket_can alias must resolve");
static_assert(sizeof(everest::lib::io::can::socket_can) > 0, "fd_event_client must instantiate on socket_can_handler");
static_assert(not everest::lib::io::utilities::event_client_async_policy_v<everest::lib::io::can::socket_can_handler>,
              "socket_can_handler must stay a synchronous policy");

static_assert(std::is_class_v<everest::lib::io::mdns::mdns_client>, "mdns_client alias must resolve");
static_assert(sizeof(everest::lib::io::mdns::mdns_client) > 0, "fd_event_client must instantiate on mdns_socket");
static_assert(not everest::lib::io::utilities::event_client_async_policy_v<everest::lib::io::mdns::mdns_socket>,
              "mdns_socket must stay a synchronous policy");

static_assert(std::is_class_v<everest::lib::io::raw::raw_client>, "raw_client alias must resolve");
static_assert(sizeof(everest::lib::io::raw::raw_client) > 0, "fd_event_client must instantiate on raw_socket");
static_assert(not everest::lib::io::utilities::event_client_async_policy_v<everest::lib::io::raw::raw_socket>,
              "raw_socket must stay a synchronous policy");

// The only alias consumed through a derived class rather than directly.
static_assert(std::is_class_v<everest::lib::io::serial::event_pty_base>, "event_pty_base alias must resolve");
static_assert(sizeof(everest::lib::io::serial::event_pty_base) > 0, "fd_event_client must instantiate on pty_handler");
static_assert(not everest::lib::io::utilities::event_client_async_policy_v<everest::lib::io::serial::pty_handler>,
              "pty_handler must stay a synchronous policy");
static_assert(std::is_base_of_v<everest::lib::io::serial::event_pty_base, everest::lib::io::serial::event_pty>,
              "event_pty must keep deriving from the alias");

static_assert(std::is_class_v<everest::lib::io::tcp::tcp_client>, "tcp_client alias must resolve");
static_assert(sizeof(everest::lib::io::tcp::tcp_client) > 0, "fd_event_client must instantiate on tcp_socket");
static_assert(everest::lib::io::utilities::event_client_async_policy_v<everest::lib::io::tcp::tcp_socket>,
              "tcp_socket must stay an async policy");

static_assert(std::is_class_v<everest::lib::io::tun_tap::tap_client>, "tap_client alias must resolve");
static_assert(sizeof(everest::lib::io::tun_tap::tap_client) > 0, "fd_event_client must instantiate on tap_handler");
static_assert(not everest::lib::io::utilities::event_client_async_policy_v<everest::lib::io::tun_tap::tap_handler>,
              "tap_handler must stay a synchronous policy");

// udp_server.hpp redeclares udp_client identically to udp_client.hpp. Both headers are
// included here, so the redundant typedef stays proven benign.
static_assert(std::is_class_v<everest::lib::io::udp::udp_client>, "udp_client alias must resolve");
static_assert(sizeof(everest::lib::io::udp::udp_client) > 0, "fd_event_client must instantiate on udp_client_socket");
static_assert(everest::lib::io::utilities::event_client_async_policy_v<everest::lib::io::udp::udp_client_socket>,
              "udp_client_socket must stay an async policy");

// The two server aliases answer the source of the last rx(), so their policies reject tx()
// until a datagram arrived. Only the wrapper level is pinned for them.
static_assert(std::is_class_v<everest::lib::io::udp::udp_server>, "udp_server alias must resolve");
static_assert(sizeof(everest::lib::io::udp::udp_server) > 0, "fd_event_client must instantiate on udp_server_socket");
static_assert(not everest::lib::io::utilities::event_client_async_policy_v<everest::lib::io::udp::udp_server_socket>,
              "udp_server_socket must stay a synchronous policy");

static_assert(std::is_class_v<everest::lib::io::udp::udp_dualstack_server>, "udp_dualstack_server alias must resolve");
static_assert(sizeof(everest::lib::io::udp::udp_dualstack_server) > 0,
              "fd_event_client must instantiate on udp_dualstack_server_socket");
static_assert(
    not everest::lib::io::utilities::event_client_async_policy_v<everest::lib::io::udp::udp_dualstack_server_socket>,
    "udp_dualstack_server_socket must stay a synchronous policy");

static_assert(std::is_class_v<everest::lib::io::udp::udp_unconnected_client>,
              "udp_unconnected_client alias must resolve");
static_assert(sizeof(everest::lib::io::udp::udp_unconnected_client) > 0,
              "fd_event_client must instantiate on udp_unconnected_socket");
static_assert(
    not everest::lib::io::utilities::event_client_async_policy_v<everest::lib::io::udp::udp_unconnected_socket>,
    "udp_unconnected_socket must stay a synchronous policy");

// Teeth for the handshake-policy assertions below: the trait must discriminate. tcp_socket is
// hookless, so it must not match, while still satisfying the async setup()/connect() policy.
static_assert(not everest::lib::io::utilities::event_client_handshake_policy_v<everest::lib::io::tcp::tcp_socket>,
              "the handshake policy trait must not match a hookless policy");

// tx_coalescing() appends to a payload possibly mid write: only byte stream policies that trim
// the sent prefix may have it, frame transports and TLS must not.
template <class Client, class = void> struct client_has_tx_coalescing : std::false_type {};
template <class Client>
struct client_has_tx_coalescing<Client, std::void_t<decltype(std::declval<Client&>().tx_coalescing(
                                            std::declval<typename Client::ClientPayloadT const&>(), std::size_t{}))>>
    : std::true_type {};

static_assert(everest::lib::io::utilities::policy_supports_tx_coalescing_v<everest::lib::io::tcp::tcp_socket>,
              "tcp_socket must admit tx_coalescing");
static_assert(client_has_tx_coalescing<everest::lib::io::tcp::tcp_client>::value,
              "tcp_client must expose tx_coalescing()");
static_assert(everest::lib::io::utilities::policy_supports_tx_coalescing_v<everest::lib::io::serial::pty_handler>,
              "pty_handler must admit tx_coalescing");
static_assert(client_has_tx_coalescing<everest::lib::io::serial::event_pty_base>::value,
              "event_pty_base must expose tx_coalescing()");

static_assert(not everest::lib::io::utilities::policy_supports_tx_coalescing_v<everest::lib::io::raw::raw_socket>,
              "raw_socket is a frame transport and must not admit tx_coalescing");
static_assert(not client_has_tx_coalescing<everest::lib::io::raw::raw_client>::value,
              "raw_client must not expose tx_coalescing()");
static_assert(not everest::lib::io::utilities::policy_supports_tx_coalescing_v<everest::lib::io::tun_tap::tap_handler>,
              "tap_handler is a frame transport and must not admit tx_coalescing");
static_assert(not client_has_tx_coalescing<everest::lib::io::tun_tap::tap_client>::value,
              "tap_client must not expose tx_coalescing()");
static_assert(
    not everest::lib::io::utilities::policy_supports_tx_coalescing_v<everest::lib::io::udp::udp_client_socket>,
    "udp_client_socket is a datagram transport and must not admit tx_coalescing");
static_assert(not client_has_tx_coalescing<everest::lib::io::udp::udp_client>::value,
              "udp_client must not expose tx_coalescing()");
static_assert(
    not everest::lib::io::utilities::policy_supports_tx_coalescing_v<everest::lib::io::can::socket_can_handler>,
    "socket_can_handler is a frame transport and must not admit tx_coalescing");
static_assert(not client_has_tx_coalescing<everest::lib::io::can::socket_can>::value,
              "socket_can must not expose tx_coalescing()");

#ifdef EVEREST_IO_ENABLE_TLS
static_assert(std::is_class_v<everest::lib::io::tls::tls_client>, "tls_client alias must resolve");
static_assert(std::is_abstract_v<everest::lib::io::tls::tls_client_interface>,
              "tls_client_interface alias must resolve to the abstract client interface");
static_assert(sizeof(everest::lib::io::tls::tls_client) > 0, "fd_event_client must instantiate on tls_client_socket");
// tls_client_socket must satisfy both policies the generic event client detects, the async
// setup()/connect() legs and the optional handshake phase. The handshake trait is all-or-nothing,
// so a partial surface here is a hard compile error in the client.
static_assert(everest::lib::io::utilities::event_client_async_policy_v<everest::lib::io::tls::tls_client_socket>,
              "tls_client_socket must stay an async policy");
static_assert(everest::lib::io::utilities::event_client_handshake_policy_v<everest::lib::io::tls::tls_client_socket>,
              "tls_client_socket must satisfy the handshake client policy");
static_assert(everest::lib::io::utilities::has_member_get_error_string_v<everest::lib::io::tls::tls_client_socket>,
              "tls_client_socket must expose get_error_string()");
static_assert(
    not everest::lib::io::utilities::policy_supports_tx_coalescing_v<everest::lib::io::tls::tls_client_socket>,
    "tls_client_socket retries a would-block with the identical buffer and must not admit tx_coalescing");
static_assert(not client_has_tx_coalescing<everest::lib::io::tls::tls_client>::value,
              "tls_client must not expose tx_coalescing()");
#endif

TEST(fd_event_client_alias, tcp_client_constructs_hookless_path_unchanged) {
    // A loopback listener gives tcp_socket's async setup()/connect() a real peer.
    int srv = ::socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_GE(srv, 0);
    sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    sa.sin_port = 0;
    ASSERT_EQ(::bind(srv, reinterpret_cast<sockaddr*>(&sa), sizeof(sa)), 0);
    ASSERT_EQ(::listen(srv, 1), 0);
    socklen_t len = sizeof(sa);
    ASSERT_EQ(::getsockname(srv, reinterpret_cast<sockaddr*>(&sa), &len), 0);
    const std::uint16_t port = ntohs(sa.sin_port);

    std::atomic<bool> accepted{false};
    std::thread accept_thread([&]() {
        int c = ::accept(srv, nullptr, nullptr);
        if (c >= 0) {
            accepted = true;
            std::this_thread::sleep_for(200ms);
            ::close(c);
        }
    });

    everest::lib::io::tcp::tcp_client client("127.0.0.1", port, 1000);

    // The ready action fires via the synchronous open() path with no deferral.
    std::atomic<bool> ready{false};
    client.set_on_ready_action([&]() { ready = true; });

    EXPECT_GE(client.get_poll_fd(), 0);

    auto deadline = std::chrono::steady_clock::now() + 5s;
    while (std::chrono::steady_clock::now() < deadline && not ready.load()) {
        client.sync(100ms);
    }

    EXPECT_TRUE(ready.load()) << "hookless tcp_client never reached ready";
    auto const& raw = client.get_raw_handler();
    ASSERT_NE(raw, nullptr);
    EXPECT_GE(raw->get_fd(), 0);

    if (accept_thread.joinable()) {
        accept_thread.join();
    }
    ::close(srv);
}
