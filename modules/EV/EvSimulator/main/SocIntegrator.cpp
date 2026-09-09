// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "SocIntegrator.hpp"

#include "Events.hpp"
#include "FsmContext.hpp"

#include <everest/logging.hpp>

#include <algorithm>
#include <stdexcept>
#include <string>
#include <utility>

namespace module {

// Validates cfg.on_battery_full and returns the parsed policy. Throws
// std::invalid_argument on an unknown value so the typo surfaces at module
// init (FsmContext ctor) rather than silently degrading to Clamp on every
// tick. Recognized strings match the manifest enum: clamp / idle_at_full /
// stop_session / pause_if_iso.
OnBatteryFull parse_on_battery_full(const std::string& s) {
    if (s == "clamp") {
        return OnBatteryFull::Clamp;
    }
    if (s == "idle_at_full") {
        return OnBatteryFull::IdleAtFull;
    }
    if (s == "stop_session") {
        return OnBatteryFull::StopSession;
    }
    if (s == "pause_if_iso") {
        return OnBatteryFull::PauseIfIso;
    }
    throw std::invalid_argument("EvSimulator: unknown on_battery_full value '" + s +
                                "'; expected one of clamp / idle_at_full / stop_session / pause_if_iso");
}

namespace {

// Match EvManager: 1 / 60 / 60 / 1000 converts milliseconds to hours so that
// power [W] * factor [h] yields energy delta in [Wh].
// car_simulation.cpp:9
constexpr double MS_FACTOR = (1.0 / 60.0 / 60.0 / 1000.0);

namespace api = everest::lib::API::V1_0::types::ev_simulator;

// Compute the instantaneous power draw in Watts according to the current
// charge mode. Mirrors car_simulation.cpp:114-133.
double instantaneous_power_w(const FsmContext& ctx) {
    const auto cm = ctx.vars.charge_mode();
    if (!cm) {
        return 0.0;
    }
    switch (*cm) {
    case api::ChargeMode::AcIec:
    case api::ChargeMode::AcIso2:
    case api::ChargeMode::AcIsoD20:
        // AC: applied current * voltage, times 3 for three-phase. The applied
        // current is the EV desired clamped to any EVSE limit
        // (effective_ac_current_a), so SoC tracks delivered energy: a 0 A
        // ceiling yields ~0 delivered power even though the EV still desires
        // its configured current. Parallels the DC delivered-current path below.
        return static_cast<double>(ctx.effective_ac_current_a()) * ctx.cfg.ac_nominal_voltage *
               (ctx.vars.three_phases ? 3.0 : 1.0);
    case api::ChargeMode::DcIso2:
    case api::ChargeMode::DcIsoD20:
        // DC: delivered current * present voltage. effective_dc_current_a()
        // returns the live measured present current when a peer EvInfo has
        // reported one (closed loop, un-clamped so BPT discharge stays
        // negative), and otherwise falls back open-loop to the configured
        // cfg.dc_target_current so SoC still advances with no live producer.
        return static_cast<double>(ctx.effective_dc_current_a()) * static_cast<double>(ctx.vars.dc_present_voltage_v);
    }
    return 0.0;
}

} // namespace

void soc_step(FsmContext& ctx) {
    // Guard against a misconfigured battery_capacity_wh (sourced from
    // cfg.dc_energy_capacity). A zero or negative capacity divides by zero
    // when deriving SoC below; NaN then compares false to both clamp branches
    // and propagates onto e2m/ev_info. Log once and bail out.
    if (ctx.vars.battery_capacity_wh <= 0.0f) {
        static bool warned = false;
        if (!warned) {
            EVLOG_error << "EvSimulator: battery_capacity_wh <= 0 (cfg.dc_energy_capacity); SocIntegrator skipping";
            warned = true;
        }
        return;
    }

    // ms is the configured tick interval; EvSimRuntime::on_tick() invokes
    // step() once per tick_fd expiry. EvManager originally derived `ms` from
    // wall-clock delta (steady_clock - timepoint_last_update), but the FSM
    // tick is uniform and equal to cfg.tick_interval_ms, so use that directly.
    const double ms = static_cast<double>(ctx.cfg.tick_interval_ms);
    const double factor = MS_FACTOR * ms;
    double power = instantaneous_power_w(ctx);

    // Policy is parsed and cached once in FsmContext's ctor so a typo in
    // cfg.on_battery_full fails at module init rather than silently downgrading
    // to Clamp on every tick.
    const auto policy = ctx.on_battery_full_policy;
    const double threshold = ctx.cfg.battery_full_threshold_pct;

    // Always integrate then clamp. Power may be negative when BPT / V2X
    // discharge is active (negative `charging_current_a` for AC or negative
    // live `evse_dc_present_current_a` for DC), so the legacy car_simulation.cpp:135-139
    // short-circuit (skip accumulation when already over capacity) is dropped:
    // it silently dropped discharge energy when the battery started above
    // capacity. The post-integration clamp keeps battery_charge_wh in
    // [0, capacity], handling overshoot on charge and underflow on discharge
    // symmetrically.
    ctx.vars.battery_charge_wh += static_cast<float>(power * factor);
    ctx.vars.battery_charge_wh = std::clamp(ctx.vars.battery_charge_wh, 0.0f, ctx.vars.battery_capacity_wh);

    // on_battery_full policy: for every policy except `clamp`, the battery
    // must not hold charge above cfg.battery_full_threshold_pct. Trimming the
    // accumulated charge back to the threshold *after* integration (rather
    // than zeroing power *before*, gated on the pre-tick SoC) means the
    // threshold is reached exactly on the crossing tick with no one-tick
    // overshoot, while the rising-edge detection below still fires that same
    // tick. Discharge (power < 0) is never trimmed so the pack can drain
    // back below the threshold and re-arm the edge. `clamp` keeps the legacy
    // integrate-then-capacity-clamp behavior untouched.
    if (policy != OnBatteryFull::Clamp && power > 0.0) {
        const auto threshold_wh =
            static_cast<float>(threshold / 100.0 * static_cast<double>(ctx.vars.battery_capacity_wh));
        if (ctx.vars.battery_charge_wh > threshold_wh) {
            ctx.vars.battery_charge_wh = threshold_wh;
        }
    }

    // car_simulation.cpp:141-147 — derive SoC, clamp to [0, 100].
    double soc =
        (static_cast<double>(ctx.vars.battery_charge_wh) / static_cast<double>(ctx.vars.battery_capacity_wh)) * 100.0;
    if (soc > 100.0) {
        soc = 100.0;
    } else if (soc <= 0.0) {
        soc = 0.0;
    }
    ctx.vars.soc_pct = static_cast<float>(soc);

    // Edge detection for stop_session / pause_if_iso. Rising edge fires the
    // FSM event exactly once; was_full latches and clears when SoC drops
    // back below the threshold so the policy can re-arm.
    const bool is_full_post = static_cast<double>(ctx.vars.soc_pct) >= threshold;
    const bool rising_edge = !ctx.vars.was_full && is_full_post;
    if (rising_edge) {
        switch (policy) {
        case OnBatteryFull::StopSession: {
            ctx.enqueue(Event{StopSessionCmd{}});
            break;
        }
        case OnBatteryFull::PauseIfIso:
            // ISO-only: fall back to idle_at_full semantics (no event) in AcIec.
            if (const auto m = ctx.vars.charge_mode(); m && is_iso_mode(*m)) {
                ctx.enqueue(Event{PauseSessionCmd{}});
            }
            break;
        case OnBatteryFull::Clamp:
        case OnBatteryFull::IdleAtFull:
            break;
        }
    }
    ctx.vars.was_full = is_full_post;

    // Publish updated SoC / energy state.
    ctx.publish_e2m_ev_info();
    ctx.publish_internal_ev_info();
    ctx.iso_update_soc(ctx.vars.soc_pct);
}

} // namespace module
