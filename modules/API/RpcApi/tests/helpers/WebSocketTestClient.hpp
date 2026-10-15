// SPDX-License-Identifier: Apache-2.0
// Copyright chargebyte GmbH and Contributors to EVerest
#ifndef WEBSOCKETTESTCLIENT_HPP
#define WEBSOCKETTESTCLIENT_HPP

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <everest/logging.hpp>
#include <iostream>
#include <libwebsockets.h>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class WebSocketTestClient {
public:
    WebSocketTestClient(const std::string& address, int port);
    ~WebSocketTestClient();

    bool connect();
    bool is_connected();
    void send(const std::string& message);
    void send_api_hello_req();
    const std::string& receive() const;
    void close();
    std::string get_received_data() {
        std::string data;
        {
            std::lock_guard<std::mutex> lock(m_cv_mutex);
            if (!m_received_messages.empty()) {
                data = std::move(m_received_messages.front());
                m_received_messages.pop_front();
            }
        }
        return data;
    }

    std::string wait_for_data(std::chrono::milliseconds timeout, bool is_result = true) {
        std::unique_lock<std::mutex> lock(m_cv_mutex);

        const auto matches = [this, is_result]() {
            return std::find_if(m_received_messages.begin(), m_received_messages.end(),
                                [is_result](const auto& message) {
                                    return !is_result || (message.find("\"result\"") != std::string::npos &&
                                                          message.find("\"id\"") != std::string::npos);
                                });
        };
        bool received = m_cv.wait_for(lock, timeout, [&] { return matches() != m_received_messages.end(); });

        if (!received) {
            return ""; // Timeout
        }

        auto message_it = matches();
        std::string data = std::move(*message_it);
        m_received_messages.erase(message_it);
        return data;
    }

    bool wait_for_response(std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(m_cv_mutex);
        return m_cv.wait_for(lock, timeout, [this] { return !m_received_messages.empty(); });
    }

    bool wait_until_connected(std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(m_cv_mutex);
        return m_cv.wait_for(lock, timeout, [this] { return m_connected.load(); });
    }

    void start_lws_service_thread();
    void stop_lws_service_thread();

private:
    static int callback(struct lws* wsi, enum lws_callback_reasons reason, void* user, void* in, size_t len);

    std::string m_address;
    int m_port;
    struct lws_context* m_context;
    struct lws_client_connect_info m_ccinfo {};
    struct lws* m_wsi;
    std::atomic<bool> m_connected{false};
    std::atomic<bool> m_lws_service_running{false};
    std::thread m_lws_service_thread;
    std::string m_received_data;
    std::deque<std::string> m_received_messages;

public:
    // Condition variable to wait for response
    std::condition_variable m_cv;
    std::mutex m_cv_mutex;
};

#endif // WEBSOCKETTESTCLIENT_HPP
