// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include "Charger.hpp"

namespace module {

/// \brief Decide whether DC HLC limits and targets may still be pushed to the ISO 15118 stack.
/// Once the HLC link was terminated (D-LINK_TERMINATE) or the charger left the active charging
/// states, ev_info only contains stale values from the previous session and must not be applied.
/// \param[in] charger_state - current state of the charger state machine
/// \param[in] hlc_link_status - latched terminate/pause status of the HLC link
/// \returns true while limit updates may be forwarded to the HLC stack
bool should_update_dc_hlc_limits(Charger::EvseState charger_state, Charger::HlcTerminatePause hlc_link_status);

} // namespace module
