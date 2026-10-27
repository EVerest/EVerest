// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "InfyPower_BEG1K075G.hpp"

namespace module {

void InfyPower_BEG1K075G::init() {
    invoke_init(*p_main);

    // Opened after the implementation subscribed to the driver signals, so it sees the first connection state.
    if (!acdc.open_device(config.can_device)) {
        EVLOG_AND_THROW(EVEXCEPTION(Everest::EverestConfigError, "Invalid CAN interface name ", config.can_device));
    }
}

void InfyPower_BEG1K075G::ready() {
    invoke_ready(*p_main);
}

} // namespace module
