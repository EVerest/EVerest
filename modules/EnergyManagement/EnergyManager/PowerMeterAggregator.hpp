// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <chrono>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <generated/interfaces/energy/Interface.hpp>
#include <utils/date.hpp>

namespace module {

/// \brief Parses a power meter reading's own measurement timestamp.
///
/// Everest::Date::from_rfc3339 signals failure with the epoch, so a meter reporting 1970 is
/// equally unusable.
/// \returns the measurement time, or std::nullopt when the timestamp is unusable
std::optional<date::utc_clock::time_point> parse_meter_timestamp(const std::string& timestamp);

/// \brief The module's one staleness rule for power meter readings, shared by the site
/// aggregate and the per connector limit.
///
/// A reading is fresh while its own timestamp is less than \p window away from \p now, in
/// either direction: clock skew within the window is tolerated. A reading without a usable
/// timestamp is never fresh; a \p window of zero or less accepts every other reading.
///
/// \p now must be a real wall clock time.
bool is_fresh(const std::optional<date::utc_clock::time_point>& measured_at, date::utc_clock::time_point now,
              std::chrono::seconds window);

/// \brief Sums the readings of several power meters that report at different times,
/// including only readings that are fresh by is_fresh().
///
/// Built, filled and summed within a single optimizer run, with the run's start_time as
/// reference, so nothing stale survives into the next run.
class PowerMeterAggregator {
public:
    /// \brief The site wide equivalent of BrokerContext::last_observed_measurement. A value
    /// the meters do not cover is nullopt, never zero.
    struct AggregateResult {
        /// Summed power [W] over all fresh meters; empty when no meter contributed. The per
        /// phase members are set only while every stored meter is fresh and reports that
        /// phase.
        std::optional<types::units::Power> power_W;
        /// Summed AC phase current [A], under the same rule as the per phase power. N and DC
        /// currents do not add up across meters and are not summed.
        types::units::Current current_A;
        /// Number of meters that contributed to the sums
        int fresh_meters{0};
        /// Number of stored meters excluded, for any reason
        int stale_meters{0};
        /// Meters excluded because their timestamp could not be parsed, for the caller to
        /// warn about once
        std::vector<std::string> unparsable_meters;
        /// Meters excluded because their timestamp lies more than the window in the future:
        /// a clock or time zone error, for the caller to warn about once
        std::vector<std::string> future_meters;
    };

    /// \param window validity window for a reading, see is_fresh().
    explicit PowerMeterAggregator(std::chrono::seconds window) : m_aggregation_window(window){};

    /// \brief Stores (or replaces) the last reading of one node.
    void update(const std::string& node_uuid, const types::powermeter::Powermeter& reading);

    /// \brief Number of stored readings, fresh and stale alike.
    std::size_t size() const;

    /// \brief Sums the readings that are fresh relative to \p now.
    AggregateResult aggregate(date::utc_clock::time_point now) const;

private:
    std::map<std::string, types::powermeter::Powermeter> m_readings;
    std::chrono::seconds m_aggregation_window;
};

/// \brief Feeds the aggregator with the power meter reading of every EVSE node in the tree,
/// leaves side before root side. Intermediate nodes are skipped: their meters measure the
/// sum of their children.
void collect_leaf_measurements(const types::energy::EnergyFlowRequest& node, PowerMeterAggregator& aggregator);

/// \brief Where a site measurement came from.
enum class SiteMeterSource {
    None,      ///< nothing measures the site
    RootMeter, ///< the grid connection's own meter
    LeafSum,   ///< the sum of the EVSE meters, which sees only the EVSEs
};

const char* to_string(SiteMeterSource source);

/// \brief Feeds the aggregator with the measurement that describes the whole site.
///
/// Prefers the root node's own power meter (energy_usage_root), the only one that sees
/// non-EVSE load behind the same fuse. Without it the EVSE meters are summed, which misses
/// that load and so overstates the headroom.
/// \returns which source was used
SiteMeterSource collect_site_measurement(const types::energy::EnergyFlowRequest& root,
                                         PowerMeterAggregator& aggregator);

} // namespace module
