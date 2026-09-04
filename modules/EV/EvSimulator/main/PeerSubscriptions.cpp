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
    mod.r_ev_board_support->subscribe_bsp_event(
        [&rt](const ::types::board_support_common::BspEvent& ev) { rt.enqueue(Event{BspEventPayload{ev}}); });
    mod.r_ev_board_support->subscribe_bsp_measurement([&rt](const ::types::board_support_common::BspMeasurement& m) {
        rt.enqueue(Event{BspMeasurementPayload{m.cp_pwm_duty_cycle, m.rcd_current_mA, m.proximity_pilot}});
    });
    mod.r_ev_board_support->subscribe_ev_info(
        [&rt](const ::types::evse_manager::EVInfo& info) { rt.enqueue(Event{EvInfoPayload{info}}); });

    // SLAC — optional.
    if (!mod.r_slac.empty()) {
        mod.r_slac[0]->subscribe_state([&rt](const ::types::slac::State& s) {
            rt.enqueue(Event{SlacStatePayload{::types::slac::state_to_string(s)}});
        });
    }

    // ISO 15118 — optional.
    if (!mod.r_ev.empty()) {
        auto* iso = mod.r_ev[0].get();

        // Bool-valued: only forward the positive edge to the FSM.
        iso->subscribe_ev_power_ready([&rt](const bool& ready) {
            if (ready) {
                rt.enqueue(Event{IsoPowerReadyEvt{}});
            }
        });

        // void()-valued (no payload): single-shot edges from the charger side.
        iso->subscribe_stop_from_charger([&rt]() { rt.enqueue(Event{IsoStopFromChargerEvt{}}); });
        iso->subscribe_v2g_session_finished([&rt]() { rt.enqueue(Event{IsoV2GFinishedEvt{}}); });
        iso->subscribe_dc_power_on([&rt]() { rt.enqueue(Event{IsoDcPowerOnEvt{}}); });
        iso->subscribe_pause_from_charger([&rt]() { rt.enqueue(Event{IsoPauseFromChargerEvt{}}); });

        // AC negotiation values — the EVSE-communicated AC current ceiling
        // (ac_evse_max_current) and target power are forwarded with their
        // payloads so the FSM can clamp the applied charge current against
        // them.
        iso->subscribe_ac_evse_max_current([&rt](const double& max_current) {
            rt.enqueue(Event{IsoAcMaxCurrentEvt{static_cast<float>(max_current)}});
        });
        iso->subscribe_ac_evse_target_power([&rt](const ::types::iso15118::AcTargetPower& target_power) {
            rt.enqueue(Event{IsoAcTargetPowerEvt{target_power}});
        });

        // DC live present current / voltage from the SECC. Forwarded into the
        // SoC integrator's closed-loop input: a reported present current
        // populates vars.evse_dc_present_current_a (the open-loop fallback is
        // used while it stays nullopt).
        iso->subscribe_dc_evse_present_current(
            [&rt](const double& a) { rt.enqueue(Event{DcEvsePresentCurrentPayload{a}}); });
        iso->subscribe_dc_evse_present_voltage(
            [&rt](const double& v) { rt.enqueue(Event{DcEvsePresentVoltagePayload{v}}); });
    }
}

} // namespace module
