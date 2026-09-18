// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <optional>
#include <vector>

#include "BrokerFastCharging.hpp"
#include "PowerMeterAggregator.hpp"

namespace module {

// ---------------------------------------------------------------- power redistribution inference
//
// Log-only stage of the power redistribution: from what each connector was allotted, what
// it actually draws and what the grid connection has to spare, infer where power could be
// reduced or increased. Nothing here changes an allocation; EnergyManagerImpl calls these
// once per optimizer run and logs the result.

/// \brief Import limit of the grid connection [W], read from the root node's own
/// schedule_import[0]. total_power_W wins; otherwise ac_max_current_A times the declared
/// phase count (3 when not declared) times the nominal voltage.
/// \returns std::nullopt when the root declares no import limit
std::optional<float> get_grid_limit_W(const types::energy::EnergyFlowRequest& root, float nominal_ac_voltage);

/// \brief Import power [W] an enforced limit hands to a connector, with the same precedence
/// as get_grid_limit_W(). \returns std::nullopt when the limit carries neither watt nor ampere.
std::optional<float> get_allocated_power_W(const types::energy::EnforcedLimits& limit, float nominal_ac_voltage);

/// \brief Static import bounds of a connector [W], from its schedule_import[0].limits_to_root.
struct StaticBoundsW {
    /// Smallest purchase that still charges: ac_min_current_A x min phase count x U.
    std::optional<float> min_W;
    /// total_power_W, else ac_max_current_A x max phase count x U.
    std::optional<float> max_W;
};

StaticBoundsW get_static_bounds_W(const types::energy::EnergyFlowRequest& node, float nominal_ac_voltage);

enum class ConnectorClass {
    Unknown,        ///< no previous allocation or no measurement to compare against
    UnderConsuming, ///< draws less than allotted by more than the margin: power can be reduced
    Saturated,      ///< draws what it was allotted and could take more
    AtMaximum,      ///< draws what it was allotted and is at its static maximum already
};

const char* to_string(ConnectorClass c);

/// \brief Result of classify_connector() for one connector.
struct ConnectorInference {
    ConnectorClass connector_class{ConnectorClass::Unknown};
    std::optional<float> allocated_W;
    std::optional<float> measured_W;
    /// By how much the allocation could shrink: down to measured x (1 + margin), but never
    /// below the connector's minimum purchase. 0 unless UnderConsuming.
    float reducible_W{0.f};
    /// True once the condition has held for the configured hold time. Set by the caller,
    /// which owns the timing; classify_connector() leaves it false.
    bool held{false};
};

/// \brief Compares what a connector was allotted with what it draws.
///
/// The deadband is relative: a gap of more than \p margin times the allocation counts as
/// under-consumption. Everything closer is treated as consuming the allocation, which is
/// either Saturated (could take more) or AtMaximum (its static limit is reached, within 1 W).
/// Without both an allocation and a measurement the class is Unknown: no claim is made on
/// missing data.
ConnectorInference classify_connector(std::optional<float> allocated_W, std::optional<float> measured_W,
                                      const StaticBoundsW& bounds, float margin);

/// \brief A connector that could take more power: its current allocation and static maximum.
struct SaturatedConnector {
    float allocated_W;
    std::optional<float> max_W;
};

/// \brief Result of infer_site().
struct SiteInference {
    std::optional<float> grid_limit_W;
    /// Fresh site aggregate. nullopt when no meter is fresh or any meter is stale: a partial
    /// sum undercounts consumption and would fabricate headroom.
    std::optional<float> measured_W;
    /// grid_limit_W - measured_W, when both are known
    std::optional<float> headroom_W;
    int saturated_connectors{0};
    /// Proposed increase [W] summed over the saturated connectors. 0 when the headroom is
    /// within the deadband, no connector can take more, or the gain is 0.
    float increase_W{0.f};
    /// True once the condition has held for the configured hold time (set by the caller).
    bool held{false};
};

/// \brief Proportional increase law for the site.
///
/// Headroom h = G - S must exceed the deadband margin x G. The increase is then
/// gain x (h - deadband), split equally over the saturated connectors and clamped per
/// connector to its static maximum. Being proportional to the remaining headroom the step
/// is large far from the grid limit and vanishes close to it, which is what the review on
/// PR #2630 asked for instead of a fixed ampere step.
SiteInference infer_site(std::optional<float> grid_limit_W, const PowerMeterAggregator::AggregateResult& aggregate,
                         const std::vector<SaturatedConnector>& saturated, float margin, float gain);

// ---------------------------------------------------------------- measurement extraction

/// \brief Extracts the imported power measurement from a node of the energy tree.
/// Prefers the leaves side measurement (what EvseManager reports for an EVSE) and falls
/// back to the root side measurement.
/// The reading's own timestamp is not checked here: staleness handling of aggregated
/// measurements is WP1.b's PowerMeterAggregator's job.
/// \returns measured power in Watt (total, plus per-phase L1/L2/L3 when the meter reports
/// them), or std::nullopt if the node carries no power measurement
std::optional<types::units::Power> get_measured_power_W(const types::energy::EnergyFlowRequest& node);

/// \brief Extracts the per-phase current measurement (L1/L2/L3) from a node of the energy
/// tree. Prefers the leaves side measurement and falls back to the root side, like
/// get_measured_power_W(). Phases the meter does not report stay nullopt (a single-phase
/// meter reports only L1) - they must not be read as zero.
/// Per-phase values are the basis for WP1.b (trade per measured phase) and the asymmetric
/// load handling of WP3.a, whose threshold is defined in ampere per phase.
/// \returns measured current per phase in Ampere; all phases nullopt if no current measurement
types::units::Current get_measured_current_A(const types::energy::EnergyFlowRequest& node);

/// \brief Broker of the PowerRedistribution strategy. In this stage it trades exactly like
/// BrokerFastCharging and additionally observes the live power meter measurement of its
/// connector; redistributing energy based on that observation is future work.
///
/// It never modifies the allocation: trading is delegated unchanged to the base class.
/// Once per optimizer run (EnergyManagerImpl builds a fresh broker for every run) it reads
/// the connector's measurement and logs the actual usage. A connector in an active session
/// that reports no measurement is warned about once per session.
///
/// Operates on a single connector: the measurement is read from this broker's own market node.
class BrokerPowerRedistribution : public BrokerFastCharging {
public:
    BrokerPowerRedistribution(Market& market, BrokerContext& context, EnergyManagerConfig config);

private:
    // Reads and logs the connector's measurement. Called exactly once, from the
    // constructor - tradeImpl() runs once per trading round and would log repeatedly.
    void observe_measurement();
};

} // namespace module
