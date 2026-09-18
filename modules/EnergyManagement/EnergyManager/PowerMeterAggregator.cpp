// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "PowerMeterAggregator.hpp"

#include <everest/logging.hpp>

namespace module {

std::optional<date::utc_clock::time_point> parse_meter_timestamp(const std::string& timestamp) {
    const auto measured_at = Everest::Date::from_rfc3339(timestamp);

    if (measured_at == date::utc_clock::time_point{}) {
        return std::nullopt;
    }
    return measured_at;
}

bool is_fresh(const std::optional<date::utc_clock::time_point>& measured_at, date::utc_clock::time_point now,
              std::chrono::seconds window) {
    if (not measured_at.has_value()) {
        return false;
    }

    if (window <= std::chrono::seconds(0)) {
        return true;
    }

    const auto age = now - measured_at.value();

    if (age < std::chrono::seconds(0)) {
        // Reading is timestamped in the future; accept it as the freshest we have.
        return true;
    }

    return age < window;
}

namespace {

enum class Freshness {
    Fresh,
    Stale,
    UnparsableTimestamp,
};

/// \brief Classifies a reading for aggregate(), which needs to tell an unusable timestamp
/// from a merely old one so it can warn about the former. The age rule itself is is_fresh()
/// and is not repeated here.
Freshness check_freshness(const types::powermeter::Powermeter& reading, date::utc_clock::time_point now,
                          std::chrono::seconds window) {
    const auto measured_at = parse_meter_timestamp(reading.timestamp);

    if (not measured_at.has_value()) {
        return Freshness::UnparsableTimestamp;
    }

    return is_fresh(measured_at, now, window) ? Freshness::Fresh : Freshness::Stale;
}

/// \brief Sums one optional measurement field across meters.
///
/// A field is only summable while every contributing meter supplies it: a meter that does
/// not measure a phase reports nothing there, not zero, so adding the others would produce
/// a sum that quietly covers fewer meters than the total. Once any contributor omits the
/// field the accumulator yields nullopt for good.
class FieldAccumulator {
public:
    void add(const std::optional<float>& value) {
        if (value.has_value()) {
            sum += value.value();
        } else {
            covered_by_all = false;
        }
    }

    std::optional<float> total() const {
        if (not covered_by_all) {
            return std::nullopt;
        }
        return sum;
    }

private:
    float sum{0.f};
    bool covered_by_all{true};
};

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

    // Summed separately so the sums are only published once a meter has actually
    // contributed: an empty sum must read as "no data", not as zero.
    float total_W = 0.f;
    FieldAccumulator power_L1_W, power_L2_W, power_L3_W;
    FieldAccumulator current_L1_A, current_L2_A, current_L3_A;

    for (const auto& [uuid, reading] : readings) {
        if (not reading.power_W.has_value()) {
            result.stale_meters++;
            continue;
        }

        const auto freshness = check_freshness(reading, now, aggregation_window);

        if (freshness == Freshness::UnparsableTimestamp) {
            // Warn once per meter, not once per optimizer cycle: a permanently broken
            // meter would otherwise produce a warning every second, around the clock.
            if (warned_unparsable.insert(uuid).second) {
                EVLOG_warning << "PowerMeterAggregator: cannot parse power meter timestamp '" << reading.timestamp
                              << "' of meter " << uuid << ", treating its readings as stale until it recovers";
            }
            result.stale_meters++;
            continue;
        }

        // The meter delivers parsable timestamps (again); allow a future warning.
        warned_unparsable.erase(uuid);

        if (freshness == Freshness::Stale) {
            result.stale_meters++;
            continue;
        }

        const auto& power = reading.power_W.value();

        total_W += power.total;
        result.fresh_meters++;

        power_L1_W.add(power.L1);
        power_L2_W.add(power.L2);
        power_L3_W.add(power.L3);

        // A meter publishing power but no current is simply not covered for current. Feed
        // the accumulators an empty Current so that counts the same as a missing phase.
        const auto current = reading.current_A.value_or(types::units::Current{});
        current_L1_A.add(current.L1);
        current_L2_A.add(current.L2);
        current_L3_A.add(current.L3);
    }

    if (result.fresh_meters > 0) {
        types::units::Power power;
        power.total = total_W;
        power.L1 = power_L1_W.total();
        power.L2 = power_L2_W.total();
        power.L3 = power_L3_W.total();
        result.power_W = power;

        result.current_A.L1 = current_L1_A.total();
        result.current_A.L2 = current_L2_A.total();
        result.current_A.L3 = current_L3_A.total();
    }

    return result;
}

void collect_leaf_measurements(const types::energy::EnergyFlowRequest& node, PowerMeterAggregator& aggregator) {
    if (node.node_type == types::energy::NodeType::Evse) {
        if (node.energy_usage_leaves.has_value()) {
            aggregator.update(node.uuid, node.energy_usage_leaves.value());
        } else if (node.energy_usage_root.has_value()) {
            aggregator.update(node.uuid, node.energy_usage_root.value());
        }
        // Do not recurse below an EVSE: its own meter already covers everything downstream
        // of it, so counting descendants as well would double count - the same reason
        // intermediate Generic nodes are skipped.
        return;
    }

    for (const auto& child : node.children) {
        collect_leaf_measurements(child, aggregator);
    }
}

} // namespace module
