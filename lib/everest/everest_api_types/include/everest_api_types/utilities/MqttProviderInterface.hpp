// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

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

    virtual Everest::UnsubscribeToken subscribe(const std::string& topic,
                                                std::function<void(std::string)> handler) const = 0;
};

} // namespace everest::lib::API::Mqtt
