// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <chrono>
#include <map>
#include <set>
#include <string>

#include <generated/interfaces/energy/Interface.hpp>
#include <utils/date.hpp>

namespace module {

/// \brief Sums the readings of several power meters that report at different times.
///
/// Power meters in the energy tree publish independently, so at any instant the stored
/// readings have different ages. Summing them all would mix a fresh value with values
/// from several seconds ago and produce a total that never existed. This class keeps the
/// last reading per node and, when asked for a sum, includes only readings whose own
/// measurement timestamp lies within the configured window.
///
/// The reference time is always supplied by the caller (normally the optimizer's
/// start_time) rather than read from the clock, so an aggregate always matches the
/// optimizer run it belongs to and tests are deterministic.
class PowerMeterAggregator {
public:
    struct AggregateResult {
        /// Sum of total power over all fresh meters [W]
        float power_W{0.f};
        /// Per phase sums [W], only meaningful when per_phase_available is true
        float power_L1_W{0.f};
        float power_L2_W{0.f};
        float power_L3_W{0.f};
        /// True only when every contributing meter reported per phase values, so the
        /// per phase sums cover the same set of meters as power_W.
        bool per_phase_available{false};
        /// Number of meters that contributed to the sums
        int fresh_meters{0};
        /// Number of stored meters excluded because their reading was too old or unusable
        int stale_meters{0};
    };

    /// \param window validity window for a reading. A window of zero disables the
    /// staleness filter and always includes the last reading of every meter.
    explicit PowerMeterAggregator(std::chrono::seconds window) : aggregation_window(window){};

    /// \brief Stores (or replaces) the last reading of one node.
    void update(const std::string& node_uuid, const types::powermeter::Powermeter& reading);

    /// \brief Drops all stored readings.
    void clear();

    /// \brief Number of stored readings, fresh and stale alike.
    std::size_t size() const;

    /// \brief Sums the readings that are fresh relative to \p now.
    /// \p now must be a real wall clock time; an epoch value would make every reading
    /// look like the future and disable the filter.
    AggregateResult aggregate(date::utc_clock::time_point now) const;

private:
    std::map<std::string, types::powermeter::Powermeter> readings;
    std::chrono::seconds aggregation_window;
    // Meters whose timestamp failed to parse, so the warning is logged once per meter
    // rather than on every optimizer cycle. An entry is dropped again once the meter
    // delivers a parsable timestamp.
    mutable std::set<std::string> warned_unparsable;
};

/// \brief Feeds the aggregator with the power meter reading of every EVSE node in the tree.
///
/// Only NodeType::Evse nodes contribute: an intermediate node's own meter measures the sum
/// of its children, so including it would double count. For each EVSE the leaves side
/// measurement is preferred (that is what EvseManager reports) with the root side
/// measurement as fallback. EVSE nodes without any measurement are simply not added.
void collect_leaf_measurements(const types::energy::EnergyFlowRequest& node, PowerMeterAggregator& aggregator);

} // namespace module
