// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <functional>
#include <string>

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

    virtual Everest::UnsubscribeToken subscribe(const std::string& topic,
                                                std::function<void(std::string)> handler) const = 0;
};

} // namespace everest::lib::API::Mqtt
