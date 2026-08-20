// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "dc_hlc_limits.hpp"

namespace module {

bool should_update_dc_hlc_limits(Charger::EvseState charger_state, Charger::HlcTerminatePause hlc_link_status) {
    // Once the HLC link was terminated (D-LINK_TERMINATE), no limit update can reach the EV
    // anymore until a new session starts, even if the car is still plugged in.
    if (hlc_link_status == Charger::HlcTerminatePause::Terminate) {
        return false;
    }

    switch (charger_state) {
    case Charger::EvseState::WaitingForAuthentication:
    case Charger::EvseState::PrepareCharging:
    case Charger::EvseState::Charging:
    case Charger::EvseState::ChargingPausedEV:
    case Charger::EvseState::ChargingPausedEVSE:
        return true;
    default:
        return false;
    }
}

} // namespace module
