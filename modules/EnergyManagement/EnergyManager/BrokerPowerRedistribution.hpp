// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <chrono>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "BrokerFastCharging.hpp"
#include "PowerMeterAggregator.hpp"

namespace module {

// ---------------------------------------------------------------- power redistribution inference
//
// Pure functions over one optimizer run that infer, from allocation, measurement and grid
// headroom, where power could be reduced or increased. Only the increase side acts: each
// connector's share of SiteInference::increase_W_by_connector is applied by its broker on
// the next run. Reductions already happen through the measurement based cap.

/// \brief Import limit of the grid connection [W] at the slot in force: the lower of
/// total_power_W and ac_max_current_A x declared phase count x nominal voltage.
///
/// Read from the root Market's offer, which is already resampled, the minimum of both sides
/// and corrected for efficiency, not from the raw request.
/// \returns std::nullopt when the root has no import schedule at all
std::optional<float> get_grid_limit_W(const Market& root, float nominal_ac_voltage);

/// \brief Import power [W] an enforced limit hands to a connector, with the same precedence
/// as get_grid_limit_W(). \returns std::nullopt when the limit carries neither watt nor ampere.
std::optional<float> get_allocated_power_W(const types::energy::EnforcedLimits& limit, float nominal_ac_voltage);

/// \brief The margin [W] the measurement based cap put on top of the connector's own
/// measurement to arrive at \p limit, i.e. redistribution_margin_A on the phases that
/// limit declares. 0 for a limit the cap is not expressed against (a watt-only DC node),
/// where no such gap exists.
float get_margin_power_W(const types::energy::EnforcedLimits& limit, float margin_A, float nominal_ac_voltage);

/// \brief Import bounds of a connector [W] at the slot in force, from its own Market offer
/// for the same reasons as get_grid_limit_W().
struct StaticBoundsW {
    /// Smallest purchase that still charges: ac_min_current_A x min phase count x U.
    std::optional<float> min_W;
    /// The lower of total_power_W and ac_max_current_A x max phase count x U.
    std::optional<float> max_W;
};

StaticBoundsW get_static_bounds_W(const Market& connector, float nominal_ac_voltage);

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
    /// True once the condition has held for the configured hold time. Set by the caller
    /// from its HoldLatch, which owns the timing; classify_connector() leaves it false.
    bool held{false};
};

/// \brief Compares what a connector was allotted with what it draws.
///
/// The deadband is relative: a gap of more than \p margin times the allocation counts as
/// under-consumption. It is floored at \p broker_margin_W, the margin the cap itself added
/// on top of this connector's measurement: a connector that follows its cap exactly is
/// allotted its measurement plus that margin, so a gap of no more than the margin is the
/// cap's own doing and says nothing about the EV. Without the floor no connector drawing
/// less than redistribution_margin_A / power_redistribution_connector_margin could ever be
/// saturated, and the site could never hand it anything.
/// Everything closer is treated as consuming the allocation, which is
/// either Saturated (could take more) or AtMaximum (its static limit is reached, within 1 W).
/// Without both an allocation and a measurement the class is Unknown: no claim is made on
/// missing data. A negative measurement is Unknown too: negative is export, the inference
/// looks only at schedule_import, and a discharging connector consuming none of its import
/// allocation is not the same thing as one that could give the whole allocation back.
ConnectorInference classify_connector(std::optional<float> allocated_W, std::optional<float> measured_W,
                                      const StaticBoundsW& bounds, float margin, float broker_margin_W);

/// \brief A connector that could take more power: its current allocation and static maximum.
///
/// Both are plain floats. A connector whose maximum is unknown cannot be given power
/// safely - there is nothing to clamp the increase against - so it is not a candidate at
/// all rather than a candidate with a missing bound that every consumer has to decide what
/// to do about.
struct SaturatedConnector {
    /// The connector the share computed for it has to be handed back to.
    std::string uuid;
    float allocated_W;
    float max_W;
};

/// \brief Pairs a Saturated classification with its bounds, when both are known.
///
/// \returns std::nullopt when the allocation or the static maximum is missing. The caller
/// then leaves the connector out of infer_site()'s candidates entirely: giving it a share
/// would be handing out power with nothing to clamp it against, and counting it among the
/// candidates would shrink everyone else's share on behalf of a connector that cannot use
/// it.
std::optional<SaturatedConnector> to_saturated_connector(const std::string& uuid, const ConnectorInference& connector,
                                                         const StaticBoundsW& bounds);

/// \brief Result of infer_site().
struct SiteInference {
    std::optional<float> grid_limit_W;
    /// Fresh site aggregate. nullopt when no meter is fresh or any meter is stale: a partial
    /// sum undercounts consumption and would fabricate headroom.
    std::optional<float> measured_W;
    /// grid_limit_W - measured_W, when both are known
    std::optional<float> headroom_W;
    int saturated_connectors{0};
    /// Which meter measured_W came from. A leaf sum sees only the EVSEs, so a consumer (and
    /// the log line) can tell how much of the site the figure actually covers.
    SiteMeterSource meter_source{SiteMeterSource::None};
    /// Proposed increase [W] summed over the saturated connectors. 0 when the headroom is
    /// within the deadband, no connector can take more, or the gain is 0.
    float increase_W{0.f};
    /// The same increase per connector, which is the form a broker can act on: increase_W
    /// is a site total and says nothing about who may draw it. Only connectors granted more
    /// than 0 W appear.
    std::map<std::string, float> increase_W_by_connector;
    /// True once the condition has held for the configured hold time (set by the caller
    /// from its HoldLatch).
    bool held{false};
};

/// \brief Proportional increase law for the site.
///
/// Headroom h = G - S must exceed the deadband margin x G. The increase is then
/// gain x (h - deadband), split equally over the saturated connectors and clamped per
/// connector to its static maximum. Being proportional to the remaining headroom the step
/// is large far from the grid limit and vanishes close to it, rather than being a fixed
/// ampere step that would approach the limit just as fast however close it already is.
SiteInference infer_site(std::optional<float> grid_limit_W, const PowerMeterAggregator::AggregateResult& aggregate,
                         const std::vector<SaturatedConnector>& saturated, float margin, float gain);

// ---------------------------------------------------------------- measurement extraction

/// \brief Reads the power meter measurement of one node of the energy tree.
///
/// All fields come from a single reading, so a value never carries another meter's
/// timestamp. The reading that reports power wins, leaves side before root side; current
/// decides only when neither reports power. Phases a meter does not report stay
/// std::nullopt.
///
/// A reading without a parsable timestamp gets no measured_at: EnergyNode and EvseManager
/// republish the last reading on every request, so only its own timestamp tells a frozen
/// meter from a steady one. Parsed by parse_meter_timestamp(), like the aggregator.
///
/// \returns the observed measurement, all fields std::nullopt if the node carries none
ObservedMeasurement read_measurement(const types::energy::EnergyFlowRequest& node);

/// \brief Current the connector draws, per phase. Precedence: measured per-phase current,
/// then per-phase power divided by \p nominal_ac_voltage, then total power spread over
/// \p active_phases.
///
/// \param active_phases only used for the total-power fallback. Pass 1 when unknown: all
/// power on one phase gives the highest per-phase current, so the least restrictive limit.
///
/// \returns all phases std::nullopt when no current can be derived
PhaseCurrents measured_phase_currents(const ObservedMeasurement& measurement, float nominal_ac_voltage,
                                      int active_phases);

/// \brief True while \p measurement has a value and is_fresh() accepts its timestamp.
///
/// \param max_age zero accepts any age
bool measurement_can_limit(const ObservedMeasurement& measurement, date::utc_clock::time_point now,
                           std::chrono::seconds max_age);

/// \brief The single ac_max_current_A applied to every phase: the highest known phase, so
/// the phase that draws most is not starved. std::nullopt when no phase is known.
std::optional<float> to_scalar_cap(const PhaseCurrents& cap);

/// \brief Broker of the PowerRedistribution strategy: trades like BrokerFastCharging, but
/// caps a charging connector at its measured current plus a margin, freeing the unused
/// allocation for the other connectors on the same fuse.
///
/// The cap only lowers what FastCharging would allocate, applies only to the slot covering
/// now, and never goes below the connector's minimum current. Reductions wait out the
/// configured hold; increases apply immediately. On top of measured plus margin the cap
/// carries BrokerContext::distributed_power_W, this connector's share of the site headroom
/// granted by the previous run's inference. Without a fresh measurement the connector is
/// capped at its minimum current plus the margin. Nodes without an AC current limit (DC)
/// trade like FastCharging.
class BrokerPowerRedistribution : public BrokerFastCharging {
public:
    BrokerPowerRedistribution(Market& market, BrokerContext& context, EnergyManagerConfig config);

    /// \brief Reads the connector's measurement into the broker context and decides the cap
    /// for this run.
    void observe() override;

    /// \brief Narrows the offer to the cap decided by observe(), then trades like
    /// BrokerFastCharging.
    void tradeImpl() override;

private:
    void decide_cap(const types::energy::EnergyFlowRequest& request);
    void limit_offer_to_cap();
    float sold_current_A(int slot) const;

    // A broker lives for one run; state that must outlive it is in BrokerContext.
    std::optional<PhaseCurrents> m_run_cap_A;
    std::string m_run_cap_source;
};

} // namespace module
