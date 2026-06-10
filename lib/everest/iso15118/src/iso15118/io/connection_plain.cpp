// SPDX-License-Identifier: Apache-2.0
// Copyright 2023 Pionix GmbH and Contributors to EVerest
#include <iso15118/io/connection_plain.hpp>

#include <cassert>
#include <cerrno>
#include <chrono>
#include <cinttypes>
#include <cstring>
#include <thread>

#include <arpa/inet.h>
#include <unistd.h>

#include <iso15118/detail/helper.hpp>
#include <iso15118/detail/io/socket_helper.hpp>

namespace iso15118::io {

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

    fd = create_tcp_listen_socket(address, end_point.port, DEFAULT_SOCKET_BACKLOG);

    poll_manager.register_fd(fd, [this]() { this->handle_connect(); });
}

ConnectionPlain::~ConnectionPlain() = default;

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
    // dead socket until the 60 s sequence timeout instead of tearing the
    // session down within one tick.
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
        log_and_throw("Failed to accept4");
    }

    if (not set_tcp_keepalive(accept_fd)) {
        logf_warning("Failed to configure TCP keepalive on accepted connection");
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

    /* tear down TCP connection gracefully */
    logf_info("Closing TCP connection");

    const auto shutdown_result = shutdown(fd, SHUT_RDWR);

    if (shutdown_result == -1) {
        logf_error("shutdown() failed");
    }

    // Waiting for client closing the connection
    std::this_thread::sleep_for(std::chrono::seconds(2));

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
