// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "RampInterpolator.hpp"

#include "FsmContext.hpp"

#include <everest/logging.hpp>

#include <algorithm>

namespace module {

void ramp_step(FsmContext& ctx, std::chrono::steady_clock::time_point now) {
    if (!ctx.vars.active_ramp.has_value()) {
        return;
    }
    auto& r = *ctx.vars.active_ramp;
    if (now >= r.end_at) {
        ctx.set_desired_ac_params(r.target_a, r.three_phases);
        ctx.vars.active_ramp.reset();
        return;
    }
    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - r.start_at).count();
    const auto total_ms = std::chrono::duration_cast<std::chrono::milliseconds>(r.end_at - r.start_at).count();
    if (total_ms <= 0) {
        // Defensive: degenerate ramp (end_at <= start_at). Snap to target so
        // the BSP still sees the final value, but log so the upstream caller
        // (the ramp builder in SetChargingCurrent) is visible as the source.
        const auto start_ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(r.start_at.time_since_epoch()).count();
        const auto end_ms = std::chrono::duration_cast<std::chrono::milliseconds>(r.end_at.time_since_epoch()).count();
        EVLOG_warning << "EvSimulator: ramp produced non-positive total_ms (" << total_ms
                      << "); snapping to target. start_at_ms=" << start_ms << " end_at_ms=" << end_ms;
        ctx.set_desired_ac_params(r.target_a, r.three_phases);
        ctx.vars.active_ramp.reset();
        return;
    }
    // Clamp the interpolation parameter to [0, 1]. now >= r.end_at is handled
    // above so t < 1 normally holds, but if the ramp was armed with a
    // start_at in the future (clock skew, or armed-ahead) elapsed_ms is
    // negative and an unclamped t drives `current` past start_a in the wrong
    // direction. Clamping keeps the output within [start_a, target_a].
    const float raw_t = static_cast<float>(elapsed_ms) / static_cast<float>(total_ms);
    const float t = std::clamp(raw_t, 0.0f, 1.0f);
    const float current = r.start_a + (r.target_a - r.start_a) * t;
    ctx.set_desired_ac_params(current, r.three_phases);
}

} // namespace module
