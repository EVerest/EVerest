// SPDX-License-Identifier: Apache-2.0
// Copyright 2023 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include <netinet/in.h>

namespace iso15118::io {

constexpr auto DEFAULT_SOCKET_BACKLOG = 4;

bool check_and_update_interface(std::string& interface_name);

bool get_first_sockaddr_in6_for_interface(const std::string& interface_name, sockaddr_in6& address);

// creates a listening ipv6 TCP socket; throws on failure.
// interface_name names the interface address belongs to, for the bind failure message.
int create_tcp_listen_socket(sockaddr_in6 address, uint16_t port, int backlog, const std::string& interface_name);

std::unique_ptr<char[]> sockaddr_in6_to_name(const sockaddr_in6&);

bool set_tcp_keepalive(int fd);

// Result of accept_connection().
struct AcceptResult {
    enum class Status {
        Accepted,  // fd holds the accepted socket: non-blocking, TCP keepalive configured
        Transient, // nothing to accept this time; keep the listener and wait for the next wakeup
        Fatal,     // hard accept failure (e.g. EMFILE); the caller must tear down its listener
    };
    Status status;
    int fd{-1};
};

// Accepts a pending connection on listen_fd as a non-blocking socket with TCP keepalive configured
// (the read() paths rely on the keepalive's ETIMEDOUT for dead-peer detection) and fills
// peer_address. Never throws: the callers run inside poll callbacks, where an escaping exception
// tears down the whole controller loop. Single definition site of the transient/fatal errno policy
// shared by the plain-TCP and TLS accept paths -- keep them from diverging again.
AcceptResult accept_connection(int listen_fd, sockaddr_in6& peer_address);

// Total budget for writing one response to a stalled peer. In the normal case the kernel send
// buffer takes a whole response (<= MAX_V2G_PACKET_SIZE) without waiting; the budget only matters
// when the peer has stopped reading (zero receive window), where it bounds the stall of the shared
// controller poll loop -- well below the seconds-scale V2G sequence timeouts, but long enough for
// an EV that is merely slow to drain its window.
constexpr int WRITE_TIMEOUT_MS = 500;

// Writes all len bytes to the non-blocking fd, waiting for writability (POLLOUT) for up to
// timeout_ms in total when the kernel send buffer is full: a would-block or short write on a
// non-blocking socket is not an error, just backpressure. Returns true when everything was
// written; false on a fatal error or an exhausted budget (errno then holds the cause, ETIMEDOUT
// for the latter).
bool write_all(int fd, const uint8_t* buf, size_t len, int timeout_ms);
} // namespace iso15118::io
