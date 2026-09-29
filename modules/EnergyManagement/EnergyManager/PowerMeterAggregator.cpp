// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "PowerMeterAggregator.hpp"

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
    // Clock skew within the window is tolerated; further into the future is not, or a frozen
    // meter with a skewed clock would count as fresh indefinitely.
    return age > -window and age < window;
}

namespace {

enum class Freshness {
    Fresh,
    Stale,
    InTheFuture,
    UnparsableTimestamp,
};

// Tells the faults a caller warns about apart from a merely old reading. The age rule
// itself is is_fresh().
Freshness check_freshness(const types::powermeter::Powermeter& reading, date::utc_clock::time_point now,
                          std::chrono::seconds window) {
    const auto measured_at = parse_meter_timestamp(reading.timestamp);

    if (not measured_at.has_value()) {
        return Freshness::UnparsableTimestamp;
    }
    if (is_fresh(measured_at, now, window)) {
        return Freshness::Fresh;
    }
    return measured_at.value() > now ? Freshness::InTheFuture : Freshness::Stale;
}

// Sums one optional field across meters; nullopt once any meter does not report it.
class FieldAccumulator {
public:
    void add(const std::optional<float>& value) {
        if (value.has_value()) {
            m_sum += value.value();
        } else {
            m_covered_by_all = false;
        }
    }

    std::optional<float> total() const {
        if (not m_covered_by_all) {
            return std::nullopt;
        }
        return m_sum;
    }

private:
    float m_sum{0.f};
    bool m_covered_by_all{true};
};

} // namespace

void PowerMeterAggregator::update(const std::string& node_uuid, const types::powermeter::Powermeter& reading) {
    m_readings[node_uuid] = reading;
}

std::size_t PowerMeterAggregator::size() const {
    return m_readings.size();
}

PowerMeterAggregator::AggregateResult PowerMeterAggregator::aggregate(date::utc_clock::time_point now) const {
    AggregateResult result;

    float total_W = 0.f;
    FieldAccumulator power_L1_W, power_L2_W, power_L3_W;
    FieldAccumulator current_L1_A, current_L2_A, current_L3_A;

    for (const auto& [uuid, reading] : m_readings) {
        if (not reading.power_W.has_value()) {
            result.stale_meters++;
            continue;
        }

        const auto freshness = check_freshness(reading, now, m_aggregation_window);

        if (freshness == Freshness::UnparsableTimestamp) {
            result.unparsable_meters.push_back(uuid);
            result.stale_meters++;
            continue;
        }
        if (freshness == Freshness::InTheFuture) {
            result.future_meters.push_back(uuid);
            result.stale_meters++;
            continue;
        }
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

        const auto current = reading.current_A.value_or(types::units::Current{});
        current_L1_A.add(current.L1);
        current_L2_A.add(current.L2);
        current_L3_A.add(current.L3);
    }

    if (result.fresh_meters > 0) {
        types::units::Power power;
        power.total = total_W;
        result.power_W = power;
    }

    // Per-phase sums feed phase decisions, which must not miss a meter's load.
    if (result.fresh_meters > 0 and result.stale_meters == 0) {
        result.power_W->L1 = power_L1_W.total();
        result.power_W->L2 = power_L2_W.total();
        result.power_W->L3 = power_L3_W.total();

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
        // Its own meter covers everything below it.
        return;
    }

    for (const auto& child : node.children) {
        collect_leaf_measurements(child, aggregator);
    }
}

} // namespace module
