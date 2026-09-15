// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include "../EvSimulator.hpp"
#include "EvSimRuntime.hpp"

namespace module {

// Subscribes the EvSimulator module to its 11 peer interface variables:
//   - ev_board_support: bsp_event, bsp_measurement, ev_info  (3, always present)
//   - ev_slac[0]:       state                                (1, optional)
//   - ev[0] (ISO15118): ev_power_ready, ac_evse_max_current,
//                       ac_evse_target_power, stop_from_charger,
//                       v2g_session_finished, dc_power_on,
//                       pause_from_charger                   (7, optional)
// Each subscription enqueues the matching `Event` onto the FSM runtime queue.
void setup_peer_subscriptions(EvSimRuntime& rt, EvSimulator& mod);

} // namespace module
