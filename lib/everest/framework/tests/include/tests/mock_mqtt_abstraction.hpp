// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <future>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <utils/mqtt_abstraction.hpp>

namespace Everest {
namespace tests {

/// \brief A mock MQTTAbstraction for unit testing.
///
/// Overrides get() to return a pre-configured JSON response and records
/// all publish() and register_handler() calls for inspection in tests.
class MockMQTTAbstraction : public MQTTAbstraction {
public:
    explicit MockMQTTAbstraction(std::string everest_prefix = "everest/") :
        m_everest_prefix(std::move(everest_prefix)) {
    }

    // --- Configurable behaviour ---

    /// \brief Set the JSON that the next get() call will return.
    void set_get_response(nlohmann::json response) {
        m_get_response = std::move(response);
    }

    // --- Recorded state ---

    /// \brief Returns the MQTTRequest passed to the last get() call, if any.
    const std::optional<MQTTRequest>& last_get_request() const {
        return m_last_get_request;
    }

    /// \brief Returns all (topic, payload) pairs passed to publish().
    const std::vector<std::pair<std::string, nlohmann::json>>& published() const {
        return m_published;
    }

    /// \brief Returns all handlers registered via register_handler(), keyed by topic.
    const std::unordered_map<std::string, std::shared_ptr<TypedHandler>>& registered_handlers() const {
        return m_handlers;
    }

    // --- MQTTAbstraction overrides ---

    nlohmann::json get(const MQTTRequest& request, std::size_t /*retries*/ = 0) override {
        m_last_get_request = request;
        return m_get_response;
    }

    nlohmann::json get(const std::string& topic, QOS qos, std::size_t retries = 0) override {
        MQTTRequest request;
        request.response_topic = topic;
        request.qos = qos;
        return get(request, retries);
    }

    void publish(const std::string& topic, const nlohmann::json& json) override {
        m_published.emplace_back(topic, json);
    }

    void publish(const std::string& topic, const nlohmann::json& json, QOS /*qos*/, bool /*retain*/ = false) override {
        m_published.emplace_back(topic, json);
    }

    void publish(const std::string& topic, const std::string& data) override {
        m_published.emplace_back(topic, nlohmann::json(data));
    }

    void publish(const std::string& topic, const std::string& data, QOS /*qos*/, bool /*retain*/ = false) override {
        m_published.emplace_back(topic, nlohmann::json(data));
    }

    void register_handler(const std::string& topic, std::shared_ptr<TypedHandler> handler, QOS /*qos*/) override {
        m_handlers[topic] = std::move(handler);
    }

    void unregister_handler(const std::string& topic, const Token& /*token*/) override {
        m_handlers.erase(topic);
    }

    bool connect() override {
        return true;
    }
    void disconnect() override {
    }
    void subscribe(const std::string& /*topic*/) override {
    }
    void subscribe(const std::string& /*topic*/, QOS /*qos*/) override {
    }
    void unsubscribe(const std::string& /*topic*/) override {
    }
    void clear_retained_topics() override {
    }
    std::shared_future<void> spawn_main_loop_thread() override {
        return {};
    }
    std::shared_future<void> get_main_loop_future() override {
        return {};
    }

    const std::string& get_everest_prefix() const override {
        return m_everest_prefix;
    }

    const std::string& get_external_prefix() const override {
        static const std::string empty;
        return empty;
    }

private:
    std::string m_everest_prefix;
    nlohmann::json m_get_response;
    std::optional<MQTTRequest> m_last_get_request;
    std::vector<std::pair<std::string, nlohmann::json>> m_published;
    std::unordered_map<std::string, std::shared_ptr<TypedHandler>> m_handlers;
};

} // namespace tests
} // namespace Everest
