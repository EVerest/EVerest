// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

/** \file */

#pragma once

#include <everest/io/event/fd_event_client.hpp>
#include <everest/io/uds/uds_socket.hpp>

namespace everest::lib::io::uds {

/**
 * @var uds_server
 * @brief Client for Unix Domain Sockets implemented in terms of \ref event::fd_event_client
 * and \ref uds::uds_server_socket
 */
using uds_server = event::fd_event_client<uds_server_socket>::type;

/**
 * @var uds_fd_server
 * @brief Server for Unix Domain Sockets implemented in terms of \ref event::fd_event_client
 * and \ref uds::uds_fd_server_socket. It's purpose is to share file descriptors across processes
 */
using uds_fd_server = event::fd_event_client<uds_fd_server_socket>::type;

} // namespace everest::lib::io::uds
