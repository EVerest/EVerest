// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "BrokerPowerRedistribution.hpp"

#include <everest/logging.hpp>

namespace module {

namespace {

// Selects the power meter reading a node's power measurement comes from: the leaves side
// (what EvseManager reports for an EVSE) when it carries a power value, the root side
// otherwise. Returning the whole reading rather than the value keeps the timestamp and the
// power it belongs to inseparable - reading them through two independent lookups is how a
// value ends up carrying somebody else's age.
const types::powermeter::Powermeter* find_power_reading(const types::energy::EnergyFlowRequest& node) {
    if (node.energy_usage_leaves.has_value() and node.energy_usage_leaves.value().power_W.has_value()) {
        return &node.energy_usage_leaves.value();
    }

    if (node.energy_usage_root.has_value() and node.energy_usage_root.value().power_W.has_value()) {
        return &node.energy_usage_root.value();
    }

    return nullptr;
}

} // namespace

std::optional<types::units::Power> get_measured_power_W(const types::energy::EnergyFlowRequest& node) {
    const auto* reading = find_power_reading(node);
    if (reading == nullptr) {
        return std::nullopt;
    }
    return reading->power_W.value();
}

std::optional<date::utc_clock::time_point> get_measured_time(const types::energy::EnergyFlowRequest& node) {
    const auto* reading = find_power_reading(node);
    if (reading == nullptr) {
        return std::nullopt;
    }

    const auto measured_at = Everest::Date::from_rfc3339(reading->timestamp);
    if (measured_at == date::utc_clock::time_point{}) {
        return std::nullopt;
    }
    return measured_at;
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
}

void BrokerPowerRedistribution::observe() {
    const auto& request = local_market.energy_flow_request;

    // Only sessions have a consumption worth observing. The optimizer runs continuously
    // and constructs a broker for every EVSE on every run; without this guard an idle
    // meterless connector would trip the missing-measurement warning.
    if (not in_session(request)) {
        return;
    }

    const auto measured_W = get_measured_power_W(request);
    context.last_observed_measurement.power_W = measured_W;
    context.last_observed_measurement.current_A = get_measured_current_A(request);
    context.last_observed_measurement.measured_at = get_measured_time(request);

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
