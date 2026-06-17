// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2025 Pionix GmbH and Contributors to EVerest

#include "CarloGavazzi_EM580.hpp"

namespace module {

void CarloGavazzi_EM580::init() {
    invoke_init(*p_main);
    invoke_init(*p_temperature_sensor);
}

void CarloGavazzi_EM580::ready() {
    invoke_ready(*p_main);
    invoke_ready(*p_temperature_sensor);
}

} // namespace module
