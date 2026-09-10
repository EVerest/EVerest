// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest

#include <everest/io/socket/socket.hpp>
#include <everest/io/tls/tls_listener_config.hpp>

#include <arpa/inet.h>
#include <cerrno>
#include <cstdint>
#include <fcntl.h>
#include <memory>
#include <netdb.h>
#include <netinet/in.h>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

namespace everest::lib::io::tls {

namespace {

// A descriptor worth nothing in itself, kept only to be released when accept runs out.
event::unique_fd open_reserve_fd() {
    return event::unique_fd(::open("/dev/null", O_RDONLY | O_CLOEXEC));
}

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

tls_listener::tls_listener(Config cfg) : m_server(std::make_unique<::tls::Server>()) {
    m_listen_fd = socket::open_tcp_server_socket(cfg.bind_addr, cfg.bind_port, cfg.ipv6_only);
    const int fd = static_cast<int>(m_listen_fd);

    sockaddr_storage bound{};
    socklen_t bound_len = sizeof(bound);
    if (::getsockname(fd, reinterpret_cast<sockaddr*>(&bound), &bound_len) == 0) {
        m_listen_port = port_from_storage(bound);
    }

    // The libtls Server borrows the fd number only, so wrap_accepted_fd works without its own
    // bind/listen. Only its blocking serve loop, never run here, would close it, so m_listen_fd
    // stays the sole owner and there is no double-close on teardown.
    cfg.tls.socket = fd;
    const auto state = m_server->init(cfg.tls, nullptr);
    if (state == ::tls::Server::state_t::init_needed) {
        throw std::runtime_error("tls_listener: tls::Server::init failed");
    }

    // Claimed while descriptors are still available, because the point of it is to be spendable
    // once they are not. A failure here is not fatal: the listener works, it just cannot shed.
    m_reserve_fd = open_reserve_fd();
}

tls_listener::~tls_listener() = default;

void tls_listener::report_error(int code, std::string const& what) {
    if (m_error and code != 0) {
        m_error(code, what);
    }
}

void tls_listener::shed_queued_connection(int accept_errno) {
    if (not m_reserve_fd.is_fd()) {
        // Nothing left to spend. The connection stays queued and the loop keeps waking on it, so
        // say so rather than spinning silently.
        report_error(accept_errno, "out of descriptors, cannot accept or shed the queued connection");
        return;
    }
    m_reserve_fd.close();
    const int shed = ::accept4(static_cast<int>(m_listen_fd), nullptr, nullptr, SOCK_CLOEXEC | SOCK_NONBLOCK);
    if (shed >= 0) {
        ::close(shed);
    }
    m_reserve_fd = open_reserve_fd();
    report_error(accept_errno, "out of descriptors, dropped the queued connection to keep the loop live");
}

event::sync_status tls_listener::sync() {
    sockaddr_storage peer{};
    socklen_t peer_len = sizeof(peer);

    // SOCK_NONBLOCK is essential: a blocking socket would stall SSL_accept, and with it the
    // whole loop, until the peer sends data.
    const int accepted = ::accept4(static_cast<int>(m_listen_fd), reinterpret_cast<sockaddr*>(&peer), &peer_len,
                                   SOCK_CLOEXEC | SOCK_NONBLOCK);

    if (accepted < 0) {
        const int err = errno;
        if (err == EAGAIN || err == EWOULDBLOCK || err == EINTR) {
            return event::sync_status::ok;
        }
        // These leave the connection queued, so the listen fd stays readable and the loop would
        // wake on it again immediately. Every other errno consumes it.
        if (err == EMFILE || err == ENFILE || err == ENOBUFS || err == ENOMEM) {
            shed_queued_connection(err);
        } else {
            report_error(err, "accept failed");
        }
        return event::sync_status::error;
    }

    char ip_buf[NI_MAXHOST] = {};
    if (::getnameinfo(reinterpret_cast<sockaddr*>(&peer), peer_len, ip_buf, sizeof(ip_buf), nullptr, 0,
                      NI_NUMERICHOST) != 0) {
        // Peer IP is informational only, never read by wrap_accepted_fd.
        ip_buf[0] = '\0';
    }

    const std::uint16_t peer_port = port_from_storage(peer);

    const std::string svc = std::to_string(peer_port);
    auto raw_conn = m_server->wrap_accepted_fd(accepted, ip_buf, svc.c_str());
    if (!raw_conn) {
        // No BIO took the fd on failure, so this side still owns it.
        ::close(accepted);
        report_error(EPROTO, "could not wrap the accepted connection");
        return event::sync_status::error;
    }

    if (m_cb) {
        auto srv = std::make_unique<tls_server>(std::move(raw_conn));
        m_cb(std::move(srv), std::string(ip_buf), peer_port);
    }

    return event::sync_status::ok;
}

} // namespace everest::lib::io::tls
