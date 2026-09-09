// SPDX-License-Identifier: Apache-2.0
// Copyright 2023 Pionix GmbH and Contributors to EVerest
//
// Platform-independent half of socket_helper.hpp
#include <iso15118/detail/io/socket_helper.hpp>

#include <utility>

#include <arpa/inet.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include <iso15118/detail/helper.hpp>

namespace iso15118::io {

namespace {

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

int create_tcp_listen_socket(sockaddr_in6 address, uint16_t port, int backlog, const std::string& interface_name) {
    const auto fd = socket(AF_INET6, SOCK_STREAM, 0);
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
