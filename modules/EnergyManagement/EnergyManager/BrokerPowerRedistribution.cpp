// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "BrokerPowerRedistribution.hpp"

#include <algorithm>

#include <everest/logging.hpp>

namespace module {

namespace {

constexpr int ASSUMED_PHASE_COUNT = 3;
// An allocation this close to the static maximum counts as at the maximum. Trading happens
// in slices of slice_ampere (0.5 A x 230 V = 115 W by default), so 1 W only absorbs
// floating point noise of the two conversions.
constexpr float AT_MAXIMUM_TOLERANCE_W = 1.f;

// Converts a limit to watt with the precedence used throughout: an explicit watt value
// wins, otherwise the ampere value times the phase count times the nominal voltage.
// Works for both LimitsReq (schedules) and LimitsRes (enforced limits), which name these
// fields identically.
template <typename Limits>
std::optional<float> limits_to_W(const Limits& limits, const std::optional<types::energy::NumberWithSource>& current_A,
                                 const std::optional<types::energy::IntegerWithSource>& phase_count,
                                 float nominal_ac_voltage) {
    if (limits.total_power_W.has_value()) {
        return limits.total_power_W.value().value;
    }
    if (current_A.has_value()) {
        const auto phases = phase_count.has_value() ? phase_count.value().value : ASSUMED_PHASE_COUNT;
        return current_A.value().value * static_cast<float>(phases) * nominal_ac_voltage;
    }
    return std::nullopt;
}

} // namespace

std::optional<float> get_grid_limit_W(const types::energy::EnergyFlowRequest& root, float nominal_ac_voltage) {
    if (root.schedule_import.empty()) {
        return std::nullopt;
    }
    const auto& limits = root.schedule_import[0].limits_to_root;
    return limits_to_W(limits, limits.ac_max_current_A, limits.ac_max_phase_count, nominal_ac_voltage);
}

std::optional<float> get_allocated_power_W(const types::energy::EnforcedLimits& limit, float nominal_ac_voltage) {
    const auto& limits = limit.limits_root_side;
    return limits_to_W(limits, limits.ac_max_current_A, limits.ac_max_phase_count, nominal_ac_voltage);
}

StaticBoundsW get_static_bounds_W(const types::energy::EnergyFlowRequest& node, float nominal_ac_voltage) {
    StaticBoundsW bounds;
    if (node.schedule_import.empty()) {
        return bounds;
    }
    const auto& limits = node.schedule_import[0].limits_to_root;

    bounds.max_W = limits_to_W(limits, limits.ac_max_current_A, limits.ac_max_phase_count, nominal_ac_voltage);

    if (limits.ac_min_current_A.has_value()) {
        // The minimum purchase uses the smallest phase count the connector accepts; a
        // connector that can charge single phase only needs min current on one phase.
        const auto phases = limits.ac_min_phase_count.has_value()   ? limits.ac_min_phase_count.value().value
                            : limits.ac_max_phase_count.has_value() ? limits.ac_max_phase_count.value().value
                                                                    : ASSUMED_PHASE_COUNT;
        bounds.min_W = limits.ac_min_current_A.value().value * static_cast<float>(phases) * nominal_ac_voltage;
    }
    return bounds;
}

const char* to_string(ConnectorClass c) {
    switch (c) {
    case ConnectorClass::UnderConsuming:
        return "UnderConsuming";
    case ConnectorClass::Saturated:
        return "Saturated";
    case ConnectorClass::AtMaximum:
        return "AtMaximum";
    case ConnectorClass::Unknown:
    default:
        return "Unknown";
    }
}

ConnectorInference classify_connector(std::optional<float> allocated_W, std::optional<float> measured_W,
                                      const StaticBoundsW& bounds, float margin) {
    ConnectorInference result;
    result.allocated_W = allocated_W;
    result.measured_W = measured_W;

    if (not allocated_W.has_value() or not measured_W.has_value() or allocated_W.value() <= 0.f) {
        return result;
    }

    const float allocated = allocated_W.value();
    const float measured = std::max(0.f, measured_W.value());
    const float gap = allocated - measured;

    if (gap > margin * allocated) {
        result.connector_class = ConnectorClass::UnderConsuming;
        // Shrink to the measurement plus margin, but never below what the EV needs to keep
        // charging at all: reducing is meant to free unused power, not to starve a session.
        float target = measured * (1.f + margin);
        if (bounds.min_W.has_value()) {
            target = std::max(target, bounds.min_W.value());
        }
        result.reducible_W = std::max(0.f, allocated - target);
        return result;
    }

    if (bounds.max_W.has_value() and allocated + AT_MAXIMUM_TOLERANCE_W >= bounds.max_W.value()) {
        result.connector_class = ConnectorClass::AtMaximum;
    } else {
        result.connector_class = ConnectorClass::Saturated;
    }
    return result;
}

SiteInference infer_site(std::optional<float> grid_limit_W, const PowerMeterAggregator::AggregateResult& aggregate,
                         const std::vector<SaturatedConnector>& saturated, float margin, float gain) {
    SiteInference site;
    site.grid_limit_W = grid_limit_W;
    site.saturated_connectors = static_cast<int>(saturated.size());

    // Same trust rule as the aggregator's no-data contract: with any meter stale the sum
    // undercounts consumption and overstates headroom, so no claim is made at all.
    if (aggregate.power_W.has_value() and aggregate.fresh_meters > 0 and aggregate.stale_meters == 0) {
        site.measured_W = aggregate.power_W.value().total;
    }

    if (not grid_limit_W.has_value() or not site.measured_W.has_value()) {
        return site;
    }

    const float headroom = grid_limit_W.value() - site.measured_W.value();
    site.headroom_W = headroom;

    const float deadband = margin * grid_limit_W.value();
    if (headroom <= deadband or saturated.empty() or gain <= 0.f) {
        return site;
    }

    const float share = gain * (headroom - deadband) / static_cast<float>(saturated.size());
    for (const auto& connector : saturated) {
        const float room =
            connector.max_W.has_value() ? std::max(0.f, connector.max_W.value() - connector.allocated_W) : share;
        site.increase_W += std::min(share, room);
    }
    return site;
}

std::optional<types::units::Power> get_measured_power_W(const types::energy::EnergyFlowRequest& node) {
    if (node.energy_usage_leaves.has_value() and node.energy_usage_leaves.value().power_W.has_value()) {
        return node.energy_usage_leaves.value().power_W.value();
    }

    if (node.energy_usage_root.has_value() and node.energy_usage_root.value().power_W.has_value()) {
        return node.energy_usage_root.value().power_W.value();
    }

    return std::nullopt;
}

types::units::Current get_measured_current_A(const types::energy::EnergyFlowRequest& node) {
    if (node.energy_usage_leaves.has_value() and node.energy_usage_leaves.value().current_A.has_value()) {
        return node.energy_usage_leaves.value().current_A.value();
    }

    if (node.energy_usage_root.has_value() and node.energy_usage_root.value().current_A.has_value()) {
        return node.energy_usage_root.value().current_A.value();
    }

    return {};
}

BrokerPowerRedistribution::BrokerPowerRedistribution(Market& market, BrokerContext& context,
                                                     EnergyManagerConfig config) :
    BrokerFastCharging(market, context, config) {
    observe_measurement();
}

void BrokerPowerRedistribution::observe_measurement() {
    const auto& request = local_market.energy_flow_request;

    // Only sessions have a consumption worth observing. The optimizer runs continuously
    // and constructs a broker for every EVSE on every run; without this guard an idle
    // meterless connector would trip the missing-measurement warning.
    if (request.evse_state.has_value() and (request.evse_state.value() == types::energy::EvseState::Unplugged or
                                            request.evse_state.value() == types::energy::EvseState::Finished)) {
        return;
    }

    const auto measured_W = get_measured_power_W(request);
    context.last_observed_measurement.power_W = measured_W;
    context.last_observed_measurement.current_A = get_measured_current_A(request);

    if (not measured_W.has_value()) {
        // Warn once per session, not once per optimizer run.
        if (not context.tracking_warned_no_measurement) {
            context.tracking_warned_no_measurement = true;
            EVLOG_warning << request.uuid << ": power meter tracking enabled but no measurement available";
        }
        return;
    }

    EVLOG_debug << request.uuid << ": measured power " << measured_W.value().total << " W";
}

} // namespace module
