// SPDX-License-Identifier: Apache-2.0
// Copyright 2023 Pionix GmbH and Contributors to EVerest
#pragma once

#include <memory>
#include <string>

#include <netinet/in.h>

namespace iso15118::io {

bool check_and_update_interface(std::string& interface_name);

bool get_first_sockaddr_in6_for_interface(const std::string& interface_name, sockaddr_in6& address);

std::unique_ptr<char[]> sockaddr_in6_to_name(const sockaddr_in6&);

bool set_tcp_keepalive(int fd);

// [V2G2-077/124] Constrain the kernel's ephemeral source-port selection for this socket to the IANA
// dynamic range 49152-65535, as required for the EVCC's TCP/TLS connection to the SECC. Best-effort:
// logs a warning and returns false if the kernel does not support IP_LOCAL_PORT_RANGE (pre-6.3).
bool restrict_source_port_range(int fd);
} // namespace iso15118::io
