// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "SocIntegrator.hpp"

#include "Events.hpp"
#include "FsmContext.hpp"

#include <algorithm>
#include <string>
#include <utility>

namespace module {

namespace {

// Match EvManager: 1 / 60 / 60 / 1000 converts milliseconds to hours so that
// power [W] * factor [h] yields energy delta in [Wh].
// car_simulation.cpp:9
constexpr double MS_FACTOR = (1.0 / 60.0 / 60.0 / 1000.0);

namespace api = everest::lib::API::V1_0::types::ev_simulator;

enum class OnBatteryFull {
    Clamp,
    IdleAtFull,
    StopSession,
    PauseIfIso
};

OnBatteryFull parse_on_battery_full(const std::string& s) {
    if (s == "idle_at_full") {
        return OnBatteryFull::IdleAtFull;
    }
    if (s == "stop_session") {
        return OnBatteryFull::StopSession;
    }
    if (s == "pause_if_iso") {
        return OnBatteryFull::PauseIfIso;
    }
    // Default (and unknown strings) fall back to `clamp` — current behavior.
    return OnBatteryFull::Clamp;
}

bool is_iso_mode(api::ChargeMode m) {
    return m == api::ChargeMode::AcIso2 || m == api::ChargeMode::AcIsoD20 || m == api::ChargeMode::DcIso2 ||
           m == api::ChargeMode::DcIsoD20;
}

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
        // DC: live present current * present voltage. Seeded from
        // cfg.dc_target_current / dc_target_voltage by FsmContext ctor and
        // overwritten at runtime by EvInfo passthrough (apply_passthrough_vars
        // in EvSimRuntime) so SoC tracks the actual delivered DC power rather
        // than the static cfg target.
        return static_cast<double>(ctx.vars.dc_present_current_a) * static_cast<double>(ctx.vars.dc_present_voltage_v);
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
    double power = instantaneous_power_w(ctx);

    // on_battery_full policy: gate positive power when SoC has reached
    // cfg.battery_full_threshold_pct. `clamp` is a no-op here (legacy
    // integrate-then-clamp); the other three policies zero out incoming
    // charge power so the battery does not keep accumulating. Discharge
    // (power < 0) is always allowed so the battery can drain back below
    // the threshold.
    const auto policy = parse_on_battery_full(ctx.cfg.on_battery_full);
    const double threshold = ctx.cfg.battery_full_threshold_pct;
    const bool is_full_pre = static_cast<double>(ctx.vars.soc_pct) >= threshold;
    if (is_full_pre && power > 0.0 && policy != OnBatteryFull::Clamp) {
        power = 0.0;
    }

    // Always integrate then clamp. Power may be negative when BPT / V2X
    // discharge is active (negative `charging_current_a` for AC or negative
    // `dc_present_current_a` for DC), so the legacy car_simulation.cpp:135-139
    // short-circuit (skip accumulation when already over capacity) is dropped:
    // it silently dropped discharge energy when the battery started above
    // capacity. The post-integration clamp keeps battery_charge_wh in
    // [0, capacity], handling overshoot on charge and underflow on discharge
    // symmetrically.
    ctx.vars.battery_charge_wh += static_cast<float>(power * factor);
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

    // Edge detection for stop_session / pause_if_iso. Rising edge fires the
    // FSM event exactly once; was_full latches and clears when SoC drops
    // back below the threshold so the policy can re-arm.
    const bool is_full_post = static_cast<double>(ctx.vars.soc_pct) >= threshold;
    const bool rising_edge = !ctx.vars.was_full && is_full_post;
    if (rising_edge) {
        switch (policy) {
        case OnBatteryFull::StopSession: {
            Event ev{};
            ev.kind = EventKind::StopSession;
            ctx.enqueue(std::move(ev));
            break;
        }
        case OnBatteryFull::PauseIfIso:
            // ISO-only: fall back to idle_at_full semantics (no event) in AcIec.
            if (ctx.vars.charge_mode && is_iso_mode(*ctx.vars.charge_mode)) {
                Event ev{};
                ev.kind = EventKind::PauseSession;
                ctx.enqueue(std::move(ev));
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
