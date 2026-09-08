// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#include <everest/io/socket/socket.hpp>
#include <everest/io/uds/uds_seqpacket_listener.hpp>

#include <cerrno>
#include <fcntl.h>
#include <memory>
#include <sys/socket.h>
#include <unistd.h>
#include <utility>

namespace everest::lib::io::uds {

namespace {

// A descriptor worth nothing in itself, kept only to be released when accept runs out.
event::unique_fd open_reserve_fd() {
    return event::unique_fd(::open("/dev/null", O_RDONLY | O_CLOEXEC));
}

} // namespace

uds_seqpacket_listener::uds_seqpacket_listener(std::string const& name, bool is_abstract, std::optional<mode_t> mode) :
    m_listen_fd(socket::open_uds_seqpacket_server_socket(name, is_abstract, mode)),
    m_bound_path(is_abstract ? std::string{} : name) {
    // Claimed while descriptors are still available, because the point of it is to be spendable
    // once they are not. A failure here is not fatal: the listener works, it just cannot shed.
    m_reserve_fd = open_reserve_fd();
}

uds_seqpacket_listener::~uds_seqpacket_listener() {
    unregister_recorded_events();
    if (not m_bound_path.empty()) {
        // Unlinked while still listening: a newcomer probing the path is refused with EADDRINUSE
        // instead of finding a stale file, rebinding, and losing its file to our unlink.
        ::unlink(m_bound_path.c_str());
    }
    m_listen_fd.close();
}

void uds_seqpacket_listener::set_accept_callback(accept_cb cb) {
    m_cb = std::move(cb);
}

void uds_seqpacket_listener::set_error_handler(error_cb cb) {
    m_error = std::move(cb);
}

int uds_seqpacket_listener::get_poll_fd() {
    return static_cast<int>(m_listen_fd);
}

void uds_seqpacket_listener::report_error(int code, std::string const& what) {
    if (m_error and code != 0) {
        m_error(code, what);
    }
}

void uds_seqpacket_listener::shed_queued_connection(int accept_errno) {
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

event::sync_status uds_seqpacket_listener::sync() {
    // Non blocking and close-on-exec like everything this library opens; the peer inherits both.
    const int accepted = ::accept4(static_cast<int>(m_listen_fd), nullptr, nullptr, SOCK_CLOEXEC | SOCK_NONBLOCK);
    if (accepted < 0) {
        const int err = errno;
        if (err == EAGAIN or err == EWOULDBLOCK or err == EINTR) {
            return event::sync_status::ok;
        }
        // These leave the connection queued, so the listening descriptor stays readable and the
        // loop would wake on it again immediately. Every other errno consumes it.
        if (err == EMFILE or err == ENFILE or err == ENOBUFS or err == ENOMEM) {
            shed_queued_connection(err);
        } else {
            report_error(err, "accept failed");
        }
        return event::sync_status::error;
    }

    auto connection = std::make_shared<event::unique_fd>(accepted);
    if (not m_cb) {
        // Nobody to hand it to. Closing is the honest answer; the peer sees EOF.
        return event::sync_status::ok;
    }
    m_cb(std::make_unique<uds_seqpacket_peer>(std::move(connection)));
    return event::sync_status::ok;
}

} // namespace everest::lib::io::uds
