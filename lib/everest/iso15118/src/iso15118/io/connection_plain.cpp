// SPDX-License-Identifier: Apache-2.0
// Copyright 2023 Pionix GmbH and Contributors to EVerest
#include <iso15118/io/connection_plain.hpp>

#include <cassert>
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

    if (read_result > 0) {
        return {did_block, static_cast<size_t>(read_result)};
    }

    if (read_result == 0 and len > 0) {
        // TCP EOF: the peer (EV) closed its side of the connection. This is the regular way a
        // session ends -- the EVCC closes first (DIN [V2G-DC-937], ISO 15118-2 [V2G2-025]) -- but
        // it also covers a mid-session EV disconnect. Surface it as CLOSED so the session driver
        // reacts now instead of running into a sequence timeout (EOF keeps the fd readable and
        // would otherwise be indistinguishable from EAGAIN here).
        logf_info("TCP connection closed by the peer");
        poll_manager.unregister_fd(fd);
        ::close(fd);
        connection_open = false;
        closed = true;
        call_if_available(event_callback, ConnectionEvent::CLOSED);
        return {true, 0};
    }

    // should be an error
    if (errno != EAGAIN) {
        // in case the error is not due to blocking, log it
        logf_error("ConnectionPlain::read failed with error code: %d", errno);
    }

    return {did_block, 0};
}

void ConnectionPlain::handle_connect() {

    sockaddr_in6 address;
    socklen_t address_len = sizeof(address);

    const auto accept_fd = accept4(fd, reinterpret_cast<struct sockaddr*>(&address), &address_len, SOCK_NONBLOCK);
    if (accept_fd == -1) {
        log_and_throw("Failed to accept4");
    }

    const auto address_name = sockaddr_in6_to_name(address);

    if (not address_name) {
        log_and_throw("Failed to determine string representation of ipv6 socket address");
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
        // already closed, either locally or by the peer (EOF path in read()); keep close() idempotent
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
