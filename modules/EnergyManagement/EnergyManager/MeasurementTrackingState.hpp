// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <optional>
#include <vector>

#include <generated/interfaces/energy/Interface.hpp>

#include "PowerMeterAggregator.hpp"

namespace module {

/// \brief State of the measurement based boosting that survives between optimizer runs.
struct MeasurementTrackingState {
    /// Consecutive cycles in which the grid connection had spare capacity
    int underutilized_cycles{0};
    /// Amount by which the tracking limit of every connector is currently widened [A per phase]
    float boost_offset_A{0.f};
    /// True when the enforced allocation exceeds the measured consumption by more than
    /// the configured threshold, i.e. allocation could be released without curtailing charging
    bool power_can_be_reduced{false};
};

/// \brief Everything the state transition needs for one optimizer cycle.
struct MeasurementTrackingInput {
    /// Windowed aggregate of the leaf power meters
    PowerMeterAggregator::AggregateResult aggregate;
    /// Import limit of the grid connection, if it declares one
    std::optional<float> grid_limit_W;
    /// Total power handed out in the previous cycle
    float last_allocated_W{0.f};
    float boost_threshold_W{500.f};
    float boost_step_A{2.f};
    int boost_hysteresis_cycles{3};
    /// Upper bound for boost_offset_A. Boosting beyond the whole grid limit is meaningless,
    /// and an unbounded offset would take many cycles to unwind once headroom disappears.
    std::optional<float> max_boost_offset_A;
};

/// \brief Reads the import limit the root node declares for itself.
///
/// This is the limit of the grid connection, against which the aggregated leaf
/// measurement is compared to find spare capacity. An explicit total_power_W wins over
/// the ampere limit; otherwise the ampere limit is converted using the declared phase
/// count (assuming 3 when the root does not say).
/// \returns the grid limit in Watt, or std::nullopt when the root declares no import limit
std::optional<float> get_grid_limit_W(const types::energy::EnergyFlowRequest& root, float nominal_ac_voltage);

/// \brief Total power currently handed out to all connectors.
/// Uses total_power_W where the enforced limit carries one, otherwise converts the
/// ampere limit with its phase count.
float sum_allocated_W(const std::vector<types::energy::EnforcedLimits>& limits, float nominal_ac_voltage);

/// \brief Computes the next boosting state for one optimizer cycle.
///
/// Boosting is driven by spare capacity at the *grid connection*
/// (grid_limit_W - measured), not by the difference between allocation and measurement.
/// Once measurement tracking is active the allocation always hugs the measurement, so that
/// difference settles at roughly connector_count * margin: it is unreachable on a small
/// site and self-reinforcing on a large one, because boosting widens the very gap that
/// triggered it. The grid limit is bounded by real capacity and cannot ratchet.
///
/// power_can_be_reduced is the separate question of whether the current allocation exceeds
/// actual consumption, which is what an external entity wants to know.
///
/// With any meter stale - not just all of them - the offset is held unchanged and no
/// claim is made about reducibility: a partially stale aggregate undercounts consumption,
/// which overstates headroom, and acting on it is worse than not acting.
MeasurementTrackingState advance_tracking_state(const MeasurementTrackingState& current,
                                                const MeasurementTrackingInput& input);

} // namespace module
