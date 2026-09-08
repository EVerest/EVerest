// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

/** \file */

#pragma once

#include <everest/io/event/fd_event_client.hpp>
#include <everest/io/uds/uds_seqpacket_socket.hpp>

namespace everest::lib::io::uds {

/**
 * @var uds_seqpacket_peer
 * @brief Accepted side of a SOCK_SEQPACKET connection, handed out by uds_seqpacket_listener. Single use: drop it on
 * error instead of resetting.
 */
using uds_seqpacket_peer = event::fd_event_client<uds_seqpacket_peer_socket>::type;

} // namespace everest::lib::io::uds
