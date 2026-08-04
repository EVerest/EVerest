// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "PowerMeterAggregator.hpp"

#include <everest/logging.hpp>

namespace module {

namespace {

/// \brief Decides whether a reading is recent enough to contribute to the sum.
///
/// An unparsable timestamp is treated as stale. Note that Everest::Date::from_rfc3339
/// does not report parse failures: it falls back to a stringstream parse which, on
/// failure, leaves the time point default constructed. So the epoch is the failure
/// signal and must be checked explicitly - a try/catch here would never fire.
/// This check runs before the window check, so a meter with a broken clock is excluded
/// even when the staleness filter is switched off.
///
/// Readings timestamped slightly in the future (clock skew between meter and
/// controller) count as fresh. A window of zero disables the age check.
bool is_fresh(const types::powermeter::Powermeter& reading, date::utc_clock::time_point now,
              std::chrono::seconds window) {
    const auto measured_at = Everest::Date::from_rfc3339(reading.timestamp);

    if (measured_at == date::utc_clock::time_point{}) {
        // Either unparsable, or a meter genuinely reporting 1970 - both unusable.
        EVLOG_warning << "PowerMeterAggregator: cannot parse power meter timestamp '" << reading.timestamp
                      << "', treating reading as stale";
        return false;
    }

    if (window <= std::chrono::seconds(0)) {
        return true;
    }

    const auto age = now - measured_at;

    if (age < std::chrono::seconds(0)) {
        // Reading is timestamped in the future; accept it as the freshest we have.
        return true;
    }

    return age < window;
}

} // namespace

void PowerMeterAggregator::update(const std::string& node_uuid, const types::powermeter::Powermeter& reading) {
    readings[node_uuid] = reading;
}

void PowerMeterAggregator::clear() {
    readings.clear();
}

std::size_t PowerMeterAggregator::size() const {
    return readings.size();
}

PowerMeterAggregator::AggregateResult PowerMeterAggregator::aggregate(date::utc_clock::time_point now) const {
    AggregateResult result;

    // Per phase sums are only reported when every contributing meter supplied them,
    // so the per phase figures always cover the same meters as the total.
    bool all_have_per_phase = true;

    for (const auto& [uuid, reading] : readings) {
        if (not reading.power_W.has_value() or not is_fresh(reading, now, aggregation_window)) {
            result.stale_meters++;
            continue;
        }

        const auto& power = reading.power_W.value();

        result.power_W += power.total;
        result.fresh_meters++;

        if (power.L1.has_value() and power.L2.has_value() and power.L3.has_value()) {
            result.power_L1_W += power.L1.value();
            result.power_L2_W += power.L2.value();
            result.power_L3_W += power.L3.value();
        } else {
            all_have_per_phase = false;
        }
    }

    result.per_phase_available = all_have_per_phase and result.fresh_meters > 0;

    return result;
}

void collect_leaf_measurements(const types::energy::EnergyFlowRequest& node, PowerMeterAggregator& aggregator) {
    if (node.node_type == types::energy::NodeType::Evse) {
        if (node.energy_usage_leaves.has_value()) {
            aggregator.update(node.uuid, node.energy_usage_leaves.value());
        } else if (node.energy_usage_root.has_value()) {
            aggregator.update(node.uuid, node.energy_usage_root.value());
        }
    }

    for (const auto& child : node.children) {
        collect_leaf_measurements(child, aggregator);
    }
}

} // namespace module
