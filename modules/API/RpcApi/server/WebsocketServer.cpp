// SPDX-License-Identifier: Apache-2.0
// Copyright chargebyte GmbH and Contributors to EVerest

#include "WebsocketServer.hpp"

#include <algorithm>
#include <cstring>
#include <everest/helpers/helpers.hpp>
#include <everest/logging.hpp>

namespace server {

static const int PER_SESSION_DATA_SIZE{4096};

static void log_callback(int level, const char* line) {
    switch (level) {
    case LLL_ERR:
        EVLOG_error << line;
        break;
    case LLL_WARN:
        EVLOG_warning << line;
        break;
    default:
    case LLL_DEBUG:
        EVLOG_debug << line;
        break;
    }
}

int WebSocketServer::callback_ws(struct lws* wsi, enum lws_callback_reasons reason, [[maybe_unused]] void* user,
                                 void* in, size_t len) {
    if (!wsi) {
        return 0;
    }

    struct lws_context* context = lws_get_context(wsi);
    WebSocketServer* server = static_cast<WebSocketServer*>(lws_context_user(context));

    if (!server) {
        throw std::runtime_error("Error: WebSocketServer instance not found!");
    }

    std::unique_lock<std::mutex> lock(server->m_clients_mutex); // To protect access to m_clients

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wswitch-enum"
    switch (reason) {
    case LWS_CALLBACK_ESTABLISHED: {
        // Generate a random UUID for the client
        std::string client_id = everest::helpers::get_uuid();
        server->m_clients.emplace(client_id, ClientState{wsi});

        char ip_address_buf[INET6_ADDRSTRLEN]{0};
        if (lws_get_peer_simple(wsi, ip_address_buf, sizeof(ip_address_buf)) == NULL) {
            ip_address_buf[0] = '\0'; // ensure empty string
            EVLOG_warning << "Failed to get client IP address";
        }

        std::string ip_address(ip_address_buf, strnlen(ip_address_buf, sizeof(ip_address_buf)));

        lock.unlock();                                      // Unlock before calling the callback
        server->on_client_connected(client_id, ip_address); // Call the on_client_connected callback
        lock.lock();                                        // Lock again after the callback
        EVLOG_info << "Client " << client_id << " connected" << (ip_address.empty() ? "" : (" from " + ip_address));
        break;
    }
    case LWS_CALLBACK_CLOSED: {
        auto it = std::find_if(server->m_clients.begin(), server->m_clients.end(),
                               [wsi](const auto& client) { return client.second.wsi == wsi; });
        if (it != server->m_clients.end()) {
            const auto client_id = it->first;
            server->m_clients.erase(it);
            lock.unlock(); // Unlock before calling the callback
            EVLOG_info << "Client " << client_id << " disconnected";
            server->on_client_disconnected(client_id); // Call the on_client_disconnected callback
            lock.lock();                               // Lock again after the callback
        }
        break;
    }
    case LWS_CALLBACK_EVENT_WAIT_CANCELLED:
        for (auto& entry : server->m_clients) {
            auto& client = entry.second;
            if (client.close_reason) {
                if (!client.close_scheduled) {
                    auto& reason = *client.close_reason;
                    lws_close_reason(client.wsi, LWS_CLOSE_STATUS_PROTOCOL_ERR,
                                     reinterpret_cast<unsigned char*>(reason.data()), reason.size());
                    lws_set_timeout(client.wsi, PENDING_TIMEOUT_CLOSE_SEND, LWS_TO_KILL_ASYNC);
                    client.close_scheduled = true;
                }
            } else if (!client.outgoing_messages.empty()) {
                lws_callback_on_writable(client.wsi);
            }
        }
        break;
    case LWS_CALLBACK_SERVER_WRITEABLE: {
        auto it = std::find_if(server->m_clients.begin(), server->m_clients.end(),
                               [wsi](const auto& client) { return client.second.wsi == wsi; });
        if (it == server->m_clients.end()) {
            break;
        }

        const auto client_id = it->first;
        auto& client = it->second;
        if (client.close_reason) {
            break;
        }

        if (!client.outgoing_messages.empty()) {
            const auto& data = client.outgoing_messages.front();
            std::vector<unsigned char> buffer(LWS_PRE + data.size());
            std::copy(data.begin(), data.end(), buffer.begin() + LWS_PRE);
            // LWS owns any buffered remainder; resubmitting it would duplicate data.
            const int written = lws_write(wsi, buffer.data() + LWS_PRE, data.size(), LWS_WRITE_BINARY);
            if (written < 0 || static_cast<std::size_t>(written) < data.size()) {
                EVLOG_error << "Failed to send data to client " << client_id << ": requested " << data.size()
                            << " bytes, lws_write returned " << written;
                return -1;
            }
            client.pending_payload_bytes -= data.size();
            client.outgoing_messages.pop_front();
            if (!client.outgoing_messages.empty()) {
                lws_callback_on_writable(wsi);
            }
        }
        break;
    }
    case LWS_CALLBACK_RECEIVE: {
        auto it = std::find_if(server->m_clients.begin(), server->m_clients.end(),
                               [wsi](const auto& client) { return client.second.wsi == wsi; });
        if (it != server->m_clients.end()) {
            const auto client_id = it->first;
            auto* data = static_cast<unsigned char*>(in);
            std::vector<uint8_t> received_data(data, data + len);
            lock.unlock();                                       // Unlock before calling the callback
            server->on_data_available(client_id, received_data); // Call the on_data_available callback
            lock.lock();                                         // Lock again after the callback
        }
        break;
    }
    default:
        break;
    }
#pragma GCC diagnostic pop

    lock.unlock(); // Unlock the mutex after processing the callback

    return 0;
}

WebSocketServer::WebSocketServer(bool ssl_enabled, int port, const std::string& iface) : m_ssl_enabled(ssl_enabled) {
    // Constructor implementation
    memset(&m_info, 0, sizeof(m_info));
    lws_set_log_level(LLL_ERR | LLL_WARN | LLL_NOTICE | LLL_INFO | LLL_DEBUG, log_callback);
    m_info.port = port;
    // interface: must not be empty (else nothing to do)
    if (iface.empty()) {
        throw std::runtime_error("WebSocketServer: parameter 'websocket_interface' must not be empty");
    }
    if (iface != "all") {
        // create a local C-string
        char* iface_ptr = new char[iface.size() + 1];
        std::strcpy(iface_ptr, iface.c_str());
        // store it persistently in a member, and free the local variable
        m_iface = std::shared_ptr<char>(iface_ptr, [](char* ptr) {
            delete[] ptr; // custom deleter to free the char array
        });

        m_info.iface = m_iface.get();
    }
    m_lws_protocols[0] = {"EVerestRpcApi", callback_ws, PER_SESSION_DATA_SIZE, 0, 0, NULL, 0};
    m_lws_protocols[1] = LWS_PROTOCOL_LIST_TERM;

    m_info.protocols = m_lws_protocols;
    m_info.options = LWS_SERVER_OPTION_FAIL_UPON_UNABLE_TO_BIND;
    m_info.options |= (m_ssl_enabled ? LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT : 0);
    m_info.user = this; // To access WebSocketServer instance in callback

    const std::string compiled_lws_version{LWS_LIBRARY_VERSION};
    const char* linked_version_cstr = lws_get_library_version();
    const std::string linked_lws_version = linked_version_cstr ? linked_version_cstr : "unknown";
    EVLOG_info << "libwebsockets version (compiled/runtime): " << compiled_lws_version << " / " << linked_lws_version;
}

WebSocketServer::~WebSocketServer() {
    stop_server();
}

bool WebSocketServer::running() const {
    return m_running;
}

void WebSocketServer::ClientState::request_close(const std::string& reason) {
    if (!close_reason) {
        close_reason = reason;
        outgoing_messages.clear();
        pending_payload_bytes = 0;
    }
}

void WebSocketServer::enqueue_message(const ClientId& client_id, ClientState& client, const Data& data) {
    if (client.close_reason) {
        return;
    }
    if (client.outgoing_messages.size() >= MAX_PENDING_MESSAGES_PER_CLIENT ||
        data.size() > MAX_PENDING_PAYLOAD_BYTES_PER_CLIENT - client.pending_payload_bytes) {
        EVLOG_warning << "Closing client " << client_id
                      << ": outbound queue limit exceeded (queued messages: " << client.outgoing_messages.size() << "/"
                      << MAX_PENDING_MESSAGES_PER_CLIENT << ", queued payload bytes: " << client.pending_payload_bytes
                      << "/" << MAX_PENDING_PAYLOAD_BYTES_PER_CLIENT << ", incoming payload bytes: " << data.size()
                      << ")";
        client.request_close("Outbound queue limit exceeded");
        return;
    }
    client.outgoing_messages.push_back(data);
    client.pending_payload_bytes += data.size();
}

// send data to all connected clients
void WebSocketServer::send_data(const std::vector<uint8_t>& data) {
    std::lock_guard<std::mutex> lock(m_clients_mutex);
    if (!m_running || !m_context) {
        return;
    }
    for (auto& client : m_clients) {
        enqueue_message(client.first, client.second, data);
    }
    lws_cancel_service(m_context);
}

// send data to client identified by ClientId
void WebSocketServer::send_data(const ClientId& client_id, const std::vector<uint8_t>& data) {
    try {
        std::lock_guard<std::mutex> lock(m_clients_mutex);

        if (!m_running || !m_context) {
            return;
        }
        auto client = m_clients.find(client_id);
        if (client == m_clients.end()) {
            EVLOG_error << "Client " << client_id << " not found";
            return;
        }
        enqueue_message(client_id, client->second, data);
        lws_cancel_service(m_context);
    } catch (const std::exception& e) {
        EVLOG_error << "Exception occurred while sending data to client " << client_id << ": " << e.what();
    }
}

void WebSocketServer::kill_client_connection(const ClientId& client_id, const std::string& kill_reason) {
    std::lock_guard<std::mutex> lock(m_clients_mutex);
    if (!m_running || !m_context) {
        return;
    }
    auto client = m_clients.find(client_id);
    if (client == m_clients.end()) {
        EVLOG_error << "Client ID " << client_id << " not found!";
        return;
    }
    if (client->second.close_reason) {
        return;
    }
    client->second.request_close(kill_reason.empty() ? "Connection closed by server" : kill_reason);
    lws_cancel_service(m_context);
    EVLOG_info << "Client " << client_id << " connection close requested (reason: " << kill_reason << ")";
}

uint WebSocketServer::connections_count() const {
    std::lock_guard<std::mutex> lock(m_clients_mutex);
    return m_clients.size();
}

bool WebSocketServer::start_server() {
    if (m_running) {
        EVLOG_warning << "WebSocket Server is already running";
        return true;
    }

    m_context = lws_create_context(&m_info);
    if (!m_context) {
        EVLOG_error << "Failed to create WebSocket context";
        return false;
    }

    EVLOG_info << "WebSocket Server running on port " << m_info.port
               << (m_info.iface ? " (interface \"" + std::string(m_info.iface) + "\" only)" : "")
               << (m_ssl_enabled ? " with" : " without") << " TLS";

    m_running = true;
    m_server_thread = std::thread([this]() {
        while (m_running) {
            lws_service(m_context, 1000);
        }
    });

    return true; // Server started successfully
}

bool WebSocketServer::stop_server() {
    {
        std::lock_guard<std::mutex> lock(m_clients_mutex);
        if (!m_running) {
            return true;
        }
        m_running = false;
        if (m_context) {
            lws_cancel_service(m_context);
        }
    }

    if (m_server_thread.joinable()) {
        m_server_thread.join(); // Wait for server thread to finish
    }

    if (m_context) {
        lws_context_destroy(m_context);
        m_context = nullptr;
    }
    {
        std::lock_guard<std::mutex> lock(m_clients_mutex);
        m_clients.clear();
    }
    EVLOG_info << "WebSocket Server stopped";

    return true;
}

} // namespace server
