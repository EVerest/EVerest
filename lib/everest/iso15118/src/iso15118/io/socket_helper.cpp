// SPDX-License-Identifier: Apache-2.0
// Copyright 2023 Pionix GmbH and Contributors to EVerest
#include <iso15118/detail/io/socket_helper.hpp>

#include <cerrno>
#include <chrono>
#include <cstring>
#include <utility>

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <iso15118/detail/helper.hpp>

namespace iso15118::io {

namespace {

auto choose_first_ipv6_interface() {
    std::string interface_name{};
    struct ifaddrs* if_list_head;
    const auto get_if_addrs_result = getifaddrs(&if_list_head);

    if (get_if_addrs_result == -1) {
        logf_error("Failed to call getifaddrs");
        return std::string("");
    }

    for (auto current_if = if_list_head; current_if != nullptr; current_if = current_if->ifa_next) {
        if (current_if->ifa_addr == nullptr or current_if->ifa_addr->sa_family != AF_INET6) {
            continue;
        }

        // NOTE (aw): because we did the check for AF_INET6, we can assume that ifa_addr is indeed an sockaddr_in6
        const auto current_addr = reinterpret_cast<const sockaddr_in6*>(current_if->ifa_addr);
        if (not IN6_IS_ADDR_LINKLOCAL(&(current_addr->sin6_addr))) {
            continue;
        }
        interface_name = current_if->ifa_name;
        break; // Stop the loop if a interface is found
    }
    freeifaddrs(if_list_head);

    return interface_name;
}

// owns an fd until released, so that a throwing setup path cannot leak it
class fd_guard {
public:
    explicit fd_guard(int fd) : fd_{fd} {
    }
    fd_guard(const fd_guard&) = delete;
    fd_guard& operator=(const fd_guard&) = delete;
    ~fd_guard() {
        if (fd_ != -1) {
            ::close(fd_);
        }
    }
    int release() {
        return std::exchange(fd_, -1);
    }

private:
    int fd_;
};

} // namespace

bool check_and_update_interface(std::string& interface_name) {

    if (interface_name == "auto") {
        logf_info("Search for the first available ipv6 interface");
        interface_name = choose_first_ipv6_interface();
    }

    struct ipv6_mreq mreq {};
    mreq.ipv6mr_interface = if_nametoindex(interface_name.c_str());
    if (!mreq.ipv6mr_interface) {
        logf_error("No such interface: %s", interface_name.c_str());
        return false;
    }
    return not interface_name.empty();
}

bool get_first_sockaddr_in6_for_interface(const std::string& interface_name, sockaddr_in6& address) {
    struct ifaddrs* if_list_head;
    const auto get_if_addrs_result = getifaddrs(&if_list_head);

    if (get_if_addrs_result == -1) {
        log_and_throw("Failed to call getifaddrs");
    }

    bool found_interface = false;

    for (auto current_if = if_list_head; current_if != nullptr; current_if = current_if->ifa_next) {
        if (current_if->ifa_addr == nullptr) {
            continue;
        }

        if (current_if->ifa_addr->sa_family != AF_INET6) {
            continue;
        }

        if (interface_name.compare("auto") != 0 && interface_name.compare(current_if->ifa_name) != 0) {
            continue;
        }

        // NOTE (aw): because we did the check for AF_INET6, we can assume that ifa_addr is indeed an sockaddr_in6
        const auto current_addr = reinterpret_cast<const sockaddr_in6*>(current_if->ifa_addr);

        // NOTE (sl): If using loopback device, accept any address. Loopback usually does not have a link local address
        if (interface_name.compare("lo") != 0 and not IN6_IS_ADDR_LINKLOCAL(&(current_addr->sin6_addr))) {
            continue;
        }

        if (interface_name == "auto") {
            logf_info("Found an ipv6 link local address for interface: %s", current_if->ifa_name);
        }

        memcpy(&address, current_addr, sizeof(address));
        found_interface = true;
        break; // Stop the loop if a interface is found
    }

    freeifaddrs(if_list_head);

    // Todo(sl): What to do if interface was not found?
    return found_interface;
}

bool set_tcp_keepalive(int fd) {
    constexpr int TCP_KEEPALIVE_IDLE_S = 10;
    constexpr int TCP_KEEPALIVE_INTERVAL_S = 3;
    constexpr int TCP_KEEPALIVE_PROBE_COUNT = 3;

    int enable = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &enable, sizeof(enable)) == -1) {
        logf_error("Failed to enable SO_KEEPALIVE");
        return false;
    }

    int idle = TCP_KEEPALIVE_IDLE_S;
    if (setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &idle, sizeof(idle)) == -1) {
        logf_error("Failed to set TCP_KEEPIDLE");
        return false;
    }

    int interval = TCP_KEEPALIVE_INTERVAL_S;
    if (setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &interval, sizeof(interval)) == -1) {
        logf_error("Failed to set TCP_KEEPINTVL");
        return false;
    }

    int count = TCP_KEEPALIVE_PROBE_COUNT;
    if (setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &count, sizeof(count)) == -1) {
        logf_error("Failed to set TCP_KEEPCNT");
        return false;
    }

    return true;
}

AcceptResult accept_connection(int listen_fd, sockaddr_in6& peer_address) {
    socklen_t address_len = sizeof(peer_address);

    // SOCK_NONBLOCK: the poll loop is shared (SDP server + connections), so nothing downstream may
    // ever block on this fd -- the plain read() path and the TLS handshake/read paths all rely on it.
    const auto accept_fd =
        ::accept4(listen_fd, reinterpret_cast<sockaddr*>(&peer_address), &address_len, SOCK_NONBLOCK);

    if (accept_fd == -1) {
        // A client that connects and RSTs quickly (e.g. a port scan) yields ECONNABORTED; EINTR and a
        // spurious wakeup are equally transient. The listener stays usable, so the caller should just
        // wait for the next connection.
        if (errno == EINTR or errno == EAGAIN or errno == EWOULDBLOCK or errno == ECONNABORTED) {
            logf_warning("accept4 failed with a transient error code: %d", errno);
            return {AcceptResult::Status::Transient, -1};
        }
        // A hard accept failure (e.g. EMFILE): the caller must contain this by tearing down its own
        // connection instead of letting an exception escape the poll callback.
        //
        // Deliberate deviation from accept(2)'s advice to retry the pending-connection network
        // errnos (EPROTO, ENETDOWN, EHOSTUNREACH, ENETUNREACH, ENONET, EOPNOTSUPP): this listener
        // serves exactly one EV over a point-to-point link-local connection, so a pending
        // connection that died of a network error means the link itself is gone, and dlink-loss
        // handling -- not an accept retry -- is the correct recovery.
        logf_error("accept4 failed with error code: %d", errno);
        return {AcceptResult::Status::Fatal, -1};
    }

    // Dead-peer detection: the read() paths rely on the keepalive's ETIMEDOUT to tear down a
    // session whose peer vanished without FIN/RST.
    if (not set_tcp_keepalive(accept_fd)) {
        logf_warning("Failed to configure TCP keepalive on the accepted connection");
    }

    return {AcceptResult::Status::Accepted, accept_fd};
}

bool write_all(int fd, const uint8_t* buf, size_t len, int timeout_ms) {
    using clock = std::chrono::steady_clock;
    const auto deadline = clock::now() + std::chrono::milliseconds(timeout_ms);

    size_t written = 0;
    while (written < len) {
        // MSG_NOSIGNAL: a peer that closed mid-write must surface as EPIPE, not kill the process
        // with SIGPIPE.
        const auto write_result = ::send(fd, buf + written, len - written, MSG_NOSIGNAL);

        if (write_result >= 0) {
            written += static_cast<size_t>(write_result);
            continue;
        }

        if (errno != EINTR and errno != EAGAIN and errno != EWOULDBLOCK) {
            return false;
        }

        const auto now = clock::now();
        if (now >= deadline) {
            errno = ETIMEDOUT;
            return false;
        }
        const auto remaining_ms = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();

        pollfd pfd{fd, POLLOUT, 0};
        const auto poll_result = ::poll(&pfd, 1, static_cast<int>(remaining_ms));
        if (poll_result == -1 and errno != EINTR) {
            return false;
        }
        // poll_result == 0 (timed out): the deadline check at the top of the next iteration
        // converts it into ETIMEDOUT after one final write attempt.
    }

    return true;
}

int create_tcp_listen_socket(sockaddr_in6 address, uint16_t port, int backlog, const std::string& interface_name) {
    // SOCK_NONBLOCK: accept_connection()'s Transient/EAGAIN contract depends on it. A pending
    // connection the client aborts between poll() reporting the listener readable and accept4()
    // running is silently dropped from the backlog; a blocking accept4 would then stall the shared
    // controller poll loop (SDP server, session timers) until the next TCP connect arrives.
    const auto fd = socket(AF_INET6, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (fd == -1) {
        log_and_throw("Failed to create an ipv6 socket");
    }
    fd_guard guard{fd};

    address.sin6_port = htons(port);

    int optval_tmp{1};
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &optval_tmp, sizeof(optval_tmp)) == -1) {
        log_and_throw("setsockopt(SO_REUSEADDR) failed");
    }

    if (setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &optval_tmp, sizeof(optval_tmp)) == -1) {
        log_and_throw("setsockopt(SO_REUSEPORT) failed");
    }

    if (bind(fd, reinterpret_cast<const struct sockaddr*>(&address), sizeof(address)) == -1) {
        const auto msg = "Failed to bind ipv6 socket to interface " + interface_name;
        log_and_throw(msg.c_str());
    }

    if (listen(fd, backlog) == -1) {
        log_and_throw("Listen on socket failed");
    }

    return guard.release();
}

std::unique_ptr<char[]> sockaddr_in6_to_name(const sockaddr_in6& address) {
    // account for ipv6 address string length plus possible scope/zone
    // identifier which seems to be an interface name, as both constants
    // (INET6_ADDRSTRLEN and IFNAMSIZ) include the terminating NULL, we
    // have one extra character that can account for the separating '%'
    // between the ipv6 address and the scope/zone identifier
    static constexpr auto MAX_NUMERIC_NAME_LENGTH = INET6_ADDRSTRLEN + IFNAMSIZ;
    auto name = std::make_unique<char[]>(MAX_NUMERIC_NAME_LENGTH);

    // FIXME (aw): what about alignment issues here between casting from sockaddr_in6 to sockaddr?
    const auto result = getnameinfo(reinterpret_cast<const sockaddr*>(&address), sizeof(address), name.get(),
                                    MAX_NUMERIC_NAME_LENGTH, nullptr, 0, NI_NUMERICHOST);

    if (result == 0) {
        return name;
    } else {
        return nullptr;
    }
}
} // namespace iso15118::io
