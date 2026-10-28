// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

/** \file */

#pragma once

#include <everest/io/event/fd_event_client.hpp>
#include <everest/io/uds/uds_seqpacket_socket.hpp>

namespace everest::lib::io::uds {

/**
 * @var uds_seqpacket_client
 * @brief Connecting side of a SOCK_SEQPACKET connection. Constructor arguments go to uds_seqpacket_client_socket::open.
 * A lost connection is reported through the error handler; reset() reconnects.
 */
using uds_seqpacket_client = event::fd_event_client<uds_seqpacket_client_socket>::type;

} // namespace everest::lib::io::uds
