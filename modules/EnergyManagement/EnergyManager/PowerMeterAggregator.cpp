// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "PowerMeterAggregator.hpp"

#include <everest/logging.hpp>

namespace module {

namespace {

enum class Freshness {
    Fresh,
    Stale,
    UnparsableTimestamp,
};

/// \brief Decides whether a reading is recent enough to contribute to the sum.
///
/// An unparsable timestamp is reported separately so the caller can warn about it. The
/// epoch check runs before the window check, so a meter with a broken clock is excluded
/// even when the staleness filter is switched off.
///
/// Readings timestamped slightly in the future (clock skew between meter and
/// controller) count as fresh. A window of zero disables the age check.
Freshness check_freshness(const types::powermeter::Powermeter& reading, date::utc_clock::time_point now,
                          std::chrono::seconds window) {
    const auto measured_at = Everest::Date::from_rfc3339(reading.timestamp);

    if (measured_at == date::utc_clock::time_point{}) {
        // Either unparsable, or a meter genuinely reporting 1970 - both unusable. Note that
        // from_rfc3339 never throws: a default constructed time point is its only failure
        // signal, which is why the epoch is checked instead of catching an exception.
        return Freshness::UnparsableTimestamp;
    }

    if (window <= std::chrono::seconds(0)) {
        return Freshness::Fresh;
    }

    const auto age = now - measured_at;

    if (age < std::chrono::seconds(0)) {
        // Reading is timestamped in the future; accept it as the freshest we have.
        return Freshness::Fresh;
    }

    return age < window ? Freshness::Fresh : Freshness::Stale;
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
    FieldAccumulator current_DC_A, current_L1_A, current_L2_A, current_L3_A;

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
        current_DC_A.add(current.DC);
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

        result.current_A.DC = current_DC_A.total();
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
