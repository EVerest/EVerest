// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <string>

namespace module {

class FsmContext;

// Policy selector for what SocIntegrator does when SoC crosses the configured
// full threshold. Lifted into module:: (was in an anonymous namespace) so the
// parsed policy can be cached on FsmContext at construction time and consumed
// without re-parsing the config string on every tick.
enum class OnBatteryFull {
    Clamp,
    IdleAtFull,
    StopSession,
    PauseIfIso,
};

// Map cfg.on_battery_full string to OnBatteryFull. Throws std::invalid_argument
// on an unrecognized value so a typo in the YAML config fails loudly at module
// init rather than silently downgrading the policy to Clamp on every tick.
OnBatteryFull parse_on_battery_full(const std::string& s);

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
// soc_step() is invoked from EvSimRuntime::on_tick(). The tick fd is armed
// only by Charging.enter() and disarmed by Charging.leave(), so soc_step()
// runs only while state == Charging.
void soc_step(FsmContext& ctx);

} // namespace module
