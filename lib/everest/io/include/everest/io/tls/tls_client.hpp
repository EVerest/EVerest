// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest

/** \file */

#pragma once
#include <everest/io/event/fd_event_client.hpp>
#include <everest/io/tls/tls_client_socket.hpp>

namespace everest::lib::io::tls {

/**
 * @var tls_client
 * @brief Event-loop-driven TLS client.
 * @details Register it with an \ref event::fd_event_handler to drive it. Constructor arguments are
 *          forwarded to tls_client_socket::setup(cfg, host, port, timeout_ms). The TLS handshake
 *          runs on the loop, only the TCP connect runs on the async policy's detached thread.
 *          Payloads passed to tx() before the connect completes and during the handshake are
 *          buffered and flushed in order once it completes. The outer handler must outlive the
 *          client, whose destructor unregisters from it.
 */
using tls_client = event::fd_event_client<tls_client_socket>::type;

/**
 * @var tls_client_interface
 * @brief The client as seen by its RX callback.
 */
using tls_client_interface = event::fd_event_client<tls_client_socket>::interface;

} // namespace everest::lib::io::tls
