// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
//
// Loopback-only fixtures: no test here depends on DNS or on egress.

#include <everest/io/event/unique_fd.hpp>
#include <everest/io/socket/socket.hpp>
#include <everest/io/tcp/tcp_socket.hpp>

#include <array>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <random>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

#include <arpa/inet.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sched.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include <gtest/gtest.h>

using everest::lib::io::event::unique_fd;
using everest::lib::io::socket::get_interface_address;
using everest::lib::io::socket::open_first_address;
using everest::lib::io::socket::open_tcp_server_socket;
using everest::lib::io::socket::socket_error;
using everest::lib::io::tcp::SourcePortRange;
using everest::lib::io::tcp::tcp_socket;

namespace {

// An arbitrary high range; what is under test is that the socket binds inside whatever
// range it is given, not the particular numbers.
constexpr SourcePortRange test_ports{49152, 65535};

std::uint16_t port_of(int fd) {
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

// A listening loopback socket on an ephemeral port, never accept()ed: the kernel completes the
// handshake from the backlog.
class loopback_listener {
public:
    loopback_listener(std::string const& address, bool ipv6) : m_fd(open_tcp_server_socket(address, 0, ipv6)) {
        m_port = port_of(m_fd);
    }

    std::uint16_t port() const {
        return m_port;
    }

    int fd() const {
        return m_fd;
    }

private:
    unique_fd m_fd;
    std::uint16_t m_port{0};
};

// @p count consecutive TCP ports, each held by a socket bound to the IPv4 wildcard address.
// Below the kernel's default ephemeral range, so no outgoing connection takes one meanwhile.
// Empty when no free run was found.
std::vector<unique_fd> bind_port_run(std::size_t count) {
    std::mt19937 generator{std::random_device{}()};
    std::uniform_int_distribution<std::uint32_t> base_of(20000, 30000);
    for (int attempt = 0; attempt < 32; ++attempt) {
        const auto base = base_of(generator);
        std::vector<unique_fd> held;
        for (std::size_t i = 0; i < count; ++i) {
            unique_fd fd(::socket(AF_INET, SOCK_STREAM, 0));
            sockaddr_in local{};
            local.sin_family = AF_INET;
            local.sin_addr.s_addr = htonl(INADDR_ANY);
            local.sin_port = htons(static_cast<std::uint16_t>(base + i));
            if (fd == -1 or ::bind(fd, reinterpret_cast<sockaddr*>(&local), sizeof(local)) != 0) {
                break;
            }
            held.push_back(std::move(fd));
        }
        if (held.size() == count) {
            return held;
        }
    }
    return {};
}

// Closes @p client first and its accepted peer second, which leaves the client's local port in
// TIME_WAIT towards @p listener.
void close_into_time_wait(tcp_socket& client, loopback_listener const& listener) {
    pollfd ready{listener.fd(), POLLIN, 0};
    ASSERT_EQ(::poll(&ready, 1, 1000), 1);
    unique_fd peer(::accept(listener.fd(), nullptr, nullptr));
    ASSERT_NE(peer, -1);
    client.close();
    pollfd eof{peer, POLLIN, 0};
    ASSERT_EQ(::poll(&eof, 1, 1000), 1);
    char byte{};
    ASSERT_EQ(::recv(peer, &byte, sizeof(byte), 0), 0);
}

// Closes @p client with a reset, so its 4-tuple is free again at once.
void abort_connection(tcp_socket& client) {
    linger const reset{1, 0};
    ASSERT_EQ(::setsockopt(client.get_fd(), SOL_SOCKET, SO_LINGER, &reset, sizeof(reset)), 0);
    client.close();
}

bool write_file(char const* path, std::string const& content) {
    std::ofstream out(path);
    out << content;
    out.flush();
    return static_cast<bool>(out);
}

constexpr int namespaces_denied{77};

// Runs @p body in a forked child, in a new user and network namespace with lo up and TCP
// timestamps off. Without timestamps the kernel keeps a TIME_WAIT tuple from being reused, as
// it does towards a peer that sends none. Returns the child's exit status: 0 when @p body
// recorded no failure, namespaces_denied when the namespace could not be set up.
int run_without_tcp_timestamps(std::function<void()> const& body) {
    std::fflush(nullptr);
    const auto uid = ::getuid();
    const auto gid = ::getgid();
    const pid_t child = ::fork();
    if (child == 0) {
        ifreq lo{};
        std::strncpy(lo.ifr_name, "lo", IFNAMSIZ - 1);
        const bool ready = ::unshare(CLONE_NEWUSER | CLONE_NEWNET) == 0 and
                           write_file("/proc/self/setgroups", "deny") and
                           write_file("/proc/self/uid_map", "0 " + std::to_string(uid) + " 1") and
                           write_file("/proc/self/gid_map", "0 " + std::to_string(gid) + " 1") and
                           write_file("/proc/sys/net/ipv4/tcp_timestamps", "0");
        const int control = ::socket(AF_INET, SOCK_DGRAM, 0);
        if (not ready or control == -1 or ::ioctl(control, SIOCGIFFLAGS, &lo) != 0) {
            ::_exit(namespaces_denied);
        }
        lo.ifr_flags |= IFF_UP;
        if (::ioctl(control, SIOCSIFFLAGS, &lo) != 0) {
            ::_exit(namespaces_denied);
        }
        ::close(control);
        // The child must not unwind into the gtest run it was forked from.
        try {
            body();
        } catch (std::exception const& e) {
            ADD_FAILURE() << "exception in the namespace child: " << e.what();
        } catch (...) {
            ADD_FAILURE() << "exception in the namespace child";
        }
        std::fflush(nullptr);
        ::_exit(::testing::Test::HasFailure() ? 1 : 0);
    }
    int status{0};
    if (child == -1 or ::waitpid(child, &status, 0) != child or not WIFEXITED(status)) {
        return -1;
    }
    return WEXITSTATUS(status);
}

} // namespace

TEST(tcp_bind_options_test, source_port_range_binds_the_local_port_within_the_range_v4) {
    loopback_listener listener("127.0.0.1", false);
    ASSERT_NE(listener.port(), 0);

    tcp_socket sock;
    ASSERT_TRUE(sock.open("127.0.0.1", listener.port(), {}, test_ports));

    const auto local_port = port_of(sock.get_fd());
    EXPECT_GE(local_port, test_ports.min);
    EXPECT_LE(local_port, test_ports.max);
}

TEST(tcp_bind_options_test, source_port_range_binds_the_local_port_within_the_range_v6) {
    loopback_listener listener("::1", true);
    ASSERT_NE(listener.port(), 0);

    tcp_socket sock;
    ASSERT_TRUE(sock.open("::1", listener.port(), {}, test_ports));

    const auto local_port = port_of(sock.get_fd());
    EXPECT_GE(local_port, test_ports.min);
    EXPECT_LE(local_port, test_ports.max);
}

// Without the option the kernel picks, which is what every existing caller gets.
TEST(tcp_bind_options_test, without_a_source_port_range_the_connect_still_succeeds) {
    loopback_listener listener("127.0.0.1", false);
    ASSERT_NE(listener.port(), 0);

    tcp_socket sock;
    ASSERT_TRUE(sock.open("127.0.0.1", listener.port()));
    EXPECT_NE(port_of(sock.get_fd()), 0);
}

// A range of one already taken port: the retry loop must end and report the failure.
TEST(tcp_bind_options_test, an_exhausted_source_port_range_fails_the_open) {
    loopback_listener listener("127.0.0.1", false);
    ASSERT_NE(listener.port(), 0);
    loopback_listener occupied("127.0.0.1", false);
    ASSERT_NE(occupied.port(), 0);

    tcp_socket sock;
    const SourcePortRange single{occupied.port(), occupied.port()};
    EXPECT_FALSE(sock.open("127.0.0.1", listener.port(), {}, single));
    EXPECT_NE(sock.get_error(), 0);
}

// One free port among 32 taken ones: the walk reaches it on every open, whatever its start.
TEST(tcp_bind_options_test, a_narrow_range_with_one_free_port_binds_it_every_time) {
    auto taken = bind_port_run(32);
    ASSERT_EQ(taken.size(), 32u);
    const auto base = port_of(taken.front());
    const auto free_port = static_cast<std::uint16_t>(base + 17);
    taken[17].close();
    const SourcePortRange range{base, static_cast<std::uint16_t>(base + 31)};

    for (int run = 0; run < 50; ++run) {
        // A fresh peer per run: the previous run's connection on the free port lingers towards
        // its own peer only.
        loopback_listener listener("127.0.0.1", false);
        ASSERT_NE(listener.port(), 0);
        tcp_socket sock;
        ASSERT_TRUE(sock.open("127.0.0.1", listener.port(), {}, range))
            << "run " << run << ": " << std::strerror(sock.get_error());
        EXPECT_EQ(port_of(sock.get_fd()), free_port);
    }
}

// A one-port range whose port sits in TIME_WAIT towards the same peer: the bind succeeds, and
// only the connect may refuse the 4-tuple that is still taken.
TEST(tcp_bind_options_test, a_port_in_time_wait_never_fails_the_bind) {
    auto held = bind_port_run(1);
    ASSERT_EQ(held.size(), 1u);
    const auto port = port_of(held.front());
    held.clear();
    const SourcePortRange single{port, port};
    loopback_listener listener("127.0.0.1", false);
    ASSERT_NE(listener.port(), 0);

    tcp_socket first;
    ASSERT_TRUE(first.open("127.0.0.1", listener.port(), {}, single)) << std::strerror(first.get_error());
    ASSERT_NO_FATAL_FAILURE(close_into_time_wait(first, listener));

    tcp_socket again;
    if (not again.open("127.0.0.1", listener.port(), {}, single)) {
        EXPECT_EQ(again.get_error(), EADDRNOTAVAIL) << std::strerror(again.get_error());
    }
}

// Towards a peer without TCP timestamps a port in TIME_WAIT binds, but its connect fails with
// EADDRNOTAVAIL. A two-port range then connects on the other port. Repeated, so the walk starts
// on the TIME_WAIT port in some rounds. Every round's listener stays open, so no round gets the
// peer port of an earlier one and meets that round's TIME_WAIT.
TEST(tcp_bind_options_test, a_connect_into_time_wait_moves_to_the_next_port) {
    const int status = run_without_tcp_timestamps([] {
        auto held = bind_port_run(2);
        ASSERT_EQ(held.size(), 2u);
        const auto port = port_of(held.front());
        held.clear();
        const SourcePortRange single{port, port};
        const SourcePortRange pair{port, static_cast<std::uint16_t>(port + 1)};
        std::vector<loopback_listener> listeners;
        listeners.reserve(10);

        for (int round = 0; round < 10; ++round) {
            auto const& listener = listeners.emplace_back("127.0.0.1", false);
            ASSERT_NE(listener.port(), 0);
            tcp_socket first;
            ASSERT_TRUE(first.open("127.0.0.1", listener.port(), {}, single)) << std::strerror(first.get_error());
            ASSERT_NO_FATAL_FAILURE(close_into_time_wait(first, listener));

            tcp_socket blocked;
            EXPECT_FALSE(blocked.open("127.0.0.1", listener.port(), {}, single));
            EXPECT_EQ(blocked.get_error(), EADDRNOTAVAIL) << std::strerror(blocked.get_error());

            tcp_socket again;
            ASSERT_TRUE(again.open("127.0.0.1", listener.port(), {}, pair))
                << "round " << round << ": " << std::strerror(again.get_error());
            EXPECT_EQ(port_of(again.get_fd()), pair.max);
        }
    });
    if (status == namespaces_denied) {
        GTEST_SKIP() << "no user and network namespace to turn TCP timestamps off in";
    }
    EXPECT_EQ(status, 0) << "the failures are printed by the child above";
}

// A port whose 4-tuple towards the peer is still taken makes connect() fail with EADDRNOTAVAIL
// after a successful bind. A live connection holds it here. The connect moves on to the next port.
TEST(tcp_bind_options_test, a_connect_refused_on_a_taken_tuple_moves_to_the_next_port) {
    auto held = bind_port_run(2);
    ASSERT_EQ(held.size(), 2u);
    const auto port = port_of(held.front());
    held.clear();
    const SourcePortRange single{port, port};
    const SourcePortRange pair{port, static_cast<std::uint16_t>(port + 1)};

    // Repeated, so the walk starts on the taken port in some rounds.
    for (int round = 0; round < 10; ++round) {
        loopback_listener listener("127.0.0.1", false);
        ASSERT_NE(listener.port(), 0);
        tcp_socket first;
        ASSERT_TRUE(first.open("127.0.0.1", listener.port(), {}, single)) << std::strerror(first.get_error());

        tcp_socket again;
        ASSERT_TRUE(again.open("127.0.0.1", listener.port(), {}, pair))
            << "round " << round << ": " << std::strerror(again.get_error());
        EXPECT_EQ(port_of(again.get_fd()), pair.max);
    }
}

// A connect refused with EADDRNOTAVAIL on port after port stops before the range is walked: 8
// retries. 19 of 20 ports hold a live tuple towards the peer, so an open reaches the
// free port only when the walk starts within 9 ports of it.
TEST(tcp_bind_options_test, consecutive_connect_refusals_stop_before_the_range_is_walked) {
    constexpr std::size_t range_size{20};
    auto held = bind_port_run(range_size);
    ASSERT_EQ(held.size(), range_size);
    const auto base = port_of(held.front());
    held.clear();
    const auto free_port = static_cast<std::uint16_t>(base + 10);
    const SourcePortRange range{base, static_cast<std::uint16_t>(base + range_size - 1)};
    loopback_listener listener("127.0.0.1", false);
    ASSERT_NE(listener.port(), 0);
    std::array<tcp_socket, range_size> holders;
    for (std::uint16_t port = range.min; port <= range.max; ++port) {
        if (port != free_port) {
            ASSERT_TRUE(holders.at(port - base).open("127.0.0.1", listener.port(), {}, SourcePortRange{port, port}));
        }
    }

    int opened{0};
    int refused{0};
    for (int run = 0; run < 50; ++run) {
        tcp_socket sock;
        if (sock.open("127.0.0.1", listener.port(), {}, range)) {
            ++opened;
            EXPECT_EQ(port_of(sock.get_fd()), free_port);
            ASSERT_NO_FATAL_FAILURE(abort_connection(sock));
        } else {
            ++refused;
            EXPECT_EQ(sock.get_error(), EADDRNOTAVAIL) << std::strerror(sock.get_error());
        }
    }
    // Each outcome has a chance of about one in two per run.
    EXPECT_GT(opened, 0);
    EXPECT_GT(refused, 0);
}

// The source-IP fallback needs the device's IPv4 address. A device without one is reported as
// EADDRNOTAVAIL, so a failed connect carries a cause instead of 0.
TEST(tcp_bind_options_test, a_device_without_an_ipv4_address_is_reported_as_eaddrnotavail) {
    try {
        std::ignore = get_interface_address("everest-none0");
        FAIL() << "no failure was reported";
    } catch (socket_error const& e) {
        EXPECT_EQ(e.error(), EADDRNOTAVAIL);
    }
}

// setup() records both options for the connect() that follows.
TEST(tcp_bind_options_test, setup_accepts_a_device_and_a_source_port_range) {
    loopback_listener listener("127.0.0.1", false);
    ASSERT_NE(listener.port(), 0);

    tcp_socket sock;
    EXPECT_TRUE(sock.setup("127.0.0.1", listener.port(), 1000, "lo", test_ports));
}

TEST(tcp_bind_options_test, a_device_bind_to_lo_reaches_a_loopback_peer) {
    loopback_listener listener("127.0.0.1", false);
    ASSERT_NE(listener.port(), 0);

    tcp_socket sock;
    ASSERT_TRUE(sock.open("127.0.0.1", listener.port(), "lo", test_ports));

    const auto local_port = port_of(sock.get_fd());
    EXPECT_GE(local_port, test_ports.min);
    EXPECT_LE(local_port, test_ports.max);
}

namespace {

// Two hand-made IPv4 resolver entries, 127.0.0.2 then 127.0.0.1.
class two_addresses {
public:
    two_addresses() {
        for (std::size_t i = 0; i < m_entries.size(); ++i) {
            m_addrs[i].sin_family = AF_INET;
            m_addrs[i].sin_addr.s_addr = htonl(i == 0 ? 0x7f000002 : INADDR_LOOPBACK);
            m_entries[i].ai_family = AF_INET;
            m_entries[i].ai_socktype = SOCK_STREAM;
            m_entries[i].ai_addr = reinterpret_cast<sockaddr*>(&m_addrs[i]);
            m_entries[i].ai_addrlen = sizeof(sockaddr_in);
        }
        m_entries[0].ai_next = &m_entries[1];
    }

    addrinfo const* list() const {
        return m_entries.data();
    }

private:
    std::array<sockaddr_in, 2> m_addrs{};
    std::array<addrinfo, 2> m_entries{};
};

std::string ip_of(addrinfo const& address) {
    std::array<char, INET_ADDRSTRLEN> text{};
    ::inet_ntop(AF_INET, &reinterpret_cast<sockaddr_in const*>(address.ai_addr)->sin_addr, text.data(), text.size());
    return text.data();
}

} // namespace

// A failure on one resolved address moves on to the next one.
TEST(tcp_bind_options_test, a_failure_on_the_first_address_tries_the_next_one) {
    two_addresses const addresses;
    std::vector<std::string> tried;
    int opened{-1};

    auto fd = open_first_address(addresses.list(), [&](addrinfo const& address) {
        tried.push_back(ip_of(address));
        if (tried.size() == 1) {
            throw socket_error("source port taken", EADDRINUSE);
        }
        unique_fd socket_fd(::socket(AF_INET, SOCK_STREAM, 0));
        opened = socket_fd;
        return socket_fd;
    });

    EXPECT_EQ(tried, (std::vector<std::string>{"127.0.0.2", "127.0.0.1"}));
    EXPECT_GE(opened, 0);
    EXPECT_EQ(static_cast<int>(fd), opened);
}

// Once every address has failed, the last failure is the one reported.
TEST(tcp_bind_options_test, when_every_address_fails_the_last_failure_is_rethrown) {
    two_addresses const addresses;
    int calls{0};

    try {
        std::ignore = open_first_address(addresses.list(), [&](addrinfo const&) -> unique_fd {
            ++calls;
            if (calls == 1) {
                throw socket_error("source port taken", EADDRINUSE);
            }
            throw socket_error("connection refused", ECONNREFUSED);
        });
        FAIL() << "no failure was reported";
    } catch (socket_error const& e) {
        EXPECT_EQ(e.error(), ECONNREFUSED);
        EXPECT_STREQ(e.what(), "connection refused");
    }
    EXPECT_EQ(calls, 2);
}

// A socket_error keeps its errno through the loop even when a later address fails otherwise.
TEST(tcp_bind_options_test, the_last_socket_error_survives_a_later_plain_failure) {
    two_addresses const addresses;

    try {
        std::ignore = open_first_address(addresses.list(), [calls = 0](addrinfo const&) mutable -> unique_fd {
            if (++calls == 1) {
                throw socket_error("connection refused", ECONNREFUSED);
            }
            throw std::runtime_error("no IPv4 address");
        });
        FAIL() << "no failure was reported";
    } catch (socket_error const& e) {
        EXPECT_EQ(e.error(), ECONNREFUSED);
        EXPECT_STREQ(e.what(), "connection refused");
    }
}

// Failures that carry no errno still leave as socket_error, so a caller records a nonzero cause.
TEST(tcp_bind_options_test, plain_failures_on_every_address_are_reported_as_eio) {
    two_addresses const addresses;

    try {
        std::ignore = open_first_address(addresses.list(), [calls = 0](addrinfo const&) mutable -> unique_fd {
            throw std::runtime_error("failure " + std::to_string(++calls));
        });
        FAIL() << "no failure was reported";
    } catch (socket_error const& e) {
        EXPECT_EQ(e.error(), EIO);
        EXPECT_STREQ(e.what(), "failure 2");
    }
}
