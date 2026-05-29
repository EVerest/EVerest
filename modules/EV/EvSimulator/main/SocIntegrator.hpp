// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

namespace module {

class FsmContext;

// Integrates battery_charge_wh / soc_pct in FsmContext::vars based on the
// current charge mode, current/voltage, and the configured tick interval.
//
// Ported verbatim from `CarSimulation::simulate_soc` in
// modules/EV/EvManager/main/car_simulation.cpp (lines 107-162) to preserve
// behavioural parity with the legacy EvManager. After integration the cached
// soc_pct (Decision #39: derived from battery_charge_wh source-of-truth) is
// republished via the external/internal ev_info topics and forwarded to ISO
// via iso_update_soc (no-op when the action is not wired, e.g. AcIec mode).
//
// step() is invoked from EvSimRuntime::on_tick() (T-C2 wires the tick fd).
// tick_fd is armed only by Charging.enter() and disarmed by Charging.leave(),
// so step() is invoked only while state == Charging.
class SocIntegrator {
public:
    static void step(FsmContext& ctx);
};

} // namespace module
