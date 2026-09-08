// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
//
// The uds socket policies drive non blocking descriptors, so every read here is polled to a
// deadline rather than attempted once: a datagram that has not been queued yet is not a failure.

#include <everest/io/socket/socket.hpp>
#include <everest/io/uds/uds_socket.hpp>
#include <everest/io/uds/uds_utils.hpp>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <fstream>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>
#include <vector>

#include <gtest/gtest.h>

namespace {

using namespace std::chrono_literals;
using everest::lib::io::uds::send_fd;
using everest::lib::io::uds::uds_client_socket;
using everest::lib::io::uds::uds_payload;
using everest::lib::io::uds::uds_server_socket;

// Every socket name is unique per process and test, so a parallel or repeated run never collides
// on an abstract name or a leftover path.
std::string unique_name(std::string const& tag) {
    return "everest_uds_test_" + std::to_string(::getpid()) + "_" + tag;
}

std::string unique_path(std::string const& tag) {
    return "/tmp/" + unique_name(tag) + ".sock";
}

std::string as_string(uds_payload const& payload) {
    return std::string(payload.buffer.begin(), payload.buffer.end());
}

template <class SocketT> bool rx_within(SocketT& socket, uds_payload& payload, std::chrono::milliseconds timeout) {
    auto const deadline = std::chrono::steady_clock::now() + timeout;
    do {
        if (socket.rx(payload)) {
            return true;
        }
        std::this_thread::sleep_for(1ms);
    } while (std::chrono::steady_clock::now() < deadline);
    return false;
}

} // namespace

TEST(uds_socket, abstract_client_to_server) {
    auto const server_name = unique_name("abstract_c2s");

    uds_server_socket server;
    ASSERT_TRUE(server.open(server_name, true));
    EXPECT_TRUE(server.is_open());
    EXPECT_EQ(server.get_error(), 0);

    uds_client_socket client;
    ASSERT_TRUE(client.open(server_name, true));
    ASSERT_TRUE(client.tx(uds_payload{"ping"}));

    uds_payload received;
    ASSERT_TRUE(rx_within(server, received, 2s));
    EXPECT_EQ(as_string(received), "ping");
}

TEST(uds_socket, server_answers_a_bound_client) {
    auto const server_name = unique_name("abstract_reply");
    auto const client_name = unique_name("abstract_reply_client");

    uds_server_socket server;
    ASSERT_TRUE(server.open(server_name, true));

    // Without a name of its own the client can send but never be answered.
    uds_client_socket client;
    ASSERT_TRUE(client.open(server_name, true, client_name, true));
    ASSERT_TRUE(client.tx(uds_payload{"question"}));

    uds_payload received;
    ASSERT_TRUE(rx_within(server, received, 2s));
    EXPECT_EQ(as_string(received), "question");

    // tx() on the server addresses the source of that last rx().
    ASSERT_TRUE(server.tx(uds_payload{"answer"}));

    uds_payload answer;
    ASSERT_TRUE(rx_within(client, answer, 2s));
    EXPECT_EQ(as_string(answer), "answer");
}

TEST(uds_socket, filesystem_path_round_trip) {
    auto const server_path = unique_path("fs");
    ::unlink(server_path.c_str());

    uds_server_socket server;
    ASSERT_TRUE(server.open(server_path, false));

    uds_client_socket client;
    ASSERT_TRUE(client.open(server_path, false));
    ASSERT_TRUE(client.tx(uds_payload{"on disk"}));

    uds_payload received;
    ASSERT_TRUE(rx_within(server, received, 2s));
    EXPECT_EQ(as_string(received), "on disk");

    server.close();
    ::unlink(server_path.c_str());
}

TEST(uds_socket, server_reopens_over_a_stale_socket_file) {
    auto const server_path = unique_path("stale");
    ::unlink(server_path.c_str());

    {
        uds_server_socket first;
        ASSERT_TRUE(first.open(server_path, false));
    }
    // The socket file outlives the socket, so the second bind must unlink it rather than fail.
    uds_server_socket second;
    EXPECT_TRUE(second.open(server_path, false));

    second.close();
    ::unlink(server_path.c_str());
}

namespace {

// A raw AF_UNIX datagram socket bound to a pathname, for peers the policies cannot produce: one
// whose name fills sun_path, or one that leaves its file behind without unlinking.
struct raw_path_socket {
    everest::lib::io::event::unique_fd fd;
    std::string path;

    explicit raw_path_socket(std::string bind_path) : path(std::move(bind_path)) {
        fd = everest::lib::io::event::unique_fd{::socket(AF_UNIX, SOCK_DGRAM | SOCK_NONBLOCK, 0)};
        struct sockaddr_un addr {};
        addr.sun_family = AF_UNIX;
        std::memcpy(addr.sun_path, path.data(), path.size());
        // Linux accepts a pathname filling sun_path with the terminator implied: addrlen is then
        // the whole structure.
        auto const len = static_cast<socklen_t>(offsetof(struct sockaddr_un, sun_path) +
                                                std::min(path.size() + 1, sizeof(addr.sun_path)));
        bound = ::bind(fd, reinterpret_cast<struct sockaddr*>(&addr), len) == 0;
    }
    ~raw_path_socket() {
        ::unlink(path.c_str());
    }
    bool bound{false};
};

bool path_exists(std::string const& path) {
    struct stat st {};
    return ::lstat(path.c_str(), &st) == 0;
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

TEST(uds_socket, a_peer_pathname_filling_sun_path_is_parsed_and_answered) {
    // 108 bytes: sun_path has no room for the terminator, and the kernel reports msg_namelen 111.
    std::string peer_path = unique_path("full");
    peer_path.resize(sizeof(sockaddr_un{}.sun_path), 'p');
    ASSERT_EQ(peer_path.size(), 108u);
    ::unlink(peer_path.c_str());

    auto const server_name = unique_name("full_server");
    uds_server_socket server;
    ASSERT_TRUE(server.open(server_name, true));

    raw_path_socket peer(peer_path);
    ASSERT_TRUE(peer.bound) << strerror(errno);

    struct sockaddr_un server_addr {};
    server_addr.sun_family = AF_UNIX;
    std::memcpy(server_addr.sun_path + 1, server_name.data(), server_name.size());
    auto const server_len = static_cast<socklen_t>(offsetof(struct sockaddr_un, sun_path) + 1 + server_name.size());
    ASSERT_EQ(::sendto(peer.fd, "question", 8, 0, reinterpret_cast<struct sockaddr*>(&server_addr), server_len), 8);

    uds_payload received;
    ASSERT_TRUE(rx_within(server, received, 2s));
    EXPECT_EQ(as_string(received), "question");

    // The reply reaching the peer proves the 108 byte name was parsed exactly, not read past.
    ASSERT_TRUE(server.tx(uds_payload{"answer"}));
    char buffer[16] = {};
    auto const deadline = std::chrono::steady_clock::now() + 2s;
    ssize_t got = -1;
    do {
        got = ::recv(peer.fd, buffer, sizeof(buffer), 0);
        if (got >= 0) {
            break;
        }
        std::this_thread::sleep_for(1ms);
    } while (std::chrono::steady_clock::now() < deadline);
    ASSERT_EQ(got, 6);
    EXPECT_EQ(std::string(buffer, 6), "answer");
}

TEST(uds_socket, close_removes_the_socket_file_a_server_bound) {
    auto const server_path = unique_path("removed_on_close");

    uds_server_socket server;
    ASSERT_TRUE(server.open(server_path, false));
    EXPECT_TRUE(path_exists(server_path));

    server.close();
    // The kernel keeps the file after the socket is gone; the policy is what removes it.
    EXPECT_FALSE(path_exists(server_path));
}

TEST(uds_socket, a_filesystem_bound_client_can_reconnect) {
    auto const server_path = unique_path("reconnect_server");
    auto const client_path = unique_path("reconnect_client");

    uds_server_socket server;
    ASSERT_TRUE(server.open(server_path, false));

    // setup() then connect() is the sequence fd_event_client drives on every reset.
    uds_client_socket client;
    bool ok = false;
    ASSERT_TRUE(client.setup(server_path, false, client_path, false));
    client.connect([&](bool result, int) { ok = result; });
    ASSERT_TRUE(ok) << strerror(client.get_error());
    EXPECT_TRUE(path_exists(client_path));

    // The second round must not find its own leftover in the way.
    ok = false;
    ASSERT_TRUE(client.setup(server_path, false, client_path, false));
    EXPECT_FALSE(path_exists(client_path));
    client.connect([&](bool result, int) { ok = result; });
    EXPECT_TRUE(ok) << strerror(client.get_error());

    client.close();
    EXPECT_FALSE(path_exists(client_path));
}

TEST(uds_socket, server_replaces_a_stale_socket_file) {
    auto const server_path = unique_path("stale_file");
    ::unlink(server_path.c_str());
    {
        // A raw socket leaves its file behind, like a process that crashed.
        raw_path_socket leftover(server_path);
        ASSERT_TRUE(leftover.bound);
        leftover.fd.close();
        ASSERT_TRUE(path_exists(server_path));

        uds_server_socket server;
        EXPECT_TRUE(server.open(server_path, false)) << strerror(server.get_error());
    }
}

TEST(uds_socket, server_refuses_a_path_that_is_not_a_socket) {
    auto const path = unique_path("not_a_socket");
    {
        std::ofstream file(path);
        file << "somebody's data";
    }
    ASSERT_TRUE(path_exists(path));

    uds_server_socket server;
    EXPECT_FALSE(server.open(path, false));
    EXPECT_EQ(server.get_error(), EEXIST);
    // Above all, the file is still there.
    EXPECT_TRUE(path_exists(path));

    ::unlink(path.c_str());
}

TEST(uds_socket, server_refuses_a_path_a_live_server_holds) {
    auto const server_path = unique_path("live");

    uds_server_socket first;
    ASSERT_TRUE(first.open(server_path, false));

    {
        uds_server_socket second;
        EXPECT_FALSE(second.open(server_path, false));
        EXPECT_EQ(second.get_error(), EADDRINUSE);
    }
    // Neither the failed open nor the destructor of the loser touched the winner's file.
    EXPECT_TRUE(path_exists(server_path));

    uds_client_socket client;
    ASSERT_TRUE(client.open(server_path, false));
    ASSERT_TRUE(client.tx(uds_payload{"still yours"}));
    uds_payload received;
    ASSERT_TRUE(rx_within(first, received, 2s));
    EXPECT_EQ(as_string(received), "still yours");
}

TEST(uds_socket, socket_layer_client_bind_defaults_to_the_abstract_namespace) {
    namespace socket = everest::lib::io::socket;
    auto const server_name = unique_name("default_ns");
    auto const client_name = unique_name("default_ns_client");

    uds_server_socket server;
    ASSERT_TRUE(server.open(server_name, true));

    // Same omitted argument as uds_client_socket::open(server, true, client): must mean the same.
    auto client = socket::open_uds_client_socket(server_name, true, client_name);
    ASSERT_EQ(::send(client, "hi", 2, 0), 2);

    uds_payload received;
    ASSERT_TRUE(rx_within(server, received, 2s));
    // A reply arriving proves the client was bound, and nothing was created in the filesystem.
    EXPECT_FALSE(path_exists(client_name));
    ASSERT_TRUE(server.tx(uds_payload{"hi back"}));
    char buffer[16] = {};
    auto const deadline = std::chrono::steady_clock::now() + 2s;
    ssize_t got = -1;
    do {
        got = ::recv(client, buffer, sizeof(buffer), 0);
        if (got >= 0) {
            break;
        }
        std::this_thread::sleep_for(1ms);
    } while (std::chrono::steady_clock::now() < deadline);
    EXPECT_EQ(got, 7);
}

TEST(uds_socket, client_to_a_missing_server_fails_and_reports_the_cause) {
    uds_client_socket client;
    EXPECT_FALSE(client.open(unique_name("nobody_listening"), true));
    EXPECT_FALSE(client.is_open());
    // The errno of the refused connect survives the failed open, so a consumer sees the cause
    // instead of a healthy zero.
    EXPECT_NE(client.get_error(), 0);
}

TEST(uds_socket, a_name_too_long_for_sun_path_fails) {
    // sun_path holds 108 bytes at most, terminator or abstract marker included.
    std::string const too_long(200, 'x');

    uds_server_socket server;
    EXPECT_FALSE(server.open(too_long, true));
    EXPECT_NE(server.get_error(), 0);

    uds_client_socket client;
    EXPECT_FALSE(client.open(too_long, true));
    EXPECT_NE(client.get_error(), 0);
}

// AF_UNIX reports a vanished peer synchronously on the send and leaves SO_ERROR alone. The
// recorded errno is what lets get_error() name the failure, which is what an event client needs to
// reset instead of retrying the same payload forever.
TEST(uds_socket, client_send_to_a_vanished_server_records_the_errno) {
    auto const server_name = unique_name("vanished_server");

    uds_client_socket client;
    {
        uds_server_socket server;
        ASSERT_TRUE(server.open(server_name, true));
        ASSERT_TRUE(client.open(server_name, true));
        EXPECT_EQ(client.get_error(), 0);
    }

    EXPECT_FALSE(client.tx(uds_payload{"into the void"}));
    EXPECT_EQ(client.get_error(), ECONNREFUSED);
    // The socket is still open, so it is the recorded errno that reports, not the missing fd.
    EXPECT_TRUE(client.is_open());
}

TEST(uds_socket, a_read_with_nothing_queued_is_not_an_error) {
    uds_server_socket server;
    ASSERT_TRUE(server.open(unique_name("nothing_queued"), true));

    uds_payload received;
    EXPECT_FALSE(server.rx(received));
    // EAGAIN is the normal answer of a non blocking socket with an empty queue.
    EXPECT_EQ(server.get_error(), 0);
}

TEST(uds_socket, server_tx_without_a_source_is_dropped_not_failed) {
    uds_server_socket server;
    ASSERT_TRUE(server.open(unique_name("no_source_drop"), true));

    // Nobody to answer: dropped, and the server is as healthy as before.
    EXPECT_TRUE(server.tx(uds_payload{"nowhere"}));
    EXPECT_EQ(server.get_error(), 0);
    EXPECT_TRUE(server.is_open());
}

TEST(uds_socket, a_reply_to_an_unnamed_sender_is_dropped_not_misdelivered) {
    auto const server_name = unique_name("unnamed_reply");
    auto const named_client_name = unique_name("unnamed_reply_named");

    uds_server_socket server;
    ASSERT_TRUE(server.open(server_name, true));

    uds_client_socket named_client;
    ASSERT_TRUE(named_client.open(server_name, true, named_client_name, true));
    // The policies always have a name; a sender without one is a raw socket that never bound.
    auto unnamed_client = everest::lib::io::socket::open_uds_client_socket(server_name, true, "", true,
                                                                           /*client_autobind=*/false);

    ASSERT_TRUE(named_client.tx(uds_payload{"question"}));
    uds_payload received;
    ASSERT_TRUE(rx_within(server, received, 2s));
    ASSERT_EQ(::send(unnamed_client, "noise", 5, 0), 5);
    ASSERT_TRUE(rx_within(server, received, 2s));
    EXPECT_EQ(as_string(received), "noise");

    // The answer to the unnamed sender has nowhere to go. It must not reach the named client.
    EXPECT_TRUE(server.tx(uds_payload{"for the unnamed one"}));
    EXPECT_EQ(server.get_error(), 0);
    uds_payload stray;
    EXPECT_FALSE(rx_within(named_client, stray, 200ms));

    // The named client is answered again as soon as it speaks.
    ASSERT_TRUE(named_client.tx(uds_payload{"still here"}));
    ASSERT_TRUE(rx_within(server, received, 2s));
    ASSERT_TRUE(server.tx(uds_payload{"answer"}));
    uds_payload answer;
    ASSERT_TRUE(rx_within(named_client, answer, 2s));
    EXPECT_EQ(as_string(answer), "answer");
}

TEST(uds_socket, a_reply_to_a_departed_client_is_dropped_not_failed) {
    auto const server_name = unique_name("departed");
    auto const client_name = unique_name("departed_client");

    uds_server_socket server;
    ASSERT_TRUE(server.open(server_name, true));

    {
        uds_client_socket client;
        ASSERT_TRUE(client.open(server_name, true, client_name, true));
        ASSERT_TRUE(client.tx(uds_payload{"question"}));
        uds_payload received;
        ASSERT_TRUE(rx_within(server, received, 2s));
    }

    // Datagram contract: undeliverable is dropped, the server itself is fine.
    EXPECT_TRUE(server.tx(uds_payload{"answer"}));
    EXPECT_EQ(server.get_error(), 0);
    EXPECT_TRUE(server.is_open());
}

// ---- descriptors travelling with a payload -----------------------------------------------------

TEST(uds_socket, a_descriptor_travels_with_its_message) {
    auto const server_name = unique_name("fd_pass");

    uds_server_socket server;
    ASSERT_TRUE(server.open(server_name, true));

    int pipe_fds[2] = {-1, -1};
    ASSERT_EQ(::pipe(pipe_fds), 0);

    uds_client_socket client;
    ASSERT_TRUE(client.open(server_name, true));
    ASSERT_TRUE(send_fd(client, pipe_fds[0], "the read end"));

    uds_payload received;
    ASSERT_TRUE(rx_within(server, received, 2s));
    EXPECT_EQ(as_string(received), "the read end");
    ASSERT_EQ(received.fds.size(), 1u);
    // The descriptor is this process's own copy, so it must not be the one that was sent.
    EXPECT_NE(received.fd(), pipe_fds[0]);

    // Reading through the received descriptor proves it points at the same pipe.
    ASSERT_EQ(::write(pipe_fds[1], "hello", 5), 5);
    char buffer[8] = {};
    EXPECT_EQ(::read(received.fd(), buffer, sizeof(buffer)), 5);
    EXPECT_STREQ(buffer, "hello");

    // The received payload owns its descriptor: the last copy closes it.
    const int received_fd = received.fd();
    received = uds_payload{};
    EXPECT_EQ(::fcntl(received_fd, F_GETFD), -1);
    EXPECT_EQ(errno, EBADF);

    ::close(pipe_fds[0]);
    ::close(pipe_fds[1]);
}

TEST(uds_socket, a_received_descriptor_is_close_on_exec) {
    auto const server_name = unique_name("fd_cloexec");

    uds_server_socket server;
    ASSERT_TRUE(server.open(server_name, true));

    int pipe_fds[2] = {-1, -1};
    ASSERT_EQ(::pipe(pipe_fds), 0); // deliberately without O_CLOEXEC
    ASSERT_EQ(::fcntl(pipe_fds[0], F_GETFD) & FD_CLOEXEC, 0);

    uds_client_socket client;
    ASSERT_TRUE(client.open(server_name, true));
    ASSERT_TRUE(send_fd(client, pipe_fds[0], "inheritable at the sender"));

    uds_payload received;
    ASSERT_TRUE(rx_within(server, received, 2s));
    ASSERT_TRUE(received.has_fds());
    // Every descriptor this library creates is close-on-exec; one it receives is no exception.
    EXPECT_NE(::fcntl(received.fd(), F_GETFD) & FD_CLOEXEC, 0);

    ::close(pipe_fds[0]);
    ::close(pipe_fds[1]);
}

TEST(uds_socket, attach_duplicate_leaves_the_caller_its_descriptor) {
    int pipe_fds[2] = {-1, -1};
    ASSERT_EQ(::pipe(pipe_fds), 0);

    {
        uds_payload payload{"dup"};
        ASSERT_TRUE(payload.attach_duplicate(pipe_fds[0]));
        EXPECT_NE(payload.fd(), pipe_fds[0]);
        EXPECT_NE(::fcntl(payload.fd(), F_GETFD) & FD_CLOEXEC, 0);
    }
    // The payload closed its duplicate; the caller's descriptor is untouched.
    EXPECT_NE(::fcntl(pipe_fds[0], F_GETFD), -1);

    // A descriptor that cannot be duplicated attaches nothing, and says so.
    uds_payload none;
    EXPECT_FALSE(none.attach_duplicate(-1));
    EXPECT_FALSE(none.has_fds());
    EXPECT_EQ(none.fd(), -1);

    ::close(pipe_fds[0]);
    ::close(pipe_fds[1]);
}

TEST(uds_socket, attach_takes_over_and_closes_with_the_last_copy) {
    int pipe_fds[2] = {-1, -1};
    ASSERT_EQ(::pipe(pipe_fds), 0);
    const int handed_over = pipe_fds[0];

    uds_payload first{"owned"};
    ASSERT_TRUE(first.attach(everest::lib::io::event::unique_fd{handed_over}));
    EXPECT_EQ(first.fd(), handed_over);
    {
        auto second = first; // shared: both copies keep it alive
        first = uds_payload{};
        EXPECT_NE(::fcntl(handed_over, F_GETFD), -1);
    }
    EXPECT_EQ(::fcntl(handed_over, F_GETFD), -1);
    EXPECT_EQ(errno, EBADF);

    // Nothing to take over is refused rather than attached as a hole.
    EXPECT_FALSE(first.attach(everest::lib::io::event::unique_fd{}));

    ::close(pipe_fds[1]);
}

TEST(uds_socket, a_message_without_descriptors_arrives_with_none) {
    auto const server_name = unique_name("fd_none");

    uds_server_socket server;
    ASSERT_TRUE(server.open(server_name, true));

    uds_client_socket client;
    ASSERT_TRUE(client.open(server_name, true));
    ASSERT_TRUE(client.tx(uds_payload{"just bytes"}));

    uds_payload received;
    ASSERT_TRUE(rx_within(server, received, 2s));
    EXPECT_EQ(as_string(received), "just bytes");
    EXPECT_FALSE(received.has_fds());
}

TEST(uds_socket, a_descriptor_travels_with_an_empty_message) {
    auto const server_name = unique_name("fd_empty_message");

    uds_server_socket server;
    ASSERT_TRUE(server.open(server_name, true));

    int pipe_fds[2] = {-1, -1};
    ASSERT_EQ(::pipe(pipe_fds), 0);

    uds_client_socket client;
    ASSERT_TRUE(client.open(server_name, true));
    // A zero byte unix datagram still carries its control message, so nothing is padded in.
    ASSERT_TRUE(send_fd(client, pipe_fds[0]));

    uds_payload received;
    ASSERT_TRUE(rx_within(server, received, 2s));
    EXPECT_EQ(received.size(), 0u);
    EXPECT_TRUE(received.has_fds());

    ::close(pipe_fds[0]);
    ::close(pipe_fds[1]);
}

TEST(uds_socket, several_descriptors_travel_in_one_message_in_order) {
    auto const server_name = unique_name("fd_several");

    uds_server_socket server;
    ASSERT_TRUE(server.open(server_name, true));

    // Three pipes; each read end travels, and each is told apart by what its write end says.
    int pipes[3][2] = {{-1, -1}, {-1, -1}, {-1, -1}};
    uds_payload payload{"three read ends"};
    for (auto& p : pipes) {
        ASSERT_EQ(::pipe(p), 0);
        ASSERT_TRUE(payload.attach_duplicate(p[0]));
    }

    uds_client_socket client;
    ASSERT_TRUE(client.open(server_name, true));
    ASSERT_TRUE(client.tx(payload));

    uds_payload received;
    ASSERT_TRUE(rx_within(server, received, 2s));
    ASSERT_EQ(received.fds.size(), 3u);
    for (size_t i = 0; i < 3; ++i) {
        const char tag = static_cast<char>('a' + i);
        ASSERT_EQ(::write(pipes[i][1], &tag, 1), 1);
        char got = 0;
        EXPECT_EQ(::read(received.fd(i), &got, 1), 1);
        EXPECT_EQ(got, tag) << "descriptor " << i << " arrived out of order";
    }

    for (auto& p : pipes) {
        ::close(p[0]);
        ::close(p[1]);
    }
}

TEST(uds_socket, a_payload_stops_attaching_at_max_fds) {
    int pipe_fds[2] = {-1, -1};
    ASSERT_EQ(::pipe(pipe_fds), 0);

    uds_payload payload;
    for (size_t i = 0; i < uds_payload::max_fds; ++i) {
        ASSERT_TRUE(payload.attach_duplicate(pipe_fds[0])) << "at " << i;
    }
    // One more than the kernel moves in a single message would be dropped by the receiver.
    EXPECT_FALSE(payload.attach_duplicate(pipe_fds[0]));
    EXPECT_EQ(payload.fds.size(), uds_payload::max_fds);

    ::close(pipe_fds[0]);
    ::close(pipe_fds[1]);
}

TEST(uds_socket, a_message_too_large_is_dropped_and_its_descriptor_closed) {
    auto const server_name = unique_name("fd_too_large");

    uds_server_socket server;
    ASSERT_TRUE(server.open(server_name, true));

    int pipe_fds[2] = {-1, -1};
    ASSERT_EQ(::pipe(pipe_fds), 0);

    // A raw sender, because the policies refuse to send past max_size themselves.
    int sender = ::socket(AF_UNIX, SOCK_DGRAM, 0);
    ASSERT_GE(sender, 0);
    const int fds_before = open_descriptor_count();

    struct sockaddr_un addr {};
    addr.sun_family = AF_UNIX;
    std::memcpy(addr.sun_path + 1, server_name.data(), server_name.size());
    auto const addr_len = static_cast<socklen_t>(offsetof(struct sockaddr_un, sun_path) + 1 + server_name.size());

    std::vector<char> oversized(uds_payload::max_size + 1, 'x');
    struct iovec iov {};
    iov.iov_base = oversized.data();
    iov.iov_len = oversized.size();
    alignas(struct cmsghdr) char control[CMSG_SPACE(sizeof(int))] = {};
    struct msghdr msg {};
    msg.msg_name = &addr;
    msg.msg_namelen = addr_len;
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = control;
    msg.msg_controllen = sizeof(control);
    struct cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(sizeof(int));
    std::memcpy(CMSG_DATA(cmsg), &pipe_fds[0], sizeof(int));
    ASSERT_EQ(::sendmsg(sender, &msg, 0), static_cast<ssize_t>(oversized.size()));

    // Delivered in part is not delivered: the message is dropped, and the descriptor that arrived
    // with it is closed rather than leaked. The connection itself is fine.
    uds_payload received;
    EXPECT_FALSE(rx_within(server, received, 200ms));
    EXPECT_EQ(server.get_error(), 0);
    EXPECT_EQ(open_descriptor_count(), fds_before);

    // And the next well formed message still arrives.
    ASSERT_EQ(::sendto(sender, "fits", 4, 0, reinterpret_cast<struct sockaddr*>(&addr), addr_len), 4);
    EXPECT_TRUE(rx_within(server, received, 2s));
    EXPECT_EQ(as_string(received), "fits");

    ::close(sender);
    ::close(pipe_fds[0]);
    ::close(pipe_fds[1]);
}

TEST(uds_socket, an_oversized_buffer_is_dropped_and_the_connection_kept) {
    auto const server_name = unique_name("oversized_drop");

    uds_server_socket server;
    ASSERT_TRUE(server.open(server_name, true));
    uds_client_socket client;
    ASSERT_TRUE(client.open(server_name, true));

    // set_message() refuses this, so only a direct write to the buffer gets here.
    uds_payload oversized;
    EXPECT_FALSE(oversized.set_message(std::string(uds_payload::max_size + 1, 'm')));
    oversized.buffer.assign(uds_payload::max_size + 1, 'm');
    // Can never be delivered: dropped, not retried, and not charged to the connection.
    EXPECT_TRUE(client.tx(oversized));
    EXPECT_EQ(client.get_error(), 0);

    // The next payload goes through as if nothing happened.
    ASSERT_TRUE(client.tx(uds_payload{"fits"}));
    uds_payload received;
    ASSERT_TRUE(rx_within(server, received, 2s));
    EXPECT_EQ(as_string(received), "fits");
}

TEST(uds_socket, send_fd_refuses_what_it_cannot_send) {
    auto const server_name = unique_name("send_fd_refuses");

    uds_server_socket server;
    ASSERT_TRUE(server.open(server_name, true));
    uds_client_socket client;
    ASSERT_TRUE(client.open(server_name, true));

    // Nothing was sent in either case, so the connection reports no error.
    EXPECT_FALSE(send_fd(client, -1, "no such descriptor"));
    int pipe_fds[2] = {-1, -1};
    ASSERT_EQ(::pipe(pipe_fds), 0);
    EXPECT_FALSE(send_fd(client, pipe_fds[0], std::string(uds_payload::max_size + 1, 'm')));
    EXPECT_EQ(client.get_error(), 0);

    ::close(pipe_fds[0]);
    ::close(pipe_fds[1]);
}

TEST(uds_socket, send_fd_handing_over_closes_the_descriptor_once_sent) {
    auto const server_name = unique_name("send_fd_handover");

    uds_server_socket server;
    ASSERT_TRUE(server.open(server_name, true));
    uds_client_socket client;
    ASSERT_TRUE(client.open(server_name, true));

    int pipe_fds[2] = {-1, -1};
    ASSERT_EQ(::pipe(pipe_fds), 0);
    const int handed_over = pipe_fds[0];

    ASSERT_TRUE(send_fd(client, everest::lib::io::event::unique_fd{handed_over}, "yours now"));
    // The raw socket sends in the call, and the payload it built is gone with it.
    EXPECT_EQ(::fcntl(handed_over, F_GETFD), -1);

    uds_payload received;
    ASSERT_TRUE(rx_within(server, received, 2s));
    ASSERT_TRUE(received.has_fds());
    ASSERT_EQ(::write(pipe_fds[1], "ok", 2), 2);
    char buffer[4] = {};
    EXPECT_EQ(::read(received.fd(), buffer, sizeof(buffer)), 2);

    ::close(pipe_fds[1]);
}

// ---- what the second review found -------------------------------------------------------------

TEST(uds_socket, a_reply_to_a_client_that_connected_elsewhere_is_dropped_not_failed) {
    auto const first_name = unique_name("elsewhere_first");
    auto const second_name = unique_name("elsewhere_second");
    auto const client_name = unique_name("elsewhere_client");

    uds_server_socket first;
    ASSERT_TRUE(first.open(first_name, true));
    uds_server_socket second;
    ASSERT_TRUE(second.open(second_name, true));

    uds_client_socket client;
    ASSERT_TRUE(client.open(first_name, true, client_name, true));
    ASSERT_TRUE(client.tx(uds_payload{"to the first"}));
    uds_payload received;
    ASSERT_TRUE(rx_within(first, received, 2s));

    // The client's socket moves on to the second server, keeping its name. A reply from the first
    // now fails with EPERM in the kernel: not a fault of the first server.
    struct sockaddr_un second_addr {};
    second_addr.sun_family = AF_UNIX;
    std::memcpy(second_addr.sun_path + 1, second_name.data(), second_name.size());
    auto const second_len = static_cast<socklen_t>(offsetof(struct sockaddr_un, sun_path) + 1 + second_name.size());
    ASSERT_EQ(::connect(client.get_fd(), reinterpret_cast<struct sockaddr*>(&second_addr), second_len), 0);
    EXPECT_TRUE(first.tx(uds_payload{"late answer"}));
    EXPECT_EQ(first.get_error(), 0);
    EXPECT_TRUE(first.is_open());
    // Dropped means dropped: the kernel refused it, nothing arrived.
    uds_payload stray;
    EXPECT_FALSE(rx_within(client, stray, 200ms));
}

TEST(uds_socket, an_empty_name_is_refused_not_bound_as_a_hidden_abstract_socket) {
    uds_server_socket server;
    EXPECT_FALSE(server.open("", false));
    EXPECT_EQ(server.get_error(), EINVAL);
    EXPECT_FALSE(server.open("", true));
    EXPECT_EQ(server.get_error(), EINVAL);

    uds_client_socket client;
    EXPECT_FALSE(client.open("", false));
    EXPECT_EQ(client.get_error(), EINVAL);
}

TEST(uds_socket, a_client_bind_that_fails_to_connect_leaves_no_socket_file) {
    auto const client_path = unique_path("no_leftover_client");
    ::unlink(client_path.c_str());

    // Nothing listens: the bind succeeds, the connect fails, and the file must go with it.
    uds_client_socket client;
    EXPECT_FALSE(client.open(unique_name("nobody_here"), true, client_path, false));
    EXPECT_EQ(client.get_error(), ECONNREFUSED);
    EXPECT_FALSE(path_exists(client_path));

    // Same through the async path fd_event_client drives.
    bool ok = true;
    ASSERT_TRUE(client.setup(unique_name("nobody_here"), true, client_path, false));
    client.connect([&](bool result, int) { ok = result; });
    EXPECT_FALSE(ok);
    EXPECT_FALSE(path_exists(client_path));
}

TEST(uds_socket, opening_a_second_path_removes_the_first) {
    auto const first = unique_path("reopen_first");
    auto const second = unique_path("reopen_second");

    uds_server_socket server;
    ASSERT_TRUE(server.open(first, false));
    EXPECT_TRUE(path_exists(first));
    // No close() in between: the policy still owns the first file and must not forget it.
    ASSERT_TRUE(server.open(second, false));
    EXPECT_FALSE(path_exists(first));
    EXPECT_TRUE(path_exists(second));
}

// ---- a name without asking for one -------------------------------------------------------------

namespace {

// The address a socket is bound to, as the kernel reports it. Empty for an unnamed socket.
std::string bound_abstract_name(int fd) {
    struct sockaddr_un addr {};
    socklen_t len = sizeof(addr);
    if (::getsockname(fd, reinterpret_cast<struct sockaddr*>(&addr), &len) != 0) {
        return {};
    }
    constexpr auto family_offset = offsetof(struct sockaddr_un, sun_path);
    if (len <= family_offset + 1 or addr.sun_path[0] != '\0') {
        return {};
    }
    return std::string(addr.sun_path + 1, len - family_offset - 1);
}

} // namespace

TEST(uds_socket, a_client_without_a_name_of_its_own_is_still_answered) {
    auto const server_name = unique_name("autobind");

    uds_server_socket server;
    ASSERT_TRUE(server.open(server_name, true));

    // No local name given: the kernel assigns one, so the reply has somewhere to go.
    uds_client_socket client;
    ASSERT_TRUE(client.open(server_name, true));
    auto const assigned = bound_abstract_name(client.get_fd());
    EXPECT_FALSE(assigned.empty());

    ASSERT_TRUE(client.tx(uds_payload{"question"}));
    uds_payload received;
    ASSERT_TRUE(rx_within(server, received, 2s));
    ASSERT_TRUE(server.tx(uds_payload{"answer"}));
    uds_payload answer;
    ASSERT_TRUE(rx_within(client, answer, 2s));
    EXPECT_EQ(as_string(answer), "answer");
}

TEST(uds_socket, two_autobound_clients_get_distinct_names) {
    auto const server_name = unique_name("autobind_two");
    uds_server_socket server;
    ASSERT_TRUE(server.open(server_name, true));

    uds_client_socket first;
    uds_client_socket second;
    ASSERT_TRUE(first.open(server_name, true));
    ASSERT_TRUE(second.open(server_name, true));
    auto const first_name = bound_abstract_name(first.get_fd());
    auto const second_name = bound_abstract_name(second.get_fd());
    EXPECT_FALSE(first_name.empty());
    EXPECT_NE(first_name, second_name);

    // Each gets its own answer.
    ASSERT_TRUE(first.tx(uds_payload{"one"}));
    uds_payload received;
    ASSERT_TRUE(rx_within(server, received, 2s));
    ASSERT_TRUE(server.tx(uds_payload{"to one"}));
    ASSERT_TRUE(second.tx(uds_payload{"two"}));
    ASSERT_TRUE(rx_within(server, received, 2s));
    ASSERT_TRUE(server.tx(uds_payload{"to two"}));

    uds_payload answer;
    ASSERT_TRUE(rx_within(first, answer, 2s));
    EXPECT_EQ(as_string(answer), "to one");
    ASSERT_TRUE(rx_within(second, answer, 2s));
    EXPECT_EQ(as_string(answer), "to two");
}

TEST(uds_socket, a_chosen_name_is_kept_and_autobind_is_not_added) {
    auto const server_name = unique_name("autobind_named");
    auto const client_name = unique_name("autobind_named_client");
    uds_server_socket server;
    ASSERT_TRUE(server.open(server_name, true));

    uds_client_socket client;
    ASSERT_TRUE(client.open(server_name, true, client_name, true));
    EXPECT_EQ(bound_abstract_name(client.get_fd()), client_name);
}

TEST(uds_socket, socket_layer_autobind_is_explicit_and_exclusive) {
    namespace socket = everest::lib::io::socket;
    auto const server_name = unique_name("autobind_layer");
    uds_server_socket server;
    ASSERT_TRUE(server.open(server_name, true));

    // The layer below the policies keeps the send only socket available.
    auto unnamed = socket::open_uds_client_socket(server_name, true, "", true, /*client_autobind=*/false);
    EXPECT_TRUE(bound_abstract_name(unnamed).empty());
    auto autobound = socket::open_uds_client_socket(server_name, true, "", true, /*client_autobind=*/true);
    EXPECT_FALSE(bound_abstract_name(autobound).empty());

    // A name and autobind are two answers to one question.
    try {
        auto both = socket::open_uds_client_socket(server_name, true, "a_name", true, /*client_autobind=*/true);
        FAIL() << "a name together with autobind must be refused";
    } catch (socket::socket_error const& e) {
        EXPECT_EQ(e.error(), EINVAL);
    }
}

// ---- permissions on a socket file --------------------------------------------------------------

namespace {

mode_t permissions_of(std::string const& path) {
    struct stat st {};
    if (::lstat(path.c_str(), &st) != 0) {
        return static_cast<mode_t>(-1);
    }
    return st.st_mode & 0777;
}

} // namespace

TEST(uds_socket, a_server_gives_its_socket_file_the_permissions_asked_for) {
    auto const path = unique_path("mode");

    uds_server_socket server;
    ASSERT_TRUE(server.open(path, false, false, 0600)) << strerror(server.get_error());
    EXPECT_EQ(permissions_of(path), 0600u);

    server.close();
    ASSERT_TRUE(server.open(path, false, false, 0660)) << strerror(server.get_error());
    // Not masked by the umask: chmod sets exactly what was asked.
    EXPECT_EQ(permissions_of(path), 0660u);
}

TEST(uds_socket, permissions_for_an_abstract_name_are_refused_before_anything_is_created) {
    uds_server_socket server;
    EXPECT_FALSE(server.open(unique_name("abstract_mode"), true, false, 0600));
    EXPECT_EQ(server.get_error(), EINVAL);
    EXPECT_FALSE(server.is_open());
}

TEST(uds_socket, a_client_without_write_permission_on_the_socket_file_is_refused) {
    if (::geteuid() == 0) {
        GTEST_SKIP() << "root bypasses file permissions";
    }
    auto const path = unique_path("mode_refuses");

    uds_server_socket server;
    ASSERT_TRUE(server.open(path, false, false, 0400)) << strerror(server.get_error());

    // Connecting needs write permission on the file, which the owner just gave up.
    uds_client_socket client;
    EXPECT_FALSE(client.open(path, false));
    EXPECT_EQ(client.get_error(), EACCES);

    // Restored, the same client gets in: it was the permission, not the socket.
    ASSERT_EQ(::chmod(path.c_str(), 0600), 0);
    EXPECT_TRUE(client.open(path, false)) << strerror(client.get_error());
}

// ---- who sent this ----------------------------------------------------------------------------

TEST(uds_socket, a_server_that_asked_gets_the_senders_credentials_with_every_datagram) {
    auto const server_name = unique_name("creds_server");

    uds_server_socket server;
    ASSERT_TRUE(server.open(server_name, true, /*with_peer_credentials=*/true));

    uds_client_socket client;
    ASSERT_TRUE(client.open(server_name, true));
    ASSERT_TRUE(client.tx(uds_payload{"who am I"}));

    uds_payload received;
    ASSERT_TRUE(rx_within(server, received, 2s));
    ASSERT_TRUE(received.credentials.has_value());
    // Filled in by the kernel from the sending process, which is this one.
    EXPECT_EQ(received.credentials->pid, ::getpid());
    EXPECT_EQ(received.credentials->uid, ::getuid());
    EXPECT_EQ(received.credentials->gid, ::getgid());
}

TEST(uds_socket, a_server_that_did_not_ask_gets_no_credentials) {
    auto const server_name = unique_name("no_creds_server");

    uds_server_socket server;
    ASSERT_TRUE(server.open(server_name, true));

    uds_client_socket client;
    ASSERT_TRUE(client.open(server_name, true));
    ASSERT_TRUE(client.tx(uds_payload{"anonymous"}));

    uds_payload received;
    ASSERT_TRUE(rx_within(server, received, 2s));
    EXPECT_FALSE(received.credentials.has_value());
}

TEST(uds_socket, a_client_that_asked_gets_the_servers_credentials_with_the_reply) {
    auto const server_name = unique_name("creds_reply");
    auto const client_name = unique_name("creds_reply_client");

    uds_server_socket server;
    ASSERT_TRUE(server.open(server_name, true));

    uds_client_socket client;
    ASSERT_TRUE(client.open(server_name, true, client_name, true, /*with_peer_credentials=*/true));
    ASSERT_TRUE(client.tx(uds_payload{"question"}));

    uds_payload received;
    ASSERT_TRUE(rx_within(server, received, 2s));
    ASSERT_TRUE(server.tx(uds_payload{"answer"}));

    uds_payload answer;
    ASSERT_TRUE(rx_within(client, answer, 2s));
    ASSERT_TRUE(answer.credentials.has_value());
    EXPECT_EQ(answer.credentials->pid, ::getpid());
    EXPECT_EQ(answer.credentials->uid, ::getuid());
}

TEST(uds_socket, credentials_and_descriptors_share_one_message) {
    auto const server_name = unique_name("creds_and_fds");

    uds_server_socket server;
    ASSERT_TRUE(server.open(server_name, true, /*with_peer_credentials=*/true));

    int pipe_fds[2] = {-1, -1};
    ASSERT_EQ(::pipe(pipe_fds), 0);

    uds_client_socket client;
    ASSERT_TRUE(client.open(server_name, true));
    ASSERT_TRUE(send_fd(client, pipe_fds[0], "both"));

    // Two control messages in one datagram; neither displaces the other.
    uds_payload received;
    ASSERT_TRUE(rx_within(server, received, 2s));
    EXPECT_EQ(as_string(received), "both");
    ASSERT_TRUE(received.has_fds());
    ASSERT_TRUE(received.credentials.has_value());
    EXPECT_EQ(received.credentials->pid, ::getpid());

    ::close(pipe_fds[0]);
    ::close(pipe_fds[1]);
}

TEST(uds_socket, credentials_set_on_an_outgoing_payload_do_not_reach_the_peer) {
    auto const server_name = unique_name("creds_forged");

    uds_server_socket server;
    ASSERT_TRUE(server.open(server_name, true, /*with_peer_credentials=*/true));

    uds_client_socket client;
    ASSERT_TRUE(client.open(server_name, true));
    uds_payload forged{"I am root"};
    forged.credentials = everest::lib::io::uds::uds_credentials{1, 0, 0};
    ASSERT_TRUE(client.tx(forged));

    uds_payload received;
    ASSERT_TRUE(rx_within(server, received, 2s));
    ASSERT_TRUE(received.credentials.has_value());
    // What arrives is what the kernel saw, not what the sender wrote.
    EXPECT_EQ(received.credentials->pid, ::getpid());
    EXPECT_EQ(received.credentials->uid, ::getuid());
}

TEST(uds_socket, a_datagram_socket_has_no_peer_credentials_to_ask_for) {
    namespace socket = everest::lib::io::socket;
    uds_server_socket server;
    ASSERT_TRUE(server.open(unique_name("no_peer"), true));
    // SO_PEERCRED wants a connection; a bound datagram server has senders, not a peer.
    EXPECT_FALSE(socket::get_peer_credentials(server.get_fd()).has_value());
}

// The SEQPACKET pair is the control channel primitive: message boundaries like a datagram, and a
// connection whose end the kernel reports. Both properties are asserted here, because neither is
// what the datagram sockets above provide.
TEST(uds_seqpacket_socket, listener_accepts_a_client_and_keeps_message_boundaries) {
    namespace socket = everest::lib::io::socket;
    auto const server_name = unique_name("seqpacket");

    auto listener = socket::open_uds_seqpacket_server_socket(server_name, true);
    ASSERT_GE(static_cast<int>(listener), 0);

    auto client = socket::open_uds_seqpacket_client_socket(server_name, true);
    ASSERT_GE(static_cast<int>(client), 0);

    // The listener is non blocking, so the accept is polled to a deadline.
    int peer = -1;
    auto const deadline = std::chrono::steady_clock::now() + 2s;
    do {
        peer = ::accept(listener, nullptr, nullptr);
        if (peer >= 0) {
            break;
        }
        std::this_thread::sleep_for(1ms);
    } while (std::chrono::steady_clock::now() < deadline);
    ASSERT_GE(peer, 0);

    // Two writes must arrive as two messages, not as one concatenated stream.
    ASSERT_EQ(::send(client, "first", 5, 0), 5);
    ASSERT_EQ(::send(client, "second", 6, 0), 6);

    char buffer[32] = {};
    ASSERT_EQ(::recv(peer, buffer, sizeof(buffer), 0), 5);
    EXPECT_EQ(std::string(buffer, 5), "first");
    std::memset(buffer, 0, sizeof(buffer));
    ASSERT_EQ(::recv(peer, buffer, sizeof(buffer), 0), 6);
    EXPECT_EQ(std::string(buffer, 6), "second");

    // Closing the client is an EOF on the accepted peer, which a datagram socket cannot report.
    client.close();
    EXPECT_EQ(::recv(peer, buffer, sizeof(buffer), 0), 0);

    ::close(peer);
}

TEST(uds_seqpacket_socket, connecting_to_a_missing_server_throws_with_the_errno) {
    namespace socket = everest::lib::io::socket;

    try {
        auto client = socket::open_uds_seqpacket_client_socket(unique_name("seqpacket_nobody"), true);
        FAIL() << "connecting to a name nothing listens on must throw";
    } catch (socket::socket_error const& e) {
        EXPECT_EQ(e.error(), ECONNREFUSED);
    }
}

TEST(uds_seqpacket_socket, a_name_too_long_for_sun_path_throws) {
    namespace socket = everest::lib::io::socket;
    std::string const too_long(200, 'x');

    try {
        auto listener = socket::open_uds_seqpacket_server_socket(too_long, true);
        FAIL() << "a name past sun_path must throw";
    } catch (socket::socket_error const& e) {
        EXPECT_EQ(e.error(), ENAMETOOLONG);
    }
}

TEST(uds_seqpacket_socket, listener_refuses_a_path_that_is_not_a_socket) {
    namespace socket = everest::lib::io::socket;
    auto const path = unique_path("seqpacket_not_a_socket");
    {
        std::ofstream file(path);
        file << "somebody's data";
    }

    try {
        auto listener = socket::open_uds_seqpacket_server_socket(path, false);
        FAIL() << "a regular file must not be replaced by a socket";
    } catch (socket::socket_error const& e) {
        EXPECT_EQ(e.error(), EEXIST);
    }
    EXPECT_TRUE(path_exists(path));
    ::unlink(path.c_str());
}

TEST(uds_seqpacket_socket, listener_refuses_a_path_a_live_listener_holds) {
    namespace socket = everest::lib::io::socket;
    auto const path = unique_path("seqpacket_live");

    auto first = socket::open_uds_seqpacket_server_socket(path, false);
    try {
        auto second = socket::open_uds_seqpacket_server_socket(path, false);
        FAIL() << "a live listener must not be unlinked from under its owner";
    } catch (socket::socket_error const& e) {
        EXPECT_EQ(e.error(), EADDRINUSE);
    }
    // The winner is still reachable.
    auto client = socket::open_uds_seqpacket_client_socket(path, false);
    EXPECT_GE(static_cast<int>(client), 0);
    first.close();
    ::unlink(path.c_str());
}

TEST(uds_seqpacket_socket, both_ends_are_non_blocking_and_close_on_exec) {
    namespace socket = everest::lib::io::socket;
    auto const server_name = unique_name("seqpacket_flags");

    auto listener = socket::open_uds_seqpacket_server_socket(server_name, true);
    auto client = socket::open_uds_seqpacket_client_socket(server_name, true);

    // The listener was always both; the client must match it, so a caller driving the pair from
    // one event loop never blocks in a send or a connect on either side.
    EXPECT_NE(::fcntl(listener, F_GETFL) & O_NONBLOCK, 0);
    EXPECT_NE(::fcntl(client, F_GETFL) & O_NONBLOCK, 0);
    EXPECT_NE(::fcntl(listener, F_GETFD) & FD_CLOEXEC, 0);
    EXPECT_NE(::fcntl(client, F_GETFD) & FD_CLOEXEC, 0);

    // Connected, not connecting: a unix connect completes in the call.
    int peer = ::accept(listener, nullptr, nullptr);
    EXPECT_GE(peer, 0);
    ::close(peer);
}
