// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

/** \file */

#pragma once

#include <everest/io/event/unique_fd.hpp>
#include <everest/io/uds/uds_client.hpp>
#include <everest/io/uds/uds_seqpacket_client.hpp>
#include <everest/io/uds/uds_seqpacket_peer.hpp>
#include <everest/io/uds/uds_server.hpp>
#include <everest/io/uds/uds_socket.hpp>
#include <string>

namespace everest::lib::io::uds {

/**
 * @var uds_client_interface
 * @brief Interface a \ref uds_client hands to its rx callback
 */
using uds_client_interface = event::fd_event_client<uds_client_socket>::interface;

/**
 * @var uds_server_interface
 * @brief Interface a \ref uds_server hands to its rx callback
 */
using uds_server_interface = event::fd_event_client<uds_server_socket>::interface;

/**
 * @var uds_seqpacket_client_interface
 * @brief Interface a \ref uds_seqpacket_client hands to its rx callback
 */
using uds_seqpacket_client_interface = event::fd_event_client<uds_seqpacket_client_socket>::interface;

/**
 * @var uds_seqpacket_peer_interface
 * @brief Interface a \ref uds_seqpacket_peer hands to its rx callback
 */
using uds_seqpacket_peer_interface = event::fd_event_client<uds_seqpacket_peer_socket>::interface;

/**
 * @name send_fd
 * @brief Send one file descriptor with \p metadata as the message.
 * @details Overloads for every socket policy and every event client. With an `int` the caller
 * keeps \p fd and a duplicate is sent, so \p fd may be closed right after the call. With a
 * \ref event::unique_fd the descriptor is given up. On an event client the send is queued; the
 * payload keeps the descriptor alive until it is written.
 * @param[in] sender Socket or event client to send through
 * @param[in] fd Descriptor to send
 * @param[in] metadata Message bytes, may be empty
 * @return The result of tx(). False without sending if \p fd is invalid or \p metadata exceeds
 *         \ref uds_payload::max_size
 * @{
 */
bool send_fd(uds_client_socket& sender, int fd, std::string const& metadata = {});
bool send_fd(uds_client_socket& sender, event::unique_fd&& fd, std::string const& metadata = {});
bool send_fd(uds_server_socket& sender, int fd, std::string const& metadata = {});
bool send_fd(uds_server_socket& sender, event::unique_fd&& fd, std::string const& metadata = {});
bool send_fd(uds_client_interface& sender, int fd, std::string const& metadata = {});
bool send_fd(uds_client_interface& sender, event::unique_fd&& fd, std::string const& metadata = {});
bool send_fd(uds_server_interface& sender, int fd, std::string const& metadata = {});
bool send_fd(uds_server_interface& sender, event::unique_fd&& fd, std::string const& metadata = {});
bool send_fd(uds_seqpacket_socket_base& sender, int fd, std::string const& metadata = {});
bool send_fd(uds_seqpacket_socket_base& sender, event::unique_fd&& fd, std::string const& metadata = {});
bool send_fd(uds_seqpacket_client_interface& sender, int fd, std::string const& metadata = {});
bool send_fd(uds_seqpacket_client_interface& sender, event::unique_fd&& fd, std::string const& metadata = {});
bool send_fd(uds_seqpacket_peer_interface& sender, int fd, std::string const& metadata = {});
bool send_fd(uds_seqpacket_peer_interface& sender, event::unique_fd&& fd, std::string const& metadata = {});
/** @} */

} // namespace everest::lib::io::uds
