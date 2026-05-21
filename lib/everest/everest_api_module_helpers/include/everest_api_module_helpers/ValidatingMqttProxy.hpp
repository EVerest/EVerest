// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#pragma once

#include <functional>
#include <string>

#include <framework/ModuleAdapter.hpp>

#include <everest_api_types/utilities/MqttProviderInterface.hpp>

namespace everest::lib::API::Mqtt {

class ValidatingMqttProxy : public MqttProviderInterface {
public:
    explicit ValidatingMqttProxy(Everest::MqttProvider& provider_);

    void publish(const std::string& topic, const std::string& data) override;
    void publish(const std::string& topic, bool data) override;

    Everest::UnsubscribeToken subscribe(const std::string& topic,
                                        std::function<void(std::string)> handler) const override;

private:
    Everest::MqttProvider& provider;

    [[nodiscard]] static bool is_topic_valid(std::string_view topic);
};

} // namespace everest::lib::API::Mqtt
