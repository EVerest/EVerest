// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "BrokerMeasurementTracking.hpp"

#include <everest/logging.hpp>

namespace module {

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

BrokerMeasurementTracking::BrokerMeasurementTracking(Market& market, BrokerContext& context,
                                                     EnergyManagerConfig config) :
    BrokerFastCharging(market, context, config) {
    observe_measurement();
}

void BrokerMeasurementTracking::observe_measurement() {
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
