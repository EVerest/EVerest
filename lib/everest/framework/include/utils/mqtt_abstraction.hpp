// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#ifndef UTILS_MQTT_ABSTRACTION_HPP
#define UTILS_MQTT_ABSTRACTION_HPP

#include <utils/framework_transport.hpp>

namespace Everest {

using MQTTAbstraction = FrameworkTransport;

inline std::unique_ptr<MQTTAbstraction> make_mqtt_abstraction(const MQTTSettings& mqtt_settings) {
    return make_framework_transport(mqtt_settings);
}

} // namespace Everest

#endif // UTILS_MQTT_ABSTRACTION_HPP
