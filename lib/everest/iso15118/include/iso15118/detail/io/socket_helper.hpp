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

// creates an ipv6 TCP socket, sets address.sin6_port to htons(port), binds to
// the address and starts listening; throws on any failure
int create_tcp_listen_socket(sockaddr_in6& address, uint16_t port, int backlog);

std::unique_ptr<char[]> sockaddr_in6_to_name(const sockaddr_in6&);

bool set_tcp_keepalive(int fd);
} // namespace iso15118::io
