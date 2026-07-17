// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/io/connection_client_plain.hpp>

#include <cassert>
#include <cstring>

#include <arpa/inet.h>
#include <net/if.h>
#include <poll.h>
#include <unistd.h>

#include <iso15118/detail/helper.hpp>
#include <iso15118/detail/io/socket_helper.hpp>

namespace iso15118::io {

static constexpr auto CONNECT_TIMEOUT_MS = 5000;

ConnectionClientPlain::ConnectionClientPlain(PollManager& poll_manager_, const std::string& interface_name,
                                             const Ipv6EndPoint& secc_endpoint) :
    poll_manager(poll_manager_), end_point(secc_endpoint) {

    fd = socket(AF_INET6, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (fd == -1) {
        log_and_throw("Failed to create an ipv6 socket");
    }

    sockaddr_in6 address{};
    address.sin6_family = AF_INET6;
    address.sin6_port = htons(secc_endpoint.port);
    memcpy(&address.sin6_addr, secc_endpoint.address, sizeof(secc_endpoint.address));
    address.sin6_scope_id = if_nametoindex(interface_name.c_str());

    const auto connect_result = ::connect(fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address));
    if (connect_result == -1 and errno != EINPROGRESS) {
        const auto error_msg = adding_err_msg("Failed to connect() to SECC");
        log_and_throw(error_msg.c_str());
    }

    // NOTE: the PollManager only monitors POLLIN, so the non-blocking connect is completed here by
    // waiting for the socket to become writable
    if (connect_result == -1) {
        struct pollfd pfd {
            fd, POLLOUT, 0
        };

        const auto poll_result = ::poll(&pfd, 1, CONNECT_TIMEOUT_MS);
        if (poll_result == -1) {
            const auto error_msg = adding_err_msg("Failed to poll() during connect");
            log_and_throw(error_msg.c_str());
        }
        if (poll_result == 0) {
            log_and_throw("Timeout while connecting to SECC");
        }

        int so_error{0};
        socklen_t len = sizeof(so_error);
        if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_error, &len) == -1) {
            const auto error_msg = adding_err_msg("Failed to getsockopt(SO_ERROR)");
            log_and_throw(error_msg.c_str());
        }
        if (so_error != 0) {
            errno = so_error;
            const auto error_msg = adding_err_msg("Failed to connect() to SECC");
            log_and_throw(error_msg.c_str());
        }
    }

    connection_open = true;

    poll_manager.register_fd(fd, [this]() { this->handle_data(); });
}

ConnectionClientPlain::~ConnectionClientPlain() = default;

void ConnectionClientPlain::set_event_callback(const ConnectionEventCallback& callback) {
    event_callback = callback;

    // the connection is established during construction, so signal the driver right away
    call_if_available(event_callback, ConnectionEvent::ACCEPTED);
    call_if_available(event_callback, ConnectionEvent::OPEN);
}

Ipv6EndPoint ConnectionClientPlain::get_public_endpoint() const {
    return end_point;
}

void ConnectionClientPlain::write(const uint8_t* buf, size_t len) {
    assert(connection_open);

    const auto write_result = ::write(fd, buf, len);

    if (write_result == -1) {
        log_and_throw("Failed to write()");
    } else if (not cmp_equal(write_result, len)) {
        log_and_throw("Could not complete write");
    }
}

ReadResult ConnectionClientPlain::read(uint8_t* buf, size_t len) {
    assert(connection_open);

    const auto read_result = ::read(fd, buf, len);
    const auto did_block = (len > 0) and (not cmp_equal(read_result, len));

    if (read_result >= 0) {
        return {did_block, static_cast<size_t>(read_result)};
    }

    // should be an error
    if (errno != EAGAIN) {
        // in case the error is not due to blocking, log it
        logf_error("ConnectionClientPlain::read failed with error code: %d", errno);
    }

    return {did_block, 0};
}

void ConnectionClientPlain::handle_data() {
    assert(connection_open);

    call_if_available(event_callback, ConnectionEvent::NEW_DATA);
}

void ConnectionClientPlain::close() {

    /* tear down TCP connection gracefully */
    logf_info("Closing TCP connection");

    const auto shutdown_result = shutdown(fd, SHUT_RDWR);

    if (shutdown_result == -1) {
        logf_error("shutdown() failed");
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
