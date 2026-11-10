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

// Where the SoC comes from. Internal integrates the delivered power, External takes the SoC and
// battery capacity from the board's ev_info directly.
enum class SocSource {
    Internal,
    External,
};

// Map cfg.soc_source to SocSource. Throws std::invalid_argument on an unrecognized value.
SocSource parse_soc_source(const std::string& s);

// Ported from CarSimulation::simulate_soc to keep parity with EvManager.
// With SocSource::Internal, battery_charge_wh is the source of truth and soc_pct the cached derived
// value. With SocSource::External, soc_pct is the source of truth and nothing is integrated.
//
// Called from EvCtlRuntime::on_tick(). The tick fd is armed only by Charging.enter() and
// disarmed by Charging.leave(), so this runs only while state == Charging.
void soc_step(FsmContext& ctx);

} // namespace module
