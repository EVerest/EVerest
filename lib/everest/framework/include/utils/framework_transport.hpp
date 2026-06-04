// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#ifndef UTILS_FRAMEWORK_TRANSPORT_HPP
#define UTILS_FRAMEWORK_TRANSPORT_HPP

#include <cstddef>
#include <future>
#include <memory>
#include <string>

#include <nlohmann/json.hpp>

#include <utils/config/mqtt_settings.hpp>
#include <utils/types.hpp>

namespace Everest {

/// \brief Pure virtual interface for framework-local transport communication in EVerest modules.
///
/// Use MqttFrameworkTransport for a broker-backed transport or ShmFrameworkTransport for shared memory.
class FrameworkTransport {
public:
    // forbid copy assignment and copy construction
    FrameworkTransport(FrameworkTransport const&) = delete;
    void operator=(FrameworkTransport const&) = delete;

    virtual ~FrameworkTransport() = default;

    /// \brief connects to the configured framework-local transport
    virtual bool connect() = 0;

    /// \brief disconnects from the configured framework-local transport
    virtual void disconnect() = 0;

    /// \brief publishes the given \p json on the given \p topic with QOS level 0
    virtual void publish(const std::string& topic, const nlohmann::json& json) = 0;

    /// \brief publishes the given \p json on the given \p topic with the given \p qos
    virtual void publish(const std::string& topic, const nlohmann::json& json, QOS qos, bool retain = false) = 0;

    /// \brief publishes the given \p data on the given \p topic with QOS level 0
    virtual void publish(const std::string& topic, const std::string& data) = 0;

    /// \brief publishes the given \p data on the given \p topic with the given \p qos
    virtual void publish(const std::string& topic, const std::string& data, QOS qos, bool retain = false) = 0;

    /// \brief subscribes to the given \p topic with QOS level 0
    virtual void subscribe(const std::string& topic) = 0;

    /// \brief subscribes to the given \p topic with the given \p qos
    virtual void subscribe(const std::string& topic, QOS qos) = 0;

    /// \brief unsubscribes from the given \p topic
    virtual void unsubscribe(const std::string& topic) = 0;

    /// \brief clears any previously published topics that had the retain flag set
    virtual void clear_retained_topics() = 0;

    /// \brief Sends a get request on \p topic and waits for a JSON response, with the given \p qos and \p retries
    virtual nlohmann::json get(const std::string& topic, QOS qos, std::size_t retries = 0) = 0;

    /// \brief Sends a transport request and waits for a JSON response.
    ///
    /// Registers a temporary handler for the response topic in \p request, publishes the request
    /// message, and waits for the corresponding response. Throws EverestTimeoutError on timeout.
    ///
    /// \param request The request containing the response topic, request topic, payload, QoS, and timeout.
    /// \param retries How often the get should be retried on timeout, defaults to 0.
    /// \return The JSON response received.
    virtual nlohmann::json get(const MQTTRequest& request, std::size_t retries = 0) = 0;

    /// \brief Get topic prefix for the "everest" topic
    virtual const std::string& get_everest_prefix() const = 0;

    /// \brief Get topic prefix for external topics
    virtual const std::string& get_external_prefix() const = 0;

    /// \brief Spawn a thread running the transport main loop
    /// \returns a future, which will be fulfilled on thread termination
    virtual std::shared_future<void> spawn_main_loop_thread() = 0;

    /// \returns the main loop future, which will be fulfilled on thread termination
    virtual std::shared_future<void> get_main_loop_future() = 0;

    /// \brief subscribes to \p topic and registers a \p handler called when a message arrives, with the given \p qos
    virtual void register_handler(const std::string& topic, std::shared_ptr<TypedHandler> handler, QOS qos) = 0;

    /// \brief unsubscribes a handler identified by its \p token from the given \p topic
    virtual void unregister_handler(const std::string& topic, const Token& token) = 0;

protected:
    FrameworkTransport() = default;
};

/// \brief Create a real FrameworkTransport backed by the given \p mqtt_settings.
///        Use this instead of constructing MqttFrameworkTransport directly.
std::unique_ptr<FrameworkTransport> make_framework_transport(const MQTTSettings& mqtt_settings);
std::unique_ptr<FrameworkTransport> make_mqtt_transport(const MQTTSettings& mqtt_settings);

} // namespace Everest

#endif // UTILS_FRAMEWORK_TRANSPORT_HPP
