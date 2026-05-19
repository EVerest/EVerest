// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

/** \file */

#pragma once

#include <everest/io/event/fd_event_client.hpp>
#include <everest/io/udp/udp_unconnected_socket.hpp>

namespace everest::lib::io::udp {

/**
 * @var udp_unconnected_client
 * @brief Client for an unconnected UDP datagram socket (IPv4 or IPv6,
 * auto-selected from the target endpoint) implemented in terms of
 * \ref event::fd_event_client and \ref udp::udp_unconnected_socket
 */
using udp_unconnected_client = event::fd_event_client<udp_unconnected_socket>::type;

} // namespace everest::lib::io::udp
