// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "TemperatureSensorSimulator.hpp"

namespace module {

void TemperatureSensorSimulator::init() {
    invoke_init(*p_temperature_sensor);
}

void TemperatureSensorSimulator::ready() {
    invoke_ready(*p_temperature_sensor);
}

} // namespace module
