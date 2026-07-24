// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "EvseManagerSimulator.hpp"

namespace module {

void EvseManagerSimulator::init() {
    invoke_init(*p_evse);
    invoke_init(*p_energy_grid);
}

void EvseManagerSimulator::ready() {
    invoke_ready(*p_evse);
    invoke_ready(*p_energy_grid);
}

} // namespace module
