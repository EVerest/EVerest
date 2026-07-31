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

static constexpr auto DEFAULT_SOCKET_BACKLOG = 4;

ConnectionPlain::ConnectionPlain(PollManager& poll_manager_, const std::string& interface_name) :
    poll_manager(poll_manager_) {
    sockaddr_in6 address;
    if (not get_first_sockaddr_in6_for_interface(interface_name, address)) {
        const auto msg = "Failed to get ipv6 socket address for interface " + interface_name;
        log_and_throw(msg.c_str());
    }

    // setup end point information
    end_point.port = 50000;
    memcpy(&end_point.address, &address.sin6_addr, sizeof(address.sin6_addr));

    fd = socket(AF_INET6, SOCK_STREAM, 0);
    if (fd == -1) {
        log_and_throw("Failed to create an ipv6 socket");
    }

    // before bind, set the port
    address.sin6_port = htons(end_point.port);

    int optval_tmp{1};
    const auto set_reuseaddr = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &optval_tmp, sizeof(optval_tmp));
    if (set_reuseaddr == -1) {
        log_and_throw("setsockopt(SO_REUSEADDR) failed");
    }

    const auto set_reuseport = setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &optval_tmp, sizeof(optval_tmp));
    if (set_reuseport == -1) {
        log_and_throw("setsockopt(SO_REUSEPORT) failed");
    }

    const auto bind_result = bind(fd, reinterpret_cast<const struct sockaddr*>(&address), sizeof(address));
    if (bind_result == -1) {
        const auto error = "Failed to bind ipv6 socket to interface " + interface_name;
        log_and_throw(error.c_str());
    }

    const auto listen_result = listen(fd, DEFAULT_SOCKET_BACKLOG);
    if (listen_result == -1) {
        log_and_throw("Listen on socket failed");
    }

    poll_manager.register_fd(fd, [this]() { this->handle_connect(); });
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

    const auto write_result = ::write(fd, buf, len);

    if (write_result == -1) {
        log_and_throw("Failed to write()");
    } else if (not cmp_equal(write_result, len)) {
        log_and_throw("Could not complete write");
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

    sockaddr_in6 address;
    socklen_t address_len = sizeof(address);

    const auto accept_fd = accept4(fd, reinterpret_cast<struct sockaddr*>(&address), &address_len, SOCK_NONBLOCK);
    if (accept_fd == -1) {
        // A client that connects and RSTs quickly (e.g. a port scan) yields ECONNABORTED; EINTR and a
        // spurious wakeup are equally transient. The listener stays registered, so just wait for the
        // next connection instead of tearing down the whole controller loop.
        if (errno == EINTR or errno == EAGAIN or errno == EWOULDBLOCK or errno == ECONNABORTED) {
            logf_warning("accept4 failed with a transient error code: %d", errno);
            return;
        }
        // A hard accept failure (e.g. EMFILE) must not escape this poll callback -- PollManager::poll()
        // would rethrow it into the controller loop, whose catch exits the loop permanently. Tear down
        // just this connection (drops the listener, delivers CLOSED -> the session is reaped).
        logf_error("accept4 failed with error code: %d; closing the TCP listener", errno);
        close();
        return;
    }

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

    call_if_available(event_callback, ConnectionEvent::ACCEPTED);

    connection_open = true;
    call_if_available(event_callback, ConnectionEvent::OPEN);

    fd = accept_fd;
    poll_manager.register_fd(fd, [this]() { this->handle_data(); });
}

void ConnectionPlain::handle_data() {
    assert(connection_open);

    call_if_available(event_callback, ConnectionEvent::NEW_DATA);
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
