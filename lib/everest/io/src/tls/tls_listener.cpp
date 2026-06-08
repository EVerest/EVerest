// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest

#include <everest/io/socket/socket.hpp>
#include <everest/io/tls/tls_listener.hpp>

#include <arpa/inet.h>
#include <cerrno>
#include <cstdint>
#include <netdb.h>
#include <netinet/in.h>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

namespace everest::lib::io::tls {

namespace {

std::uint16_t port_from_storage(const sockaddr_storage& ss) {
    if (ss.ss_family == AF_INET6) {
        return ntohs(reinterpret_cast<const sockaddr_in6*>(&ss)->sin6_port);
    }
    if (ss.ss_family == AF_INET) {
        return ntohs(reinterpret_cast<const sockaddr_in*>(&ss)->sin_port);
    }
    return 0;
}

} // namespace

tls_listener::tls_listener(Config cfg) {
    m_listen_fd = socket::open_tcp_server_socket(cfg.bind_addr, cfg.bind_port, cfg.ipv6_only);
    const int fd = static_cast<int>(m_listen_fd);

    sockaddr_storage bound{};
    socklen_t bound_len = sizeof(bound);
    if (::getsockname(fd, reinterpret_cast<sockaddr*>(&bound), &bound_len) == 0) {
        m_listen_port = port_from_storage(bound);
    }

    // Hand the listen fd to the libtls Server so wrap_accepted_fd works without the
    // Server attempting its own bind/listen. The Server borrows the fd number only;
    // it does NOT take ownership (it never runs its blocking serve loop here, which
    // is the only path that would close it). m_listen_fd (unique_fd) stays the sole
    // owner, so there is no double-close on teardown.
    cfg.tls.socket = fd;
    const auto state = m_server.init(cfg.tls, nullptr);
    if (state == ::tls::Server::state_t::init_needed) {
        throw std::runtime_error("tls_listener: tls::Server::init failed");
    }
}

event::sync_status tls_listener::sync() {
    sockaddr_storage peer{};
    socklen_t peer_len = sizeof(peer);

    // SOCK_NONBLOCK is essential: the handshake is stepped non-blockingly
    // (accept(0)) on the loop; a blocking socket would stall SSL_accept (and the
    // whole loop) until the peer sends data.
    const int accepted = ::accept4(static_cast<int>(m_listen_fd), reinterpret_cast<sockaddr*>(&peer), &peer_len,
                                   SOCK_CLOEXEC | SOCK_NONBLOCK);

    if (accepted < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            return event::sync_status::ok;
        }
        return event::sync_status::error;
    }

    char ip_buf[NI_MAXHOST] = {};
    if (::getnameinfo(reinterpret_cast<sockaddr*>(&peer), peer_len, ip_buf, sizeof(ip_buf), nullptr, 0,
                      NI_NUMERICHOST) != 0) {
        // Peer IP is informational only (never read by wrap_accepted_fd); leave ip_buf empty.
        ip_buf[0] = '\0';
    }

    const std::uint16_t peer_port = port_from_storage(peer);

    // wrap_accepted_fd's key-logging path runs std::stoul(service); an empty string would throw
    // std::invalid_argument and kill the accept loop, so always pass a numeric service string.
    const std::string svc = std::to_string(peer_port);
    auto raw_conn = m_server.wrap_accepted_fd(accepted, ip_buf, svc.c_str());
    if (!raw_conn) {
        ::close(accepted);
        return event::sync_status::error;
    }

    if (m_cb) {
        auto srv = std::make_unique<tls_server>(std::move(raw_conn));
        m_cb(std::move(srv), std::string(ip_buf), peer_port);
    }

    return event::sync_status::ok;
}

} // namespace everest::lib::io::tls
