// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
//
// The SEQPACKET side driven the way it is meant to be: a listener and its peers on one
// fd_event_handler, a client on its own loop. What is asserted is what SEQPACKET was chosen for
// over datagrams, a connection whose end both sides learn about, and what it keeps from them,
// message boundaries and descriptors.

#include <everest/io/event/fd_event_handler.hpp>
#include <everest/io/socket/socket.hpp>
#include <everest/io/uds/uds_seqpacket_client.hpp>
#include <everest/io/uds/uds_seqpacket_listener.hpp>
#include <everest/io/uds/uds_seqpacket_peer.hpp>
#include <everest/io/uds/uds_seqpacket_socket.hpp>
#include <everest/io/uds/uds_utils.hpp>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <memory>
#include <poll.h>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

#include <gtest/gtest.h>

namespace {

using namespace std::chrono_literals;
namespace event = everest::lib::io::event;
namespace socket_api = everest::lib::io::socket;
using everest::lib::io::uds::send_fd;
using everest::lib::io::uds::uds_payload;
using everest::lib::io::uds::uds_seqpacket_client;
using everest::lib::io::uds::uds_seqpacket_client_socket;
using everest::lib::io::uds::uds_seqpacket_listener;
using everest::lib::io::uds::uds_seqpacket_peer;
using everest::lib::io::uds::uds_seqpacket_peer_socket;

std::string unique_name(std::string const& tag) {
    return "everest_uds_seqpacket_test_" + std::to_string(::getpid()) + "_" + tag;
}

std::string unique_path(std::string const& tag) {
    return "/tmp/" + unique_name(tag) + ".sock";
}

std::string as_string(uds_payload const& payload) {
    return std::string(payload.buffer.begin(), payload.buffer.end());
}

bool path_exists(std::string const& path) {
    struct stat st {};
    return ::lstat(path.c_str(), &st) == 0;
}

// Drives one handler until \p done says so or the budget is spent. Keeps the verdict rather than
// asking again, so a predicate that consumes what it waits for works too.
template <class PredicateT>
bool pump_until(event::fd_event_handler& handler, PredicateT done, std::chrono::milliseconds budget) {
    auto const deadline = std::chrono::steady_clock::now() + budget;
    bool satisfied = done();
    while (not satisfied and std::chrono::steady_clock::now() < deadline) {
        handler.poll(20ms);
        handler.run_actions();
        satisfied = done();
    }
    return satisfied;
}

// A listener with every accepted peer kept alive and echoing, on one handler with the client.
struct echo_server {
    event::fd_event_handler& handler;
    uds_seqpacket_listener listener;
    std::vector<std::unique_ptr<uds_seqpacket_peer>> peers;
    std::atomic<int> accepted{0};
    std::atomic<int> peer_errors{0};
    std::atomic<int> last_peer_error{0};
    std::vector<std::string> received;

    echo_server(event::fd_event_handler& h, std::string const& name, bool is_abstract = true) :
        handler(h), listener(name, is_abstract) {
        listener.set_accept_callback([this](std::unique_ptr<uds_seqpacket_peer> peer) {
            ++accepted;
            peer->set_rx_handler([this](uds_payload const& payload, auto& device) {
                received.push_back(as_string(payload));
                device.tx(payload);
            });
            peer->set_error_handler([this](int code, std::string const&) {
                if (code != 0) {
                    ++peer_errors;
                    last_peer_error = code;
                }
            });
            handler.register_event_handler(peer.get());
            peers.push_back(std::move(peer));
        });
        handler.register_event_handler(&listener);
    }
    ~echo_server() {
        for (auto& peer : peers) {
            handler.unregister_event_handler(peer.get());
        }
        handler.unregister_event_handler(&listener);
    }
};

// A client on the same handler, collecting what comes back.
struct collecting_client {
    event::fd_event_handler& handler;
    uds_seqpacket_client client;
    std::vector<uds_payload> received;
    std::atomic<int> ready_count{0};
    std::atomic<int> first_error{0};

    collecting_client(event::fd_event_handler& h, std::string const& name, bool is_abstract = true) :
        handler(h), client(name, is_abstract) {
        client.set_rx_handler([this](uds_payload const& payload, auto&) { received.push_back(payload); });
        client.set_on_ready_action([this]() { ++ready_count; });
        client.set_error_handler([this](int code, std::string const&) {
            if (code != 0 and first_error.load() == 0) {
                first_error = code;
            }
        });
        handler.register_event_handler(&client);
    }
    ~collecting_client() {
        handler.unregister_event_handler(&client);
    }
};

} // namespace

TEST(uds_seqpacket, listener_hands_out_a_peer_and_messages_keep_their_boundaries) {
    auto const name = unique_name("echo");
    event::fd_event_handler handler;
    echo_server server(handler, name);
    collecting_client client(handler, name);

    ASSERT_TRUE(pump_until(handler, [&]() { return client.ready_count.load() == 1; }, 5s)) << "client never ready";
    ASSERT_TRUE(pump_until(handler, [&]() { return server.accepted.load() == 1; }, 5s)) << "nothing accepted";

    // Two sends must come back as two messages, never as one concatenated stream.
    ASSERT_TRUE(client.client.tx(uds_payload{"first"}));
    ASSERT_TRUE(client.client.tx(uds_payload{"second"}));
    ASSERT_TRUE(pump_until(handler, [&]() { return client.received.size() == 2; }, 5s)) << "echo incomplete";
    EXPECT_EQ(as_string(client.received[0]), "first");
    EXPECT_EQ(as_string(client.received[1]), "second");
    ASSERT_EQ(server.received.size(), 2u);
    EXPECT_EQ(server.received[0], "first");
    EXPECT_EQ(server.received[1], "second");
}

TEST(uds_seqpacket, a_descriptor_travels_over_the_connection_both_ways) {
    auto const name = unique_name("fd");
    event::fd_event_handler handler;
    echo_server server(handler, name);
    collecting_client client(handler, name);
    ASSERT_TRUE(
        pump_until(handler, [&]() { return client.ready_count.load() == 1 and server.accepted.load() == 1; }, 5s));

    int pipe_fds[2] = {-1, -1};
    ASSERT_EQ(::pipe(pipe_fds), 0);

    // The echo sends the payload back with its descriptor, so the client gets its own duplicate.
    ASSERT_TRUE(send_fd(client.client, pipe_fds[0], "read end"));
    ASSERT_TRUE(pump_until(handler, [&]() { return client.received.size() == 1; }, 5s)) << "no echo";
    auto const& echoed = client.received[0];
    EXPECT_EQ(as_string(echoed), "read end");
    ASSERT_TRUE(echoed.has_fds());
    EXPECT_NE(echoed.fd(), pipe_fds[0]);
    ASSERT_EQ(::write(pipe_fds[1], "through", 7), 7);
    char buffer[8] = {};
    EXPECT_EQ(::read(echoed.fd(), buffer, sizeof(buffer)), 7);
    EXPECT_STREQ(buffer, "through");

    ::close(pipe_fds[0]);
    ::close(pipe_fds[1]);
}

TEST(uds_seqpacket, an_empty_message_is_delivered_not_taken_for_eof) {
    auto const name = unique_name("empty");
    event::fd_event_handler handler;
    echo_server server(handler, name);
    collecting_client client(handler, name);
    ASSERT_TRUE(
        pump_until(handler, [&]() { return client.ready_count.load() == 1 and server.accepted.load() == 1; }, 5s));

    ASSERT_TRUE(client.client.tx(uds_payload{}));
    ASSERT_TRUE(client.client.tx(uds_payload{"after the empty one"}));
    ASSERT_TRUE(pump_until(handler, [&]() { return client.received.size() == 2; }, 5s)) << "echo incomplete";
    EXPECT_EQ(client.received[0].size(), 0u);
    EXPECT_EQ(as_string(client.received[1]), "after the empty one");
    // Neither side mistook the zero byte message for the peer closing.
    EXPECT_EQ(server.peer_errors.load(), 0);
    EXPECT_EQ(client.first_error.load(), 0);
}

TEST(uds_seqpacket, the_peer_learns_that_the_client_went_away) {
    auto const name = unique_name("client_gone");
    event::fd_event_handler handler;
    echo_server server(handler, name);
    {
        collecting_client client(handler, name);
        ASSERT_TRUE(pump_until(handler, [&]() { return server.accepted.load() == 1; }, 5s));
    }
    // A datagram server would never hear of this. The connection reports it.
    ASSERT_TRUE(pump_until(handler, [&]() { return server.peer_errors.load() >= 1; }, 5s)) << "peer never told";
    EXPECT_TRUE(server.last_peer_error.load() == ECONNRESET or server.last_peer_error.load() == ENOTCONN)
        << strerror(server.last_peer_error.load());
}

TEST(uds_seqpacket, the_client_learns_that_the_peer_went_away_and_reconnects) {
    auto const name = unique_name("peer_gone");
    event::fd_event_handler handler;
    echo_server server(handler, name);
    collecting_client client(handler, name);
    ASSERT_TRUE(
        pump_until(handler, [&]() { return client.ready_count.load() == 1 and server.accepted.load() == 1; }, 5s));

    // The server drops its side of the connection; the listener stays.
    handler.unregister_event_handler(server.peers.front().get());
    server.peers.clear();

    ASSERT_TRUE(pump_until(handler, [&]() { return client.first_error.load() != 0; }, 5s)) << "client never told";
    EXPECT_TRUE(client.first_error.load() == ECONNRESET or client.first_error.load() == ENOTCONN)
        << strerror(client.first_error.load());

    // A client reconnects, and the listener accepts the new connection.
    client.client.reset();
    ASSERT_TRUE(pump_until(
        handler, [&]() { return client.ready_count.load() == 2 and server.accepted.load() == 2; }, 5s))
        << "no reconnect";
    ASSERT_TRUE(client.client.tx(uds_payload{"back again"}));
    ASSERT_TRUE(pump_until(handler, [&]() { return not client.received.empty(); }, 5s));
    EXPECT_EQ(as_string(client.received.back()), "back again");
}

TEST(uds_seqpacket, a_peer_is_single_use) {
    auto const name = unique_name("single_use");
    event::fd_event_handler handler;
    echo_server server(handler, name);
    collecting_client client(handler, name);
    ASSERT_TRUE(pump_until(handler, [&]() { return server.accepted.load() == 1; }, 5s));

    // The descriptor was taken out of the shared handle on the first open, so a reset, which
    // hands the same argument to a fresh policy, finds nothing and must say so rather than adopt
    // whatever number the kernel has since reused.
    auto& peer = *server.peers.front();
    peer.reset();
    ASSERT_TRUE(pump_until(handler, [&]() { return server.peer_errors.load() >= 1; }, 5s)) << "reset not reported";
    EXPECT_EQ(server.last_peer_error.load(), ENOTCONN);
}

TEST(uds_seqpacket, connecting_to_nothing_is_reported_with_the_errno) {
    event::fd_event_handler handler;
    collecting_client client(handler, unique_name("nobody"));
    ASSERT_TRUE(pump_until(handler, [&]() { return client.first_error.load() != 0; }, 5s)) << "never reported";
    EXPECT_EQ(client.first_error.load(), ECONNREFUSED);
}

TEST(uds_seqpacket, raw_policies_round_trip_without_an_event_loop) {
    auto const name = unique_name("raw");
    auto listener = socket_api::open_uds_seqpacket_server_socket(name, true);

    uds_seqpacket_client_socket client;
    ASSERT_TRUE(client.open(name, true));

    const int accepted = ::accept4(listener, nullptr, nullptr, SOCK_CLOEXEC | SOCK_NONBLOCK);
    ASSERT_GE(accepted, 0);
    uds_seqpacket_peer_socket peer;
    auto handle = std::make_shared<event::unique_fd>(accepted);
    ASSERT_TRUE(peer.open(handle));
    // Taken over: the handle is empty now, and a second open cannot reuse it.
    EXPECT_FALSE(handle->is_fd());
    uds_seqpacket_peer_socket second;
    EXPECT_FALSE(second.open(handle));
    EXPECT_EQ(second.get_error(), ENOTCONN);

    ASSERT_TRUE(client.tx(uds_payload{"ping"}));
    uds_payload received;
    auto const deadline = std::chrono::steady_clock::now() + 2s;
    while (not peer.rx(received) and std::chrono::steady_clock::now() < deadline) {
    }
    EXPECT_EQ(as_string(received), "ping");

    // EOF on the raw policy: rx() fails and names the cause.
    client.close();
    auto const eof_deadline = std::chrono::steady_clock::now() + 2s;
    while (peer.get_error() == 0 and std::chrono::steady_clock::now() < eof_deadline) {
        peer.rx(received);
    }
    EXPECT_EQ(peer.get_error(), ECONNRESET);
}

TEST(uds_seqpacket, listener_removes_its_socket_file_and_refuses_a_foreign_one) {
    auto const path = unique_path("listener_file");
    {
        uds_seqpacket_listener listener(path, false);
        EXPECT_TRUE(path_exists(path));
    }
    EXPECT_FALSE(path_exists(path));

    {
        std::ofstream file(path);
        file << "somebody's data";
    }
    try {
        uds_seqpacket_listener listener(path, false);
        FAIL() << "a regular file must not be replaced by a listener";
    } catch (socket_api::socket_error const& e) {
        EXPECT_EQ(e.error(), EEXIST);
    }
    EXPECT_TRUE(path_exists(path));
    ::unlink(path.c_str());
}

TEST(uds_seqpacket, listener_without_an_accept_callback_closes_what_it_accepts) {
    auto const name = unique_name("no_callback");
    event::fd_event_handler handler;
    uds_seqpacket_listener listener(name);
    handler.register_event_handler(&listener);

    collecting_client client(handler, name);
    // The connection completes, is accepted, and is closed again: the client sees the peer go.
    ASSERT_TRUE(pump_until(handler, [&]() { return client.first_error.load() != 0; }, 5s)) << "never closed";
    EXPECT_TRUE(client.first_error.load() == ECONNRESET or client.first_error.load() == ENOTCONN)
        << strerror(client.first_error.load());
    handler.unregister_event_handler(&listener);
}

TEST(uds_seqpacket, both_ends_know_who_is_on_the_other_end) {
    auto const name = unique_name("peer_creds");
    event::fd_event_handler handler;
    echo_server server(handler, name);
    collecting_client client(handler, name);
    ASSERT_TRUE(
        pump_until(handler, [&]() { return client.ready_count.load() == 1 and server.accepted.load() == 1; }, 5s));

    // Recorded by the kernel at accept and connect time; both ends are this process here.
    auto const accepted_side = server.peers.front()->get_raw_handler()->peer_credentials();
    ASSERT_TRUE(accepted_side.has_value());
    EXPECT_EQ(accepted_side->pid, ::getpid());
    EXPECT_EQ(accepted_side->uid, ::getuid());
    EXPECT_EQ(accepted_side->gid, ::getgid());

    auto const connecting_side = client.client.get_raw_handler()->peer_credentials();
    ASSERT_TRUE(connecting_side.has_value());
    EXPECT_EQ(connecting_side->pid, ::getpid());
}

TEST(uds_seqpacket, a_peer_pidfd_names_the_connecting_process) {
    auto const name = unique_name("peer_pidfd");
    event::fd_event_handler handler;
    echo_server server(handler, name);
    collecting_client client(handler, name);
    ASSERT_TRUE(pump_until(handler, [&]() { return server.accepted.load() == 1; }, 5s));

    auto pidfd = server.peers.front()->get_raw_handler()->peer_pidfd();
    if (not pidfd.is_fd()) {
        GTEST_SKIP() << "SO_PEERPIDFD not supported by this kernel";
    }
    // A pidfd is pollable and readable-when-exited; alive, it is simply a valid descriptor that
    // the kernel ties to exactly this process.
    EXPECT_NE(::fcntl(pidfd, F_GETFD), -1);
    struct pollfd pfd {};
    pfd.fd = pidfd;
    pfd.events = POLLIN;
    // Not exited: nothing to read.
    EXPECT_EQ(::poll(&pfd, 1, 0), 0);
}

TEST(uds_seqpacket, without_a_connection_there_are_no_peer_credentials) {
    uds_seqpacket_client_socket unconnected;
    EXPECT_FALSE(unconnected.peer_credentials().has_value());
    EXPECT_FALSE(unconnected.peer_pidfd().is_fd());
}

TEST(uds_seqpacket, listener_gives_its_socket_file_the_permissions_asked_for) {
    auto const path = unique_path("listener_mode");
    {
        uds_seqpacket_listener listener(path, false, 0600);
        struct stat st {};
        ASSERT_EQ(::lstat(path.c_str(), &st), 0);
        EXPECT_EQ(st.st_mode & 0777, 0600u);
    }
    EXPECT_FALSE(path_exists(path));

    try {
        uds_seqpacket_listener listener(unique_name("listener_abstract_mode"), true, 0600);
        FAIL() << "permissions on an abstract name must be refused";
    } catch (socket_api::socket_error const& e) {
        EXPECT_EQ(e.error(), EINVAL);
    }
}

TEST(uds_seqpacket, two_empty_messages_in_a_row_are_both_delivered) {
    auto const name = unique_name("two_empty");
    event::fd_event_handler handler;
    echo_server server(handler, name);
    collecting_client client(handler, name);
    ASSERT_TRUE(
        pump_until(handler, [&]() { return client.ready_count.load() == 1 and server.accepted.load() == 1; }, 5s));

    // The record queued behind an empty one is empty as well: still not EOF.
    ASSERT_TRUE(client.client.tx(uds_payload{}));
    ASSERT_TRUE(client.client.tx(uds_payload{}));
    ASSERT_TRUE(client.client.tx(uds_payload{"third"}));
    ASSERT_TRUE(pump_until(handler, [&]() { return client.received.size() == 3; }, 5s)) << "echo incomplete";
    EXPECT_EQ(server.peer_errors.load(), 0);
    EXPECT_EQ(client.first_error.load(), 0);
}

TEST(uds_seqpacket, an_fd_only_message_behind_an_empty_one_is_delivered) {
    auto const name = unique_name("empty_then_fd");
    event::fd_event_handler handler;
    echo_server server(handler, name);
    collecting_client client(handler, name);
    ASSERT_TRUE(
        pump_until(handler, [&]() { return client.ready_count.load() == 1 and server.accepted.load() == 1; }, 5s));

    int pipe_fds[2] = {-1, -1};
    ASSERT_EQ(::pipe(pipe_fds), 0);
    // send_fd with default metadata is a zero byte record carrying a descriptor.
    ASSERT_TRUE(client.client.tx(uds_payload{}));
    ASSERT_TRUE(send_fd(client.client, pipe_fds[0]));
    ASSERT_TRUE(pump_until(handler, [&]() { return client.received.size() == 2; }, 5s)) << "echo incomplete";
    EXPECT_TRUE(client.received[1].has_fds());
    EXPECT_EQ(server.peer_errors.load(), 0);
    EXPECT_EQ(client.first_error.load(), 0);

    ::close(pipe_fds[0]);
    ::close(pipe_fds[1]);
}

TEST(uds_seqpacket, a_second_listener_on_a_live_path_does_not_disturb_the_first) {
    auto const path = unique_path("no_phantom_accept");
    event::fd_event_handler handler;
    echo_server server(handler, path, false);

    try {
        uds_seqpacket_listener second(path, false);
        FAIL() << "a live listener must not be replaced";
    } catch (socket_api::socket_error const& e) {
        EXPECT_EQ(e.error(), EADDRINUSE);
    }
    // The probe that found the first listener alive must not have connected to it.
    pump_until(handler, [&]() { return false; }, 200ms);
    EXPECT_EQ(server.accepted.load(), 0);
    EXPECT_EQ(server.peer_errors.load(), 0);
}

TEST(uds_seqpacket, a_peer_made_from_a_blocking_descriptor_is_non_blocking) {
    int pair[2] = {-1, -1};
    ASSERT_EQ(::socketpair(AF_UNIX, SOCK_SEQPACKET, 0, pair), 0); // blocking on purpose
    ASSERT_EQ(::fcntl(pair[0], F_GETFL) & O_NONBLOCK, 0);

    uds_seqpacket_peer_socket peer;
    ASSERT_TRUE(peer.open(std::make_shared<event::unique_fd>(pair[0])));
    // A blocking descriptor on the loop would stall it on a full send buffer.
    EXPECT_NE(::fcntl(peer.get_fd(), F_GETFL) & O_NONBLOCK, 0);
    ::close(pair[1]);
}

TEST(uds_seqpacket, an_empty_name_is_refused_for_listener_and_client) {
    try {
        uds_seqpacket_listener listener("", false);
        FAIL() << "an empty path must not bind a hidden abstract socket";
    } catch (socket_api::socket_error const& e) {
        EXPECT_EQ(e.error(), EINVAL);
    }
    uds_seqpacket_client_socket client;
    EXPECT_FALSE(client.open("", true));
    EXPECT_EQ(client.get_error(), EINVAL);
}
