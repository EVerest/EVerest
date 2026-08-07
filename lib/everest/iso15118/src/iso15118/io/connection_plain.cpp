// SPDX-License-Identifier: Apache-2.0
// Copyright 2023 Pionix GmbH and Contributors to EVerest
#include <iso15118/io/connection_plain.hpp>

#include <cassert>
#include <cerrno>
#include <cinttypes>
#include <cstring>

#include <arpa/inet.h>
#include <unistd.h>

#include <iso15118/detail/helper.hpp>
#include <iso15118/detail/io/socket_helper.hpp>

namespace iso15118::io {

ConnectionPlain::ConnectionPlain(PollManager& poll_manager_, const std::string& interface_name) :
    poll_manager(poll_manager_) {
    sockaddr_in6 address{};
    if (not get_first_sockaddr_in6_for_interface(interface_name, address)) {
        const auto msg = "Failed to get ipv6 socket address for interface " + interface_name;
        log_and_throw(msg.c_str());
    }

    // setup end point information
    end_point.port = 50000;
    memcpy(&end_point.address, &address.sin6_addr, sizeof(address.sin6_addr));

    fd = create_tcp_listen_socket(address, end_point.port, DEFAULT_SOCKET_BACKLOG, interface_name);

    poll_manager.register_fd(fd, [this]() { this->handle_connect(); });
}

ConnectionPlain::ConnectionPlain(PollManager& poll_manager_, int connected_fd) :
    ConnectionPlain(poll_manager_, connected_fd, std::nullopt) {
}

ConnectionPlain::ConnectionPlain(PollManager& poll_manager_, int connected_fd,
                                 const std::optional<sha512_hash_t>& vehicle_cert_hash_) :
    poll_manager(poll_manager_), fd(connected_fd), vehicle_cert_hash(vehicle_cert_hash_) {

    sockaddr_in6 local_adr{};
    socklen_t length = sizeof(local_adr);

    const auto sock_name_result = getsockname(fd, reinterpret_cast<sockaddr*>(&local_adr), &length);
    if (sock_name_result == 0 and local_adr.sin6_family == AF_INET6) {
        std::memcpy(&end_point.address, &local_adr.sin6_addr, sizeof(end_point.address));
        end_point.port = ntohs(local_adr.sin6_port);
    } else {
        logf_warning("getsockname() failed or local adr had no ipv6 address, falling back");
        end_point.port = 50000;
        std::memset(&end_point.address, 0x00, sizeof(end_point.address));
    }

    poll_manager.register_fd(fd, [this]() { this->handle_bootstrap(); });
}

ConnectionPlain::~ConnectionPlain() {
    // Make sure the socket is closed and unregistered from the poll manager even if the session was
    // torn down without an explicit close(). The event callback targets the (dying) session, so
    // silence it first.
    event_callback = nullptr;
    close();
}

void ConnectionPlain::set_event_callback(const ConnectionEventCallback& callback) {
    this->event_callback = callback;
}

Ipv6EndPoint ConnectionPlain::get_public_endpoint() const {
    return end_point;
}

void ConnectionPlain::write(const uint8_t* buf, size_t len) {
    assert(connection_open);

    // The fd is non-blocking, so a peer with a stalled receive window can make ::write() accept
    // only part of a multi-kB response (or none, EAGAIN) -- that is backpressure, not an error.
    // write_all waits for POLLOUT and continues, bounded by WRITE_TIMEOUT_MS so a peer that stays
    // stalled ends the session (throw -> session teardown) instead of stalling the shared poll
    // loop forever.
    if (not write_all(fd, buf, len, WRITE_TIMEOUT_MS)) {
        logf_error("write failed with error code: %d", errno);
        log_and_throw("Failed to write()");
    }
}

ReadResult ConnectionPlain::read(uint8_t* buf, size_t len) {
    assert(connection_open);

    const auto read_result = ::read(fd, buf, len);
    const auto did_block = (len > 0) and (not cmp_equal(read_result, len));

    if (read_result == 0 && len > 0) {
        return {false, 0, true}; // peer closed (EOF)
    }

    if (read_result >= 0) {
        return {did_block, static_cast<size_t>(read_result)};
    }

    // read_result < 0: distinguish a genuine would-block from a fatal error.
    // EAGAIN/EWOULDBLOCK/EINTR mean "retry"; anything else (ECONNRESET, or
    // ETIMEDOUT from the TCP keepalive, ...) is terminal, so report it as a
    // closed connection. Otherwise the level-triggered poll would spin on the
    // dead socket until the sequence timeout instead of tearing the session
    // down within one tick.
    if (errno == EAGAIN or errno == EWOULDBLOCK or errno == EINTR) {
        return {true, 0, false};
    }

    logf_warning("ConnectionPlain::read failed with error code: %d", errno);
    return {false, 0, true};
}

void ConnectionPlain::handle_connect() {

    sockaddr_in6 address{};
    const auto accepted = accept_connection(fd, address);

    if (accepted.status == AcceptResult::Status::Transient) {
        // The listener stays registered; just wait for the next connection.
        return;
    }

    if (accepted.status == AcceptResult::Status::Fatal) {
        // Tear down just this connection (drops the listener, delivers CLOSED -> the session is
        // reaped) instead of the whole controller loop.
        logf_error("Closing the TCP listener after a fatal accept failure");
        close();
        return;
    }

    const auto accept_fd = accepted.fd;

    const auto address_name = sockaddr_in6_to_name(address);

    if (not address_name) {
        // Never fatal (and would leak the accepted fd if it threw): log, drop the accepted socket
        // and tear down this connection.
        logf_error("Failed to determine string representation of ipv6 socket address");
        ::close(accept_fd);
        close();
        return;
    }

    logf_info("Incoming connection from [%s]:%" PRIu16, address_name.get(), ntohs(address.sin6_port));

    poll_manager.unregister_fd(fd);
    ::close(fd);

    // Point the member fd at the accepted socket BEFORE delivering events: an event handler
    // reacting to ACCEPTED/OPEN with write()/read()/close() must not act on the just-closed
    // listener fd, whose number the kernel may already have reused.
    fd = accept_fd;

    call_if_available(event_callback, ConnectionEvent::ACCEPTED);

    if (closed) {
        // An event handler closed the connection during ACCEPTED (e.g. protocol/TLS gating):
        // CLOSED has already been delivered, so the connection must not be revived by setting
        // connection_open, and OPEN must not be delivered after CLOSED.
        return;
    }

    connection_open = true;
    call_if_available(event_callback, ConnectionEvent::OPEN);

    if (closed) {
        // An event handler closed the connection; don't re-register the closed fd.
        return;
    }

    poll_manager.register_fd(fd, [this]() { this->handle_data(); });
}

void ConnectionPlain::handle_data() {
    assert(connection_open);

    call_if_available(event_callback, ConnectionEvent::NEW_DATA);
}

void ConnectionPlain::handle_bootstrap() {
    call_if_available(event_callback, ConnectionEvent::ACCEPTED);

    if (closed) {
        // Same guard as handle_connect: a close() from the ACCEPTED handler already delivered
        // CLOSED and unregistered the fd; don't revive the connection or fire OPEN after it.
        return;
    }

    connection_open = true;
    call_if_available(event_callback, ConnectionEvent::OPEN);

    if (closed) {
        // An event handler closed the connection; don't re-register the closed fd.
        return;
    }

    poll_manager.unregister_fd(fd);

    // The incoming v2gtp message is handled one poll cycle later
    poll_manager.register_fd(fd, [this]() { this->handle_data(); });
}

void ConnectionPlain::close() {
    if (closed) {
        // keep close() idempotent: the session driver closes on peer-EOF and again during teardown
        return;
    }
    closed = true;

    /* tear down the TCP connection (or the not-yet-accepted listening socket) */
    logf_info("Closing TCP connection");

    if (connection_open) {
        // Established connection: send our FIN. The grace period for an EV-initiated close happens
        // non-blocking in the session driver *before* this call (DIN [V2G-DC-937/938], ISO 15118-20
        // [V2G20-1633]); close() itself must never stall the shared poll loop.
        const auto shutdown_result = shutdown(fd, SHUT_RDWR);

        if (shutdown_result == -1) {
            logf_error("shutdown() failed");
        }
    }

    poll_manager.unregister_fd(fd);

    const auto close_shutdown = ::close(fd);

    if (close_shutdown == -1) {
        logf_error("close() failed");
    }

    logf_info("TCP connection closed gracefully");

    connection_open = false;
    call_if_available(event_callback, ConnectionEvent::CLOSED);
}

} // namespace iso15118::io
