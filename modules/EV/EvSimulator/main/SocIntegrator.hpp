// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

namespace module {

class FsmContext;

// Integrates battery_charge_wh / soc_pct in FsmContext::vars based on the
// current charge mode, current/voltage, and the configured tick interval.
//
// Ported from `CarSimulation::simulate_soc` in
// modules/EV/EvManager/main/car_simulation.cpp to preserve behavioral parity
// with the legacy EvManager. battery_charge_wh is the source of truth;
// soc_pct is the cached derived value (100 * charge_wh / capacity_wh) and is
// republished via the external/internal ev_info topics after each step and
// forwarded to ISO via iso_update_soc (no-op when the action is not wired,
// e.g. AcIec mode).
//
// step() is invoked from EvSimRuntime::on_tick(). The tick fd is armed only
// by Charging.enter() and disarmed by Charging.leave(), so step() runs only
// while state == Charging.
class SocIntegrator {
public:
    static void step(FsmContext& ctx);
};

} // namespace module
