// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

/** \file */

#pragma once

#include <everest/io/event/fd_event_client.hpp>
#include <everest/io/udp/udp_dualstack_server_socket.hpp>

namespace everest::lib::io::udp {

/**
 * @var udp_dualstack_server
 * @brief Dual-stack (IPv6 + IPv4-mapped) UDP server implemented in terms of
 * \ref event::fd_event_client and \ref udp::udp_dualstack_server_socket
 */
using udp_dualstack_server = event::fd_event_client<udp_dualstack_server_socket>::type;

} // namespace everest::lib::io::udp
