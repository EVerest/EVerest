// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "SocIntegrator.hpp"

#include "FsmContext.hpp"

#include <algorithm>

namespace module {

namespace {

// Match EvManager: 1 / 60 / 60 / 1000 converts milliseconds to hours so that
// power [W] * factor [h] yields energy delta in [Wh].
// car_simulation.cpp:9
constexpr double MS_FACTOR = (1.0 / 60.0 / 60.0 / 1000.0);

namespace api = everest::lib::API::V1_0::types::ev_simulator;

// Compute the instantaneous power draw in Watts according to the current
// charge mode. Mirrors car_simulation.cpp:114-133.
double instantaneous_power_w(const FsmContext& ctx) {
    if (!ctx.vars.charge_mode) {
        return 0.0;
    }
    switch (*ctx.vars.charge_mode) {
    case api::ChargeMode::AcIec:
    case api::ChargeMode::AcIso2:
    case api::ChargeMode::AcIsoD20:
        // AC: current * voltage, times 3 for three-phase. car_simulation.cpp:118-127
        return static_cast<double>(ctx.vars.charging_current_a) * ctx.cfg.ac_nominal_voltage *
               (ctx.vars.three_phases ? 3.0 : 1.0);
    case api::ChargeMode::DcIso2:
    case api::ChargeMode::DcIsoD20:
        // DC: configured target current * target voltage. car_simulation.cpp:128-132
        return static_cast<double>(ctx.cfg.dc_target_current) * static_cast<double>(ctx.cfg.dc_target_voltage);
    }
    return 0.0;
}

} // namespace

void SocIntegrator::step(FsmContext& ctx) {
    // ms is the configured tick interval; EvSimRuntime::on_tick() invokes
    // step() once per tick_fd expiry. EvManager originally derived `ms` from
    // wall-clock delta (steady_clock - timepoint_last_update), but the FSM
    // tick is uniform and equal to cfg.tick_interval_ms, so use that directly.
    const double ms = static_cast<double>(ctx.cfg.tick_interval_ms);
    const double factor = MS_FACTOR * ms;
    const double power = instantaneous_power_w(ctx);

    // car_simulation.cpp:135-139 — clamp to capacity if already over, else
    // accumulate energy.
    if (ctx.vars.battery_charge_wh > ctx.vars.battery_capacity_wh) {
        ctx.vars.battery_charge_wh = ctx.vars.battery_capacity_wh;
    } else {
        ctx.vars.battery_charge_wh += static_cast<float>(power * factor);
    }

    // Decision #39: battery_charge_wh is the source-of-truth; soc_pct is the
    // cached derived value updated in lockstep. Defensive clamp on
    // battery_charge_wh to keep the derived value in [0, capacity].
    ctx.vars.battery_charge_wh = std::clamp(ctx.vars.battery_charge_wh, 0.0f, ctx.vars.battery_capacity_wh);

    // car_simulation.cpp:141-147 — derive SoC, clamp to [0, 100].
    double soc =
        (static_cast<double>(ctx.vars.battery_charge_wh) / static_cast<double>(ctx.vars.battery_capacity_wh)) * 100.0;
    if (soc > 100.0) {
        soc = 100.0;
    } else if (soc <= 0.0) {
        soc = 0.0;
    }
    ctx.vars.soc_pct = static_cast<float>(soc);

    // Publish updated SoC / energy state.
    ctx.publish_e2m_ev_info();
    ctx.publish_internal_ev_info();
    ctx.iso_update_soc(ctx.vars.soc_pct);
}

} // namespace module
