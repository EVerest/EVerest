// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "RampInterpolator.hpp"

#include "FsmContext.hpp"

namespace module {

void RampInterpolator::step(FsmContext& ctx, std::chrono::steady_clock::time_point now) {
    if (!ctx.vars.active_ramp.has_value()) {
        return;
    }
    auto& r = *ctx.vars.active_ramp;
    if (now >= r.end_at) {
        ctx.bsp_apply_ac_params(r.target_a, r.three_phases);
        ctx.vars.active_ramp.reset();
        return;
    }
    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - r.start_at).count();
    const auto total_ms = std::chrono::duration_cast<std::chrono::milliseconds>(r.end_at - r.start_at).count();
    if (total_ms <= 0) {
        // Defensive: degenerate ramp (end_at <= start_at). Snap to target.
        ctx.bsp_apply_ac_params(r.target_a, r.three_phases);
        ctx.vars.active_ramp.reset();
        return;
    }
    const float t = static_cast<float>(elapsed_ms) / static_cast<float>(total_ms);
    const float current = r.start_a + (r.target_a - r.start_a) * t;
    ctx.bsp_apply_ac_params(current, r.three_phases);
}

} // namespace module
