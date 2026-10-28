// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

/** \file */

#pragma once

#include <everest/io/event/fd_event_client.hpp>
#include <everest/io/uds/uds_socket.hpp>

namespace everest::lib::io::uds {

/**
 * @var uds_client
 * @brief Datagram client. Constructor arguments go to uds_client_socket::open.
 */
using uds_client = event::fd_event_client<uds_client_socket>::type;

} // namespace everest::lib::io::uds
