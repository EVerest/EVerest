// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "BrokerPowerRedistribution.hpp"

#include <everest/logging.hpp>

namespace module {

namespace {

// Selects the single power meter reading an observation is taken from. One selection for
// power, current and timestamp: resolving them through independent lookups is how a value
// ends up carrying another meter's age, or how a per-phase current ends up next to a total
// that came from a different meter.
//
// Power decides the choice, and only when no side reports power does current decide it.
// Within each of those two passes the leaves side (what EvseManager reports for an EVSE)
// wins over the root side. Selecting on "carries anything usable" instead would let a
// leaves reading with current but no power hide a root reading that does have power - the
// measurement every consumer of this actually compares an allocation against.
const types::powermeter::Powermeter* find_reading(const types::energy::EnergyFlowRequest& node) {
    const auto pick =
        [&node](bool (*carries)(const types::powermeter::Powermeter&)) -> const types::powermeter::Powermeter* {
        if (node.energy_usage_leaves.has_value() and carries(node.energy_usage_leaves.value())) {
            return &node.energy_usage_leaves.value();
        }
        if (node.energy_usage_root.has_value() and carries(node.energy_usage_root.value())) {
            return &node.energy_usage_root.value();
        }
        return nullptr;
    };

    if (const auto* reading = pick([](const types::powermeter::Powermeter& p) { return p.power_W.has_value(); })) {
        return reading;
    }

    return pick([](const types::powermeter::Powermeter& p) { return p.current_A.has_value(); });
}

std::optional<date::utc_clock::time_point> measured_time_of(const types::powermeter::Powermeter& reading) {
    const auto measured_at = Everest::Date::from_rfc3339(reading.timestamp);
    if (measured_at == date::utc_clock::time_point{}) {
        return std::nullopt;
    }
    return measured_at;
}

} // namespace

ObservedMeasurement read_measurement(const types::energy::EnergyFlowRequest& node) {
    const auto* reading = find_reading(node);
    if (reading == nullptr) {
        return {};
    }

    ObservedMeasurement measurement;
    measurement.power_W = reading->power_W;
    measurement.current_A = reading->current_A.value_or(types::units::Current{});
    measurement.measured_at = measured_time_of(*reading);
    return measurement;
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

    context.last_observed_measurement = read_measurement(request);

    if (not context.last_observed_measurement.power_W.has_value()) {
        // Warn once per session, not once per optimizer run.
        if (not context.tracking_warned_no_measurement) {
            context.tracking_warned_no_measurement = true;
            EVLOG_warning << request.uuid << ": power meter tracking enabled but no measurement available";
        }
        return;
    }

    EVLOG_debug << request.uuid << ": measured power " << context.last_observed_measurement.power_W.value().total
                << " W";
}

} // namespace module
