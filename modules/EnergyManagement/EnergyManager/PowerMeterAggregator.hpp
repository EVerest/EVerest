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
/// Everest::Date::from_rfc3339 does not throw: a default constructed time point is its only
/// failure signal, which is why the epoch is checked instead of catching an exception. A
/// meter genuinely reporting 1970 is equally unusable, so the two cases collapse.
/// \returns the measurement time, or std::nullopt when the timestamp is unusable
std::optional<date::utc_clock::time_point> parse_meter_timestamp(const std::string& timestamp);

/// \brief The module's one staleness rule for power meter readings.
///
/// A reading is fresh while its own measurement timestamp is younger than \p window. Two
/// cases are deliberately not stale: a timestamp slightly in the future, since minor clock
/// skew between a meter and the controller must not discard data, and any reading at all
/// once the window is zero or negative - though the manifest no longer allows that to be
/// configured, a caller passing a window from elsewhere still gets a defined answer.
///
/// A reading with no usable timestamp has no age to judge and is never fresh.
///
/// This lives here, next to the aggregator that first needed it, because every consumer of
/// a measurement has to apply the same rule: the site aggregate and the per connector
/// snapshot are two views of the same meters, and they must not disagree about which of
/// them are alive.
///
/// \p now must be a real wall clock time; an epoch value would make every reading look
/// like the future and disable the filter.
bool is_fresh(const std::optional<date::utc_clock::time_point>& measured_at, date::utc_clock::time_point now,
              std::chrono::seconds window);

/// \brief Sums the readings of several power meters that report at different times.
///
/// Built, filled and summed within a single optimizer run: it holds no state that outlives
/// one aggregation, so a stale entry cannot survive into the next run.
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
    /// \brief The site wide equivalent of BrokerContext::last_observed_measurement.
    ///
    /// Deliberately shaped like ObservedMeasurement (see Broker.hpp) so a consumer handles
    /// one measurement type whether it reads a single connector or the whole site. The rule
    /// stated there holds here too: a value the meters do not cover is nullopt, never zero.
    struct AggregateResult {
        /// Summed power [W] over all fresh meters. Empty when no meter contributed, so
        /// "no data" never reads as a total of zero (nothing connected vs. nothing
        /// flowing). Its per phase members are nullopt unless *every* contributing meter
        /// reported that phase, so a phase sum never silently omits a meter.
        std::optional<types::units::Power> power_W;
        /// Summed current [A] over the same meters, under the same per field rule. A
        /// single phase meter reports only L1, so L2 and L3 stay nullopt as soon as one
        /// contributing meter does not measure them.
        ///
        /// Only the AC phases are summed. N is left out because neutral currents do not add
        /// up scalar-wise, and DC for the same reason one step further removed: an ampere
        /// on a DC link is only meaningful together with that link's voltage, so 100 A at
        /// 400 V and 100 A at 800 V are not 200 A of anything. A site-wide DC ampere has no
        /// consumer and no defensible meaning, so it is not produced.
        types::units::Current current_A;
        /// Number of meters that contributed to the sums
        int fresh_meters{0};
        /// Number of stored meters excluded because their reading was too old or unusable
        int stale_meters{0};
        /// Meters excluded because their timestamp could not be parsed at all, as opposed
        /// to merely being old. Reported rather than logged here: warning once per meter
        /// instead of once per optimizer cycle is a decision about a meter's history, and
        /// this class only ever sees one instant.
        std::vector<std::string> unparsable_meters;
    };

    /// \param window validity window for a reading, see is_fresh().
    explicit PowerMeterAggregator(std::chrono::seconds window) : aggregation_window(window){};

    /// \brief Stores (or replaces) the last reading of one node.
    void update(const std::string& node_uuid, const types::powermeter::Powermeter& reading);

    /// \brief Number of stored readings, fresh and stale alike.
    std::size_t size() const;

    /// \brief Sums the readings that are fresh relative to \p now, by the is_fresh() rule.
    ///
    /// A timestamp that cannot be parsed counts as stale, and the meter is named in
    /// AggregateResult::unparsable_meters so the caller can warn about it once rather than
    /// on every optimizer cycle.
    ///
    /// Pure: no state of this object and nothing outside it changes, which is what lets an
    /// instance be built, summed and dropped within one optimizer run.
    AggregateResult aggregate(date::utc_clock::time_point now) const;

private:
    std::map<std::string, types::powermeter::Powermeter> readings;
    std::chrono::seconds aggregation_window;
};

/// \brief Feeds the aggregator with the power meter reading of every EVSE node in the tree.
///
/// Only NodeType::Evse nodes contribute: an intermediate node's own meter measures the sum
/// of its children, so including it would double count. For each EVSE the leaves side
/// measurement is preferred (that is what EvseManager reports) with the root side
/// measurement as fallback. EVSE nodes without any measurement are simply not added.
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
/// Prefers the root node's own power meter (energy_usage_root), which is what EnergyNode
/// publishes for a grid connection point. That single meter is the one thing that sees the
/// whole connection, including the building load that no EVSE meter can account for, so on
/// any site with other consumers behind the same fuse it is the only correct answer.
///
/// Only when the root has no meter of its own does this fall back to summing the EVSE
/// meters. That sum is not a site measurement: it is a site measurement minus every load
/// the energy tree does not know about, and it errs by exactly the amount of non-EVSE
/// consumption - always in the direction of claiming headroom that is already spent.
///
/// The "do not double count" rule that keeps intermediate nodes out of the leaf sum is
/// about summing children. It does not apply here, where a single meter replaces the sum
/// rather than joining it.
///
/// Either way exactly one freshness rule applies, because both go through the aggregator.
/// \returns which source was used
SiteMeterSource collect_site_measurement(const types::energy::EnergyFlowRequest& root,
                                         PowerMeterAggregator& aggregator);

} // namespace module
