// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "MeasurementTrackingState.hpp"

#include <algorithm>

#include <everest/logging.hpp>

namespace module {

namespace {
constexpr int ASSUMED_PHASE_COUNT = 3;
}

std::optional<float> get_grid_limit_W(const types::energy::EnergyFlowRequest& root, float nominal_ac_voltage) {
    if (root.schedule_import.empty()) {
        return std::nullopt;
    }

    const auto& limits = root.schedule_import[0].limits_to_root;

    if (limits.total_power_W.has_value()) {
        return limits.total_power_W.value().value;
    }

    if (limits.ac_max_current_A.has_value()) {
        const auto phases =
            limits.ac_max_phase_count.has_value() ? limits.ac_max_phase_count.value().value : ASSUMED_PHASE_COUNT;
        return limits.ac_max_current_A.value().value * static_cast<float>(phases) * nominal_ac_voltage;
    }

    return std::nullopt;
}

float sum_allocated_W(const std::vector<types::energy::EnforcedLimits>& limits, float nominal_ac_voltage) {
    float total_W = 0.f;

    for (const auto& limit : limits) {
        if (limit.limits_root_side.total_power_W.has_value()) {
            total_W += limit.limits_root_side.total_power_W.value().value;
        } else if (limit.limits_root_side.ac_max_current_A.has_value()) {
            const auto phases = limit.limits_root_side.ac_max_phase_count.has_value()
                                    ? limit.limits_root_side.ac_max_phase_count.value().value
                                    : ASSUMED_PHASE_COUNT;
            total_W +=
                limit.limits_root_side.ac_max_current_A.value().value * static_cast<float>(phases) * nominal_ac_voltage;
        }
    }

    return total_W;
}

MeasurementTrackingState advance_tracking_state(const MeasurementTrackingState& current,
                                                const MeasurementTrackingInput& input) {
    MeasurementTrackingState next = current;

    if (input.aggregate.fresh_meters == 0) {
        // No trustworthy measurement this cycle. Hold the offset where it is, drop the
        // hysteresis progress, and make no claim about reducibility.
        next.underutilized_cycles = 0;
        next.power_can_be_reduced = false;
        return next;
    }

    // Can the current allocation be released without curtailing charging?
    next.power_can_be_reduced =
        input.last_allocated_W > 0.f and (input.last_allocated_W - input.aggregate.power_W) > input.boost_threshold_W;

    if (not input.grid_limit_W.has_value() or input.boost_step_A <= 0.f) {
        // Without a declared grid limit there is no reference for spare capacity, and a
        // zero step means boosting is switched off. Neither is an error.
        next.underutilized_cycles = 0;
        return next;
    }

    const float grid_headroom_W = input.grid_limit_W.value() - input.aggregate.power_W;

    if (grid_headroom_W > input.boost_threshold_W) {
        next.underutilized_cycles = current.underutilized_cycles + 1;

        if (next.underutilized_cycles >= input.boost_hysteresis_cycles) {
            next.boost_offset_A = current.boost_offset_A + input.boost_step_A;
            next.underutilized_cycles = 0;
        }
    } else {
        // Headroom is gone: release the boost again, one step per cycle, and require a
        // fresh run of consecutive cycles before boosting again.
        next.underutilized_cycles = 0;
        next.boost_offset_A = std::max(0.f, current.boost_offset_A - input.boost_step_A);
    }

    if (input.max_boost_offset_A.has_value()) {
        next.boost_offset_A = std::min(next.boost_offset_A, input.max_boost_offset_A.value());
    }

    return next;
}

} // namespace module
