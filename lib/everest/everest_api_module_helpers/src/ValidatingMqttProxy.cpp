// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <everest_api_module_helpers/ValidatingMqttProxy.hpp>

#include <utility>

#include <everest/logging.hpp>
#include <everest_api_types/utilities/Topics.hpp>

namespace everest::lib::API::Mqtt {

ValidatingMqttProxy::ValidatingMqttProxy(Everest::MqttProvider& provider_) : provider(provider_) {
}

void ValidatingMqttProxy::publish(const std::string& topic, const std::string& data) {
    if (Topics::is_publishable(topic)) {
        provider.publish(topic, data);
    } else {
        EVLOG_warning << "ValidatingMqttProxy: Dropped data as topic '" << topic << "' is invalid";
    }
}

void ValidatingMqttProxy::publish(const std::string& topic, bool data) {
    if (Topics::is_publishable(topic)) {
        provider.publish(topic, data);
    } else {
        EVLOG_warning << "ValidatingMqttProxy: Dropped data as topic '" << topic << "' is invalid";
    }
}

Everest::UnsubscribeToken ValidatingMqttProxy::subscribe(const std::string& topic,
                                                         std::function<void(std::string)> handler) const {
    return provider.subscribe(topic, std::move(handler));
}

} // namespace everest::lib::API::Mqtt
