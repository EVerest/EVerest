// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <functional>
#include <optional>
#include <string>
#include <utility>

#include <framework/everest.hpp>

namespace everest::lib::API::Mqtt {

class MqttProviderInterface {
public:
    virtual ~MqttProviderInterface() = default;

    virtual void publish(const std::string& topic, const std::string& data) = 0;
    virtual void publish(const std::string& topic, bool data) = 0;

    // without this overload, a string literal converts to bool and publishes "true"
    void publish(const std::string& topic, const char* data) {
        publish(topic, std::string{data});
    }

    // a message without data is an empty JSON object, never an empty payload
    void publish(const std::string& topic) {
        publish(topic, std::string{"{}"});
    }

    // a request without replyTo does not want its reply
    template <class DataT> void publish_reply(const std::optional<std::string>& reply_to, DataT&& data) {
        if (reply_to) {
            publish(*reply_to, std::forward<DataT>(data));
        }
    }

    virtual Everest::UnsubscribeToken subscribe(const std::string& topic,
                                                std::function<void(std::string)> handler) const = 0;
};

} // namespace everest::lib::API::Mqtt
