// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 - 2022 Pionix GmbH and Contributors to EVerest
#include "EnergyNode.hpp"

namespace module {

void EnergyNode::init() {
    // Parse comma-separated external_consumer_ids into the vector
    if (!config.external_consumer_ids.empty()) {
        std::istringstream ss(config.external_consumer_ids);
        std::string id;
        while (std::getline(ss, id, ',')) {
            if (!id.empty()) {
                external_consumers.push_back(id);
            }
        }
    }
    invoke_init(*p_energy_grid);
    invoke_init(*p_external_limits);
}

void EnergyNode::ready() {
    invoke_ready(*p_energy_grid);
    invoke_ready(*p_external_limits);
}

} // namespace module
