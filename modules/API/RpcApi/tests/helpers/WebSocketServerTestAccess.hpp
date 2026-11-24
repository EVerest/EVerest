// SPDX-License-Identifier: Apache-2.0
// Copyright chargebyte GmbH and Contributors to EVerest
#pragma once

#include <cstddef>
#include <libwebsockets.h>
#include <mutex>
#include <sys/socket.h>

#include "../server/WebsocketServer.hpp"

namespace server {
class WebSocketServerTestAccess {
public:
    static constexpr std::size_t message_limit() {
        return WebSocketServer::MAX_PENDING_MESSAGES_PER_CLIENT;
    }
    static constexpr std::size_t payload_limit() {
        return WebSocketServer::MAX_PENDING_PAYLOAD_BYTES_PER_CLIENT;
    }
    static int listen_port(WebSocketServer& server) {
        return lws_get_vhost_listen_port(lws_get_vhost_by_name(server.m_context, "default"));
    }
    static bool set_send_buffer_size(WebSocketServer& server, const TransportInterface::ClientId& id, int bytes) {
        std::lock_guard<std::mutex> lock(server.m_clients_mutex);
        return setsockopt(lws_get_socket_fd(server.m_clients.at(id).wsi), SOL_SOCKET, SO_SNDBUF, &bytes,
                          sizeof(bytes)) == 0;
    }
    static bool queue_empty(WebSocketServer& server, const TransportInterface::ClientId& id) {
        std::lock_guard<std::mutex> lock(server.m_clients_mutex);
        const auto it = server.m_clients.find(id);
        return it != server.m_clients.end() && it->second.outgoing_messages.empty() &&
               it->second.pending_payload_bytes == 0;
    }
    static std::size_t queued_message_count(WebSocketServer& server, const TransportInterface::ClientId& id) {
        std::lock_guard<std::mutex> lock(server.m_clients_mutex);
        return server.m_clients.at(id).outgoing_messages.size();
    }
};
} // namespace server
