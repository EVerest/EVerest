// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include <arpa/inet.h>
#include <dirent.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <iso15118/detail/io/socket_helper.hpp>

namespace {

// the scan itself holds an fd, but both invocations scan identically so the
// transient cancels out on compare
int count_open_fds() {
    auto* dir = opendir("/proc/self/fd");
    REQUIRE(dir != nullptr);

    int count = 0;
    while (auto* entry = readdir(dir)) {
        if (strcmp(entry->d_name, ".") == 0 or strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        ++count;
    }
    closedir(dir);
    return count;
}

} // namespace

TEST_CASE("socket_helper: create_tcp_listen_socket binds and listens") {
    sockaddr_in6 address{};
    address.sin6_family = AF_INET6;
    address.sin6_addr = in6addr_loopback;

    const auto fd = iso15118::io::create_tcp_listen_socket(address, /*port=*/0, /*backlog=*/4, "lo");
    REQUIRE(fd >= 0);

    sockaddr_in6 bound_address{};
    socklen_t bound_address_len = sizeof(bound_address);
    REQUIRE(getsockname(fd, reinterpret_cast<sockaddr*>(&bound_address), &bound_address_len) == 0);
    REQUIRE(ntohs(bound_address.sin6_port) != 0);

    // a successful loopback connect proves the socket is in listen state
    const auto client_fd = socket(AF_INET6, SOCK_STREAM, 0);
    REQUIRE(client_fd >= 0);
    const auto connect_result =
        connect(client_fd, reinterpret_cast<const sockaddr*>(&bound_address), sizeof(bound_address));
    CHECK(connect_result == 0);

    close(client_fd);
    close(fd);
}

TEST_CASE("socket_helper: create_tcp_listen_socket closes the fd when setup fails") {
    // an AF_INET address family on an AF_INET6 socket makes bind fail deterministically
    sockaddr_in6 address{};
    address.sin6_family = AF_INET;
    address.sin6_addr = in6addr_loopback;

    // repeated so that one leaked fd per call outweighs any descriptor opened
    // lazily in between, the logger being the likeliest
    constexpr int FAILING_CALL_COUNT = 20;
    constexpr int NOISE_ALLOWANCE = 1;

    const auto fds_before = count_open_fds();
    for (int i = 0; i < FAILING_CALL_COUNT; ++i) {
        REQUIRE_THROWS(iso15118::io::create_tcp_listen_socket(address, /*port=*/0, /*backlog=*/4, "lo"));
    }
    const auto fds_after = count_open_fds();

    REQUIRE(fds_after - fds_before <= NOISE_ALLOWANCE);
}

TEST_CASE("socket_helper: write_all completes a backpressured write once the peer drains") {
    int fds[2];
    REQUIRE(socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, fds) == 0);

    // Shrink the send buffer so a multi-buffer payload reliably hits EAGAIN mid-write.
    int sndbuf = 4096;
    REQUIRE(setsockopt(fds[0], SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf)) == 0);

    constexpr size_t PAYLOAD_SIZE = 1024 * 1024;
    std::vector<uint8_t> payload(PAYLOAD_SIZE, 0xa5);

    std::atomic<size_t> drained{0};
    std::thread reader([&]() {
        std::array<uint8_t, 65536> buf{};
        while (drained.load() < PAYLOAD_SIZE) {
            const auto r = ::read(fds[1], buf.data(), buf.size());
            if (r > 0) {
                drained.fetch_add(static_cast<size_t>(r));
            } else if (r == -1 and (errno == EAGAIN or errno == EWOULDBLOCK)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            } else {
                break;
            }
        }
    });

    const auto ok = iso15118::io::write_all(fds[0], payload.data(), payload.size(), /*timeout_ms=*/5000);
    reader.join();

    CHECK(ok);
    CHECK(drained.load() == PAYLOAD_SIZE);

    close(fds[0]);
    close(fds[1]);
}

TEST_CASE("socket_helper: write_all fails with ETIMEDOUT when the peer never drains") {
    int fds[2];
    REQUIRE(socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, fds) == 0);

    int sndbuf = 4096;
    REQUIRE(setsockopt(fds[0], SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf)) == 0);

    // Larger than send + receive buffer together, so the write can never complete.
    std::vector<uint8_t> payload(1024 * 1024, 0xa5);

    errno = 0;
    const auto ok = iso15118::io::write_all(fds[0], payload.data(), payload.size(), /*timeout_ms=*/100);

    CHECK_FALSE(ok);
    CHECK(errno == ETIMEDOUT);

    close(fds[0]);
    close(fds[1]);
}

TEST_CASE("socket_helper: write_all surfaces a closed peer as EPIPE instead of raising SIGPIPE") {
    int fds[2];
    REQUIRE(socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, fds) == 0);
    close(fds[1]);

    // The first write may be swallowed by the send buffer before the kernel notices the closed peer.
    std::array<uint8_t, 16> payload{};
    errno = 0;
    bool ok = iso15118::io::write_all(fds[0], payload.data(), payload.size(), /*timeout_ms=*/100);
    if (ok) {
        ok = iso15118::io::write_all(fds[0], payload.data(), payload.size(), /*timeout_ms=*/100);
    }

    CHECK_FALSE(ok);
    CHECK(errno == EPIPE);

    close(fds[0]);
}

TEST_CASE("socket_helper: sockaddr_in6_to_name formats the address like getnameinfo(NI_NUMERICHOST)") {
    const auto loopback_index = if_nametoindex("lo");
    REQUIRE(loopback_index != 0);
    constexpr std::uint32_t no_such_interface = 9999;
    char scope_name[IF_NAMESIZE];
    REQUIRE(if_indextoname(no_such_interface, scope_name) == nullptr);

    struct Case {
        const char* address;
        std::uint32_t scope_id;
        std::string expected;
    };
    const auto loopback_number = "%" + std::to_string(loopback_index);
    const std::vector<Case> cases{
        {"2001:db8::1", 0, "2001:db8::1"},
        // link-local: the interface name, or the index if there is no such interface
        {"fe80::1", loopback_index, "fe80::1%lo"},
        {"fe80::1", no_such_interface, "fe80::1%9999"},
        {"ff02::1", loopback_index, "ff02::1%lo"},
        // any other scope: always the index
        {"2001:db8::1", loopback_index, "2001:db8::1" + loopback_number},
        {"ff05::1", loopback_index, "ff05::1" + loopback_number},
    };

    for (auto const& c : cases) {
        INFO(c.address << " with scope " << c.scope_id);
        sockaddr_in6 address{};
        address.sin6_family = AF_INET6;
        address.sin6_scope_id = c.scope_id;
        REQUIRE(inet_pton(AF_INET6, c.address, &address.sin6_addr) == 1);

        char reference[NI_MAXHOST];
        REQUIRE(getnameinfo(reinterpret_cast<const sockaddr*>(&address), sizeof(address), reference, sizeof(reference),
                            nullptr, 0, NI_NUMERICHOST) == 0);

        const auto name = iso15118::io::sockaddr_in6_to_name(address);
        REQUIRE(name != nullptr);
        CHECK(std::string(name.get()) == reference);
        CHECK(std::string(name.get()) == c.expected);
    }
}
