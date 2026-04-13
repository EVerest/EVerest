// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#ifndef UTILS_MQTT_ABSTRACTION_IMPL_HPP
#define UTILS_MQTT_ABSTRACTION_IMPL_HPP

#include <cstddef>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include <mqtt.h>
#include <nlohmann/json.hpp>

#include <utils/message_handler.hpp>
#include <utils/message_queue.hpp>
#include <utils/mqtt_abstraction.hpp>
#include <utils/types.hpp>

#include <utils/thread.hpp>

constexpr auto MQTT_BUF_SIZE = 500 * std::size_t{1024};

namespace Everest {
/// \brief Contains a payload and the topic it was received on with additional QOS
struct MessageWithQOS : Message {
    QOS qos;     ///< The Quality of Service level
    bool retain; ///< If the retain flag should be set on publishing this message

    MessageWithQOS(const std::string& topic, const std::string& payload, QOS qos, bool retain);
};

///
/// \brief Contains a C++ abstraction of MQTT-C and some convenience functionality for using MQTT in EVerest modules
///
class MQTTAbstractionImpl : public MQTTAbstraction {
public:
    MQTTAbstractionImpl(const std::string& mqtt_server_address, const std::string& mqtt_server_port,
                        const std::string& mqtt_everest_prefix, const std::string& mqtt_external_prefix);
    MQTTAbstractionImpl(const std::string& mqtt_server_socket_path, const std::string& mqtt_everest_prefix,
                        const std::string& mqtt_external_prefix);

    ~MQTTAbstractionImpl() override;

    MQTTAbstractionImpl(MQTTAbstractionImpl const&) = delete;
    void operator=(MQTTAbstractionImpl const&) = delete;

    bool connect() override;
    void disconnect() override;
    void publish(const std::string& topic, const nlohmann::json& json) override;
    void publish(const std::string& topic, const nlohmann::json& json, QOS qos, bool retain = false) override;
    void publish(const std::string& topic, const std::string& data) override;
    void publish(const std::string& topic, const std::string& data, QOS qos, bool retain = false) override;
    void subscribe(const std::string& topic) override;
    void subscribe(const std::string& topic, QOS qos) override;
    void unsubscribe(const std::string& topic) override;
    void clear_retained_topics() override;
    nlohmann::json get(const MQTTRequest& request, std::size_t retries = 0) override;
    nlohmann::json get(const std::string& topic, QOS qos, std::size_t retries = 0) override;
    const std::string& get_everest_prefix() const override;
    const std::string& get_external_prefix() const override;
    std::shared_future<void> spawn_main_loop_thread() override;
    std::shared_future<void> get_main_loop_future() override;
    void register_handler(const std::string& topic, std::shared_ptr<TypedHandler> handler, QOS qos) override;
    void unregister_handler(const std::string& topic, const Token& token) override;

    ///
    /// \brief checks if the given \p full_topic matches the given \p wildcard_topic that can contain "+" and "#"
    /// wildcards
    ///
    /// \returns true if the topic matches, false otherwise
    static bool check_topic_matches(const std::string& full_topic, const std::string& wildcard_topic);

    ///
    /// \brief callback that is called from the mqtt implementation whenever a message is received
    static void publish_callback(void** unused, struct mqtt_response_publish* published);

private:
    static constexpr int mqtt_poll_timeout_ms{300000};
    bool mqtt_is_connected;
    MessageHandler message_handler;
    MessageQueue message_queue;
    std::vector<std::shared_ptr<MessageWithQOS>> messages_before_connected;
    std::mutex messages_before_connected_mutex;
    std::mutex topics_mutex;
    std::mutex topic_request_mutex;
    std::vector<std::string> retained_topics;
    std::unordered_set<std::string> subscribed_topics;

    Thread mqtt_mainloop_thread;
    std::shared_future<void> main_loop_future;

    std::string mqtt_server_socket_path;
    std::string mqtt_server_address;
    std::string mqtt_server_port;
    std::string mqtt_everest_prefix;
    std::string mqtt_external_prefix;
    struct mqtt_client mqtt_client;
    std::array<uint8_t, MQTT_BUF_SIZE> sendbuf;
    std::array<uint8_t, MQTT_BUF_SIZE> recvbuf;

    static int open_nb_socket(const char* addr, const char* port);
    bool connectBroker(std::string& socket_path);
    bool connectBroker(const char* host, const char* port);
    void on_mqtt_message(const Message& message);
    void on_mqtt_connect();
    static void on_mqtt_disconnect();

    void notify_write_data();

    int mqtt_socket_fd{-1};
    int event_fd{-1};
    int disconnect_event_fd{-1};
    nlohmann::json get_internal(const MQTTRequest& request);
};
} // namespace Everest

#endif // UTILS_MQTT_ABSTRACTION_IMPL_HPP
