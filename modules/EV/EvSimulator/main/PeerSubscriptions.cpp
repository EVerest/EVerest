// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "PeerSubscriptions.hpp"

#include "Events.hpp"

#include <generated/types/board_support_common.hpp>
#include <generated/types/evse_manager.hpp>
#include <generated/types/iso15118.hpp>
#include <generated/types/slac.hpp>

namespace module {

void setup_peer_subscriptions(EvSimRuntime& rt, EvSimulator& mod) {
    // BSP — always present (ev_board_support requirement is required, min_connections=1).
    mod.r_ev_board_support->subscribe_bsp_event([&rt](const ::types::board_support_common::BspEvent& ev) {
        rt.enqueue(Event{EventKind::BspEvent, BspEventPayload{ev}});
    });
    mod.r_ev_board_support->subscribe_bsp_measurement([&rt](const ::types::board_support_common::BspMeasurement& m) {
        rt.enqueue(Event{EventKind::BspMeasurement,
                         BspMeasurementPayload{m.cp_pwm_duty_cycle, m.rcd_current_mA, m.proximity_pilot}});
    });
    mod.r_ev_board_support->subscribe_ev_info([&rt](const ::types::evse_manager::EVInfo& info) {
        rt.enqueue(Event{EventKind::EvInfo, EvInfoPayload{info}});
    });

    // SLAC — optional.
    if (!mod.r_slac.empty()) {
        mod.r_slac[0]->subscribe_state([&rt](const ::types::slac::State& s) {
            rt.enqueue(Event{EventKind::SlacState, SlacStatePayload{::types::slac::state_to_string(s)}});
        });
    }

    // ISO 15118 — optional.
    if (!mod.r_ev.empty()) {
        auto* iso = mod.r_ev[0].get();

        // Bool-valued: only forward the positive edge to the FSM.
        iso->subscribe_ev_power_ready([&rt](const bool& ready) {
            if (ready) {
                rt.enqueue(Event{EventKind::IsoPowerReady, {}});
            }
        });

        // void()-valued (no payload): single-shot edges from the charger side.
        iso->subscribe_stop_from_charger([&rt]() { rt.enqueue(Event{EventKind::IsoStopFromCharger, {}}); });
        iso->subscribe_v2g_session_finished([&rt]() { rt.enqueue(Event{EventKind::IsoV2GFinished, {}}); });
        iso->subscribe_dc_power_on([&rt]() { rt.enqueue(Event{EventKind::IsoDcPowerOn, {}}); });
        iso->subscribe_pause_from_charger([&rt]() { rt.enqueue(Event{EventKind::IsoPauseFromCharger, {}}); });

        // AC negotiation values — forwarded to the FSM as kind-only events; no
        // state currently consumes the payload (states list these kinds in a
        // fall-through case to satisfy -Werror=switch). Subscribed so the FSM
        // queue receives the wake.
        iso->subscribe_ac_evse_max_current([&rt](const double&) { rt.enqueue(Event{EventKind::IsoAcMaxCurrent, {}}); });
        iso->subscribe_ac_evse_target_power([&rt](const ::types::iso15118::AcTargetPower&) {
            rt.enqueue(Event{EventKind::IsoAcTargetPower, {}});
        });
    }
}

} // namespace module
