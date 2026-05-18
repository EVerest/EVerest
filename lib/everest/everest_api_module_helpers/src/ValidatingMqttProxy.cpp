// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#include <everest_api_module_helpers/ValidatingMqttProxy.hpp>

#include <cstddef>
#include <string_view>
#include <utility>

#include <everest/logging.hpp>

namespace everest::lib::API::Mqtt {

ValidatingMqttProxy::ValidatingMqttProxy(Everest::MqttProvider& provider_) : provider(provider_) {
}

void ValidatingMqttProxy::publish(const std::string& topic, const std::string& data) {
    if (is_topic_valid(topic)) {
        provider.publish(topic, data);
    } else {
        EVLOG_warning << "ValidatingMqttProxy: Droped '" << data << "' as topic '" << topic << "' invalid";
    }
}

void ValidatingMqttProxy::publish(const std::string& topic, bool data) {
    if (is_topic_valid(topic)) {
        provider.publish(topic, data);
    } else {
        EVLOG_warning << "ValidatingMqttProxy: Droped '" << data << "' as topic '" << topic << "' invalid";
    }
}

Everest::UnsubscribeToken ValidatingMqttProxy::subscribe(const std::string& topic,
                                                         std::function<void(std::string)> handler) const {
    return provider.subscribe(topic, std::move(handler));
}

bool ValidatingMqttProxy::is_topic_valid(std::string_view topic) {
    static constexpr std::size_t max_topic_length = 65535;
    if (topic.empty() || topic.length() > max_topic_length) {
        return false;
    }
    if (topic.find('\0') != std::string_view::npos || topic.find_first_of("+#") != std::string_view::npos) {
        return false;
    }
    return true;
}

} // namespace everest::lib::API::Mqtt
