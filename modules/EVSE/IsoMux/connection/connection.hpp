// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2022 chargebyte GmbH
// Copyright (C) 2022 Contributors to EVerest

#ifndef CONNECTION_H
#define CONNECTION_H

#include <cstddef>
#include <netinet/in.h>

#include "v2g_ctx.hpp"

/*!
 * \brief initialise TCP/TLS connections
 * \param ctx the V2G context
 * \return 0 on success
 */
int connection_init(struct v2g_context* ctx);

/*!
 * \brief start TCP/TLS servers
 * \param ctx the V2G context
 * \return 0 on success
 */
int connection_start_servers(struct v2g_context* ctx);
int create_udp_socket(const uint16_t udp_port, const char* interface_name);

/*!
 * \brief check for V2G message sequence timeout
 * \param ts_start start time
 * \param ctx the V2G context
 * \return true on timeout
 */
bool is_sequence_timeout(struct timespec ts_start, struct v2g_context* ctx);

/*!
 * \brief connection_read This abstracts a read from the connection socket, so that higher level functions
 * are not required to distinguish between TCP and TLS connections.
 * \param conn v2g connection context
 * \param buf buffer to store received message sequence.
 * \param count number of read bytes.
 * \return Returns the number of read bytes if successful, otherwise returns -1 for reading errors and
 * -2 for closed connection */
ssize_t connection_read(struct v2g_connection* conn, unsigned char* buf, std::size_t count, bool read_complete);

/*!
 * \brief connection_write This abstracts a write to the connection socket, so that higher level functions
 * are not required to distinguish between TCP and TLS connections.
 * \param conn v2g connection context
 * \param buf buffer to store received message sequence.
 * \param count size of the buffer
 * \return Returns the number of read bytes if successful, otherwise returns -1 for reading errors and
 * -2 for closed connection */
ssize_t connection_write(struct v2g_connection* conn, unsigned char* buf, std::size_t count);

/*!
 * \brief connection_slot_acquire reserves a slot for a connection thread, shared by the TCP and TLS servers.
 * When all slots are taken, the socket of the oldest connection without a v2g-session is shut down and its slot
 * handed over.
 * \param socket_fd identifies the slot, so it must be released before the socket is closed
 * \return false if no slot is available, the connection must then be closed without starting a thread
 */
bool connection_slot_acquire(int socket_fd);
void connection_slot_claim_session(int socket_fd);
bool connection_slot_held(int socket_fd);
void connection_slot_release(int socket_fd);

void* connection_handle_tcp(void* data);
void* connection_handle(void* data);
int connection_proxy(struct v2g_connection* conn, int proxy_fd);

#endif /* CONNECTION_H */
