// SPDX-License-Identifier: Apache-2.0
// Copyright chargebyte GmbH and Contributors to EVerest
#ifndef WEBSOCKETSERVER_HPP
#define WEBSOCKETSERVER_HPP

#include <atomic>
#include <boost/asio.hpp>
#include <cstddef>
#include <deque>
#include <libwebsockets.h>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "TransportInterface.hpp"

namespace server {

class WebSocketServer : public TransportInterface {
public:
    // Constructor and Destructor
    explicit WebSocketServer(bool ssl_enabled, int port, const std::string& iface);
    ~WebSocketServer() override;

    // Methods
    bool running() const override;
    void send_data(const std::vector<uint8_t>& data) override;
    void send_data(const ClientId& client_id, const Data& data) override;
    void kill_client_connection(const ClientId& client_id, const std::string& kill_reason) override;
    uint connections_count() const override;

    bool start_server() override;
    bool stop_server() override;

private:
    friend class WebSocketServerTestAccess;

    static constexpr std::size_t MAX_PENDING_MESSAGES_PER_CLIENT = 256;
    static constexpr std::size_t MAX_PENDING_PAYLOAD_BYTES_PER_CLIENT = 1024 * 1024;

    struct ClientState {
        struct lws* wsi;
        std::deque<Data> outgoing_messages;
        std::size_t pending_payload_bytes = 0;
        std::optional<std::string> close_reason;
        bool close_scheduled = false;

        explicit ClientState(struct lws* connection) : wsi(connection) {
        }
        void request_close(const std::string& reason);
    };

    // Members
    bool m_ssl_enabled;
    std::shared_ptr<char> m_iface;
    struct lws_context_creation_info m_info {};
    struct lws_protocols m_lws_protocols[2];
    std::atomic<bool> m_running{false};
    struct lws_context* m_context = nullptr;
    std::thread m_server_thread;
    std::unordered_map<ClientId, ClientState> m_clients;
    mutable std::mutex m_clients_mutex;

    // Methods
    static int callback_ws(struct lws* wsi, enum lws_callback_reasons reason, void* user, void* in, size_t len);
    void enqueue_message(const ClientId& client_id, ClientState& client, const Data& data);
};

} // namespace server

#endif // WEBSOCKETSERVER_HPP
