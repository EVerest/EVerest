// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "PeerSubscriptions.hpp"

#include "Events.hpp"

#include <generated/types/board_support_common.hpp>
#include <generated/types/evse_manager.hpp>
#include <generated/types/slac.hpp>

namespace module {

void setup_peer_subscriptions(EvCtlRuntime& rt, EvController& mod) {
    // BSP: always present (ev_board_support requirement is required, min_connections=1).
    mod.r_ev_board_support->subscribe_bsp_event(
        [&rt](const ::types::board_support_common::BspEvent& ev) { rt.enqueue(Event{BspEventPayload{ev}}); });
    mod.r_ev_board_support->subscribe_bsp_measurement([&rt](const ::types::board_support_common::BspMeasurement& m) {
        rt.enqueue(Event{BspMeasurementPayload{m.cp_pwm_duty_cycle, m.rcd_current_mA, m.proximity_pilot}});
    });
    mod.r_ev_board_support->subscribe_ev_info(
        [&rt](const ::types::evse_manager::EVInfo& info) { rt.enqueue(Event{EvInfoPayload{info}}); });

    // SLAC: optional.
    if (!mod.r_slac.empty()) {
        mod.r_slac[0]->subscribe_state([&rt](const ::types::slac::State& s) {
            rt.enqueue(Event{SlacStatePayload{::types::slac::state_to_string(s)}});
        });
    }
}

} // namespace module
