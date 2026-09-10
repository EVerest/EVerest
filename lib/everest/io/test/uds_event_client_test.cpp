// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
//
// The uds policies driven through event::fd_event_client. What is asserted here is the contract
// between the two: a failure the policy can name reaches the error handler, and a condition that
// is not a failure does not. A policy that returns false with no errno leaves the client retrying
// the same payload on a socket that is always writable, which is a spin nobody observes.

#include <everest/io/uds/uds_client.hpp>
#include <everest/io/uds/uds_server.hpp>
#include <everest/io/uds/uds_socket.hpp>
#include <everest/io/uds/uds_utils.hpp>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <dirent.h>
#include <fcntl.h>
#include <memory>
#include <optional>
#include <string>
#include <unistd.h>

#include <gtest/gtest.h>

namespace {

using namespace std::chrono_literals;
using everest::lib::io::uds::send_fd;
using everest::lib::io::uds::uds_client;
using everest::lib::io::uds::uds_client_socket;
using everest::lib::io::uds::uds_payload;
using everest::lib::io::uds::uds_server;
using everest::lib::io::uds::uds_server_socket;

std::string unique_name(std::string const& tag) {
    return "everest_uds_evt_test_" + std::to_string(::getpid()) + "_" + tag;
}

// Drives the client until \p done says so or the deadline passes. Returns done's verdict.
template <class ClientT, class PredicateT>
bool drive_until(ClientT& client, PredicateT done, std::chrono::milliseconds timeout) {
    auto const deadline = std::chrono::steady_clock::now() + timeout;
    // The verdict is kept rather than asked again: a predicate that consumes what it waits for,
    // such as an rx(), answers true exactly once.
    bool result = done();
    while (not result and std::chrono::steady_clock::now() < deadline) {
        client.sync(10ms);
        result = done();
    }
    return result;
}

// Descriptors open in this process, minus the one the directory listing itself holds.
int open_descriptor_count() {
    int count = 0;
    if (DIR* dir = ::opendir("/proc/self/fd")) {
        while (::readdir(dir) != nullptr) {
            ++count;
        }
        ::closedir(dir);
        count -= 3; // ".", ".." and the directory descriptor
    }
    return count;
}

} // namespace

TEST(uds_event_client, client_reports_a_vanished_server_instead_of_spinning) {
    auto const server_name = unique_name("vanished");
    auto server = std::make_unique<uds_server_socket>();
    ASSERT_TRUE(server->open(server_name, true));

    uds_client client(server_name, true);

    std::atomic<int> first_error{0};
    client.set_error_handler([&](int code, std::string const&) {
        if (code != 0 and first_error.load() == 0) {
            first_error = code;
        }
    });
    std::atomic<bool> ready{false};
    client.set_on_ready_action([&]() { ready = true; });
    ASSERT_TRUE(drive_until(client, [&]() { return ready.load(); }, 5s)) << "client never came up";

    server.reset();
    ASSERT_TRUE(client.tx(uds_payload{"into the void"}));

    // The send fails with ECONNREFUSED and SO_ERROR stays 0. Only the recorded errno turns that
    // into a report; without it this loop runs out its deadline with the payload still queued.
    ASSERT_TRUE(drive_until(
        client, [&]() { return first_error.load() != 0; }, 5s))
        << "the vanished server was never reported";
    EXPECT_EQ(first_error.load(), ECONNREFUSED);
}

TEST(uds_event_client, server_survives_a_client_that_left_before_the_reply) {
    auto const server_name = unique_name("survives");
    auto const client_name = unique_name("survives_client");

    uds_server server(server_name, true);

    std::atomic<int> errors{0};
    server.set_error_handler([&](int code, std::string const&) {
        if (code != 0) {
            ++errors;
        }
    });
    std::atomic<int> received{0};
    server.set_rx_handler([&](uds_payload const&, auto&) { ++received; });
    std::atomic<bool> ready{false};
    server.set_on_ready_action([&]() { ready = true; });
    ASSERT_TRUE(drive_until(server, [&]() { return ready.load(); }, 5s)) << "server never came up";

    {
        uds_client_socket client;
        ASSERT_TRUE(client.open(server_name, true, client_name, true));
        ASSERT_TRUE(client.tx(uds_payload{"question"}));
        ASSERT_TRUE(drive_until(server, [&]() { return received.load() == 1; }, 5s));
    }

    // The reply cannot be delivered. That is the client's loss, not a server failure.
    ASSERT_TRUE(server.tx(uds_payload{"answer"}));
    drive_until(server, [&]() { return false; }, 200ms);
    EXPECT_EQ(errors.load(), 0);

    // And the server still serves the next client.
    uds_client_socket next_client;
    ASSERT_TRUE(next_client.open(server_name, true));
    ASSERT_TRUE(next_client.tx(uds_payload{"still there?"}));
    EXPECT_TRUE(drive_until(server, [&]() { return received.load() == 2; }, 5s));
}

TEST(uds_event_client, server_tx_before_any_client_is_dropped_and_the_server_keeps_serving) {
    auto const server_name = unique_name("premature");
    uds_server server(server_name, true);

    std::atomic<int> errors{0};
    server.set_error_handler([&](int code, std::string const&) {
        if (code != 0) {
            ++errors;
        }
    });
    std::atomic<int> received{0};
    server.set_rx_handler([&](uds_payload const&, auto&) { ++received; });
    std::atomic<bool> ready{false};
    server.set_on_ready_action([&]() { ready = true; });
    ASSERT_TRUE(drive_until(server, [&]() { return ready.load(); }, 5s)) << "server never came up";

    // Nobody to answer yet. Failing the server for it would close its socket on every client.
    ASSERT_TRUE(server.tx(uds_payload{"to whom?"}));
    drive_until(server, [&]() { return false; }, 200ms);
    EXPECT_EQ(errors.load(), 0);

    uds_client_socket client;
    ASSERT_TRUE(client.open(server_name, true));
    ASSERT_TRUE(client.tx(uds_payload{"first client"}));
    EXPECT_TRUE(drive_until(server, [&]() { return received.load() == 1; }, 5s));
}

TEST(uds_event_client, server_without_an_rx_handler_leaks_no_descriptors) {
    auto const server_name = unique_name("fd_leak");
    int pipe_fds[2] = {-1, -1};
    ASSERT_EQ(::pipe(pipe_fds), 0);
    constexpr int messages = 32;

    const int fds_before = open_descriptor_count();
    {
        // No rx handler: fd_event_client still reads every message into its reused payload.
        uds_server server(server_name, true);
        std::atomic<bool> ready{false};
        server.set_on_ready_action([&]() { ready = true; });
        ASSERT_TRUE(drive_until(server, [&]() { return ready.load(); }, 5s)) << "server never came up";

        uds_client_socket client;
        ASSERT_TRUE(client.open(server_name, true));
        // The event client owns a handful of descriptors of its own (epoll, event and timer fds),
        // so the baseline is taken with both ends up and before any message.
        const int fds_idle = open_descriptor_count();

        for (int i = 0; i < messages; ++i) {
            ASSERT_TRUE(send_fd(client, pipe_fds[0], "unread"));
        }
        drive_until(server, [&]() { return false; }, 300ms);

        // Each read replaced the previous descriptor in the reused payload, so at most the last
        // one is still held.
        EXPECT_LE(open_descriptor_count(), fds_idle + 1);
    }
    // Gone with the server: nothing of the 32 stays behind.
    EXPECT_EQ(open_descriptor_count(), fds_before);

    ::close(pipe_fds[0]);
    ::close(pipe_fds[1]);
}

TEST(uds_event_client, client_keeps_a_queued_descriptor_alive_until_it_is_sent) {
    auto const server_name = unique_name("fd_lifetime");

    uds_server_socket server;
    ASSERT_TRUE(server.open(server_name, true));

    int pipe_fds[2] = {-1, -1};
    ASSERT_EQ(::pipe(pipe_fds), 0);

    uds_client client(server_name, true);
    std::atomic<bool> ready{false};
    client.set_on_ready_action([&]() { ready = true; });
    ASSERT_TRUE(drive_until(client, [&]() { return ready.load(); }, 5s)) << "client never came up";

    // tx() only queues. The caller drops its descriptor right away; the queued copy owns a
    // duplicate, so what goes out on the writable event is still a live pipe end.
    ASSERT_TRUE(send_fd(client, pipe_fds[0], "read end"));
    ::close(pipe_fds[0]);
    pipe_fds[0] = -1;

    uds_payload received;
    ASSERT_TRUE(drive_until(client, [&]() { return server.rx(received); }, 5s)) << "nothing arrived";
    ASSERT_TRUE(received.has_fds());
    ASSERT_EQ(::write(pipe_fds[1], "alive", 5), 5);
    char buffer[8] = {};
    EXPECT_EQ(::read(received.fd(), buffer, sizeof(buffer)), 5);
    EXPECT_STREQ(buffer, "alive");

    ::close(pipe_fds[1]);
}

TEST(uds_event_client, rx_handler_receives_the_descriptor_with_the_message) {
    auto const server_name = unique_name("fd_rx_handler");

    uds_server server(server_name, true);
    std::atomic<bool> ready{false};
    server.set_on_ready_action([&]() { ready = true; });

    // Keeping a copy of the payload is how a handler keeps the descriptor past the callback.
    uds_payload kept;
    std::atomic<bool> got{false};
    server.set_rx_handler([&](uds_payload const& payload, auto&) {
        kept = payload;
        got = true;
    });
    ASSERT_TRUE(drive_until(server, [&]() { return ready.load(); }, 5s)) << "server never came up";

    int pipe_fds[2] = {-1, -1};
    ASSERT_EQ(::pipe(pipe_fds), 0);
    uds_client_socket client;
    ASSERT_TRUE(client.open(server_name, true));
    ASSERT_TRUE(send_fd(client, pipe_fds[0], "for the handler"));

    ASSERT_TRUE(drive_until(server, [&]() { return got.load(); }, 5s)) << "handler never ran";
    EXPECT_EQ(std::string(kept.buffer.begin(), kept.buffer.end()), "for the handler");
    ASSERT_TRUE(kept.has_fds());
    ASSERT_EQ(::write(pipe_fds[1], "hi", 2), 2);
    char buffer[4] = {};
    EXPECT_EQ(::read(kept.fd(), buffer, sizeof(buffer)), 2);

    ::close(pipe_fds[0]);
    ::close(pipe_fds[1]);
}

TEST(uds_event_client, a_handler_answers_with_a_descriptor_through_its_device) {
    auto const server_name = unique_name("fd_answer");
    auto const client_name = unique_name("fd_answer_client");

    int pipe_fds[2] = {-1, -1};
    ASSERT_EQ(::pipe(pipe_fds), 0);

    uds_server server(server_name, true);
    std::atomic<bool> ready{false};
    server.set_on_ready_action([&]() { ready = true; });
    // The interface handed to the callback is what the send_fd overloads for event clients take.
    server.set_rx_handler([&](uds_payload const&, auto& device) { send_fd(device, pipe_fds[0], "here you go"); });
    ASSERT_TRUE(drive_until(server, [&]() { return ready.load(); }, 5s)) << "server never came up";

    uds_client_socket client;
    ASSERT_TRUE(client.open(server_name, true, client_name, true));
    ASSERT_TRUE(client.tx(uds_payload{"may I have a descriptor"}));

    uds_payload answer;
    ASSERT_TRUE(drive_until(server, [&]() { return client.rx(answer); }, 5s)) << "no answer";
    EXPECT_EQ(std::string(answer.buffer.begin(), answer.buffer.end()), "here you go");
    ASSERT_TRUE(answer.has_fds());
    ASSERT_EQ(::write(pipe_fds[1], "yes", 3), 3);
    char buffer[4] = {};
    EXPECT_EQ(::read(answer.fd(), buffer, sizeof(buffer)), 3);

    ::close(pipe_fds[0]);
    ::close(pipe_fds[1]);
}

TEST(uds_event_client, an_rx_handler_sees_the_senders_credentials_when_the_server_asked) {
    auto const server_name = unique_name("creds");

    // The third constructor argument is uds_server_socket::open's with_peer_credentials.
    uds_server server(server_name, true, true);
    std::atomic<bool> ready{false};
    server.set_on_ready_action([&]() { ready = true; });
    std::optional<everest::lib::io::uds::uds_credentials> seen;
    std::atomic<bool> got{false};
    server.set_rx_handler([&](uds_payload const& payload, auto&) {
        seen = payload.credentials;
        got = true;
    });
    ASSERT_TRUE(drive_until(server, [&]() { return ready.load(); }, 5s)) << "server never came up";

    uds_client_socket client;
    ASSERT_TRUE(client.open(server_name, true));
    ASSERT_TRUE(client.tx(uds_payload{"hello"}));
    ASSERT_TRUE(drive_until(server, [&]() { return got.load(); }, 5s)) << "handler never ran";
    ASSERT_TRUE(seen.has_value());
    EXPECT_EQ(seen->pid, ::getpid());
    EXPECT_EQ(seen->uid, ::getuid());
}

TEST(uds_event_client, a_client_without_a_local_name_gets_the_servers_answer) {
    auto const server_name = unique_name("autobind");

    uds_server server(server_name, true);
    server.set_rx_handler([](uds_payload const& payload, auto& device) { device.tx(payload); });
    std::atomic<bool> server_ready{false};
    server.set_on_ready_action([&]() { server_ready = true; });
    ASSERT_TRUE(drive_until(server, [&]() { return server_ready.load(); }, 5s)) << "server never came up";

    // Two arguments, no naming ceremony: the kernel gives the client the name the echo needs.
    uds_client client(server_name, true);
    std::atomic<bool> ready{false};
    client.set_on_ready_action([&]() { ready = true; });
    std::string echoed;
    std::atomic<bool> got{false};
    client.set_rx_handler([&](uds_payload const& payload, auto&) {
        echoed.assign(payload.buffer.begin(), payload.buffer.end());
        got = true;
    });
    ASSERT_TRUE(drive_until(client, [&]() { return ready.load(); }, 5s)) << "client never came up";
    ASSERT_TRUE(client.tx(uds_payload{"echo?"}));

    auto const deadline = std::chrono::steady_clock::now() + 5s;
    while (not got.load() and std::chrono::steady_clock::now() < deadline) {
        server.sync(10ms);
        client.sync(10ms);
    }
    ASSERT_TRUE(got.load()) << "no answer reached the unnamed client";
    EXPECT_EQ(echoed, "echo?");
}
