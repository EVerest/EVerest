// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest

#include <everest/io/tls/tls_listener.hpp>

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <netdb.h>
#include <netinet/in.h>
#include <stdexcept>
#include <sys/socket.h>
#include <unistd.h>

namespace everest::lib::io::tls {

tls_listener::tls_listener(Config cfg) {
    const bool is_ipv6 = cfg.ipv6_only || (cfg.bind_addr.find(':') != std::string::npos);
    const int family = is_ipv6 ? AF_INET6 : AF_INET;

    const int fd = ::socket(family, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        throw std::runtime_error(std::string("tls_listener socket(): ") + std::strerror(errno));
    }
    m_listen_fd = event::unique_fd(fd);

    int opt = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));

    if (is_ipv6) {
        const int v6only = cfg.ipv6_only ? 1 : 0;
        ::setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &v6only, sizeof(v6only));
    }

    sockaddr_storage sa{};
    socklen_t sa_len = 0;
    if (is_ipv6) {
        auto* sa6 = reinterpret_cast<sockaddr_in6*>(&sa);
        sa6->sin6_family = AF_INET6;
        sa6->sin6_port = htons(cfg.bind_port);
        if (::inet_pton(AF_INET6, cfg.bind_addr.c_str(), &sa6->sin6_addr) != 1) {
            throw std::runtime_error(std::string("tls_listener inet_pton IPv6: ") + cfg.bind_addr);
        }
        sa_len = sizeof(sockaddr_in6);
    } else {
        auto* sa4 = reinterpret_cast<sockaddr_in*>(&sa);
        sa4->sin_family = AF_INET;
        sa4->sin_port = htons(cfg.bind_port);
        if (::inet_pton(AF_INET, cfg.bind_addr.c_str(), &sa4->sin_addr) != 1) {
            throw std::runtime_error(std::string("tls_listener inet_pton IPv4: ") + cfg.bind_addr);
        }
        sa_len = sizeof(sockaddr_in);
    }

    if (::bind(fd, reinterpret_cast<sockaddr*>(&sa), sa_len) != 0) {
        throw std::runtime_error(std::string("tls_listener bind(): ") + std::strerror(errno));
    }

    if (::listen(fd, SOMAXCONN) != 0) {
        throw std::runtime_error(std::string("tls_listener listen(): ") + std::strerror(errno));
    }

    sockaddr_storage bound{};
    socklen_t bound_len = sizeof(bound);
    if (::getsockname(fd, reinterpret_cast<sockaddr*>(&bound), &bound_len) == 0) {
        if (bound.ss_family == AF_INET6) {
            m_listen_port = ntohs(reinterpret_cast<sockaddr_in6*>(&bound)->sin6_port);
        } else {
            m_listen_port = ntohs(reinterpret_cast<sockaddr_in*>(&bound)->sin_port);
        }
    }

    // Hand the listen fd to the libtls Server so wrap_accepted_fd works
    // without the Server attempting its own bind/listen.
    cfg.tls.socket = fd;
    const auto state = m_server.init(cfg.tls, nullptr);
    if (state == ::tls::Server::state_t::init_needed) {
        throw std::runtime_error("tls_listener: tls::Server::init failed");
    }
}

event::sync_status tls_listener::sync() {
    sockaddr_storage peer{};
    socklen_t peer_len = sizeof(peer);

    const int accepted =
        ::accept4(static_cast<int>(m_listen_fd), reinterpret_cast<sockaddr*>(&peer), &peer_len, SOCK_CLOEXEC);

    if (accepted < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            return event::sync_status::ok;
        }
        return event::sync_status::error;
    }

    char ip_buf[NI_MAXHOST] = {};
    char svc_buf[NI_MAXSERV] = {};
    ::getnameinfo(reinterpret_cast<sockaddr*>(&peer), peer_len, ip_buf, sizeof(ip_buf), svc_buf, sizeof(svc_buf),
                  NI_NUMERICHOST | NI_NUMERICSERV);

    std::uint16_t peer_port = 0;
    if (peer.ss_family == AF_INET6) {
        peer_port = ntohs(reinterpret_cast<sockaddr_in6*>(&peer)->sin6_port);
    } else {
        peer_port = ntohs(reinterpret_cast<sockaddr_in*>(&peer)->sin_port);
    }

    auto raw_conn = m_server.wrap_accepted_fd(accepted, ip_buf, svc_buf);
    if (!raw_conn) {
        ::close(accepted);
        return event::sync_status::error;
    }

    if (m_cb) {
        m_cb(std::make_unique<tls_server_connection>(std::move(raw_conn)), std::string(ip_buf), peer_port);
    }

    return event::sync_status::ok;
}

} // namespace everest::lib::io::tls
