// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <chrono>
#include <optional>
#include <string>
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

/// \brief Import limit of the grid connection [W], read from the root Market's offer at the
/// slot in force. total_power_W wins; otherwise ac_max_current_A times the declared phase
/// count times the nominal voltage.
///
/// The offer, not the raw request: Market::get_max_available_energy() has already resampled
/// the schedule onto the optimizer's timestamp grid, taken the minimum of the leaves side
/// and root side limits and divided by the conversion efficiency. Reading
/// schedule_import[0].limits_to_root instead skips all three, and each one skipped
/// overstates the limit - on a multi-slot external schedule, or a limit expressed only on
/// the leaves side, by whatever the two happen to differ by.
/// \returns std::nullopt when the root has no import schedule at all
std::optional<float> get_grid_limit_W(const Market& root, float nominal_ac_voltage);

/// \brief Import power [W] an enforced limit hands to a connector, with the same precedence
/// as get_grid_limit_W(). \returns std::nullopt when the limit carries neither watt nor ampere.
std::optional<float> get_allocated_power_W(const types::energy::EnforcedLimits& limit, float nominal_ac_voltage);

/// \brief Import bounds of a connector [W] at the slot in force, from its own Market offer
/// for the same reasons as get_grid_limit_W().
struct StaticBoundsW {
    /// Smallest purchase that still charges: ac_min_current_A x min phase count x U.
    std::optional<float> min_W;
    /// total_power_W, else ac_max_current_A x max phase count x U.
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
/// missing data. A negative measurement is Unknown too: negative is export, the inference
/// looks only at schedule_import, and a discharging connector consuming none of its import
/// allocation is not the same thing as one that could give the whole allocation back.
ConnectorInference classify_connector(std::optional<float> allocated_W, std::optional<float> measured_W,
                                      const StaticBoundsW& bounds, float margin);

/// \brief A connector that could take more power: its current allocation and static maximum.
struct SaturatedConnector {
    float allocated_W;
    std::optional<float> max_W;
};

/// \brief Pairs a Saturated classification with its bounds.
///
/// Only meaningful for ConnectorClass::Saturated, which classify_connector() only returns
/// once it has an allocation - so the caller does not have to dereference allocated_W on
/// the strength of an invariant established in another function.
SaturatedConnector to_saturated_connector(const ConnectorInference& connector, const StaticBoundsW& bounds);

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
    /// True once the condition has held for the configured hold time (set by the caller).
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
/// Exactly one reading is selected and every field of the result comes from it. Selecting
/// per field instead would let the per-phase current of one meter sit next to the total
/// power and timestamp of another, and the timestamp is only worth carrying while it
/// belongs to the values beside it.
///
/// Power decides which reading that is - the leaves side (what EvseManager reports for an
/// EVSE) before the root side - and current decides only when neither side reports power.
/// A leaves reading that carries current alone must not hide a root reading that carries
/// the power, which is the value an allocation is actually compared against.
///
/// The measured power is in Watt (total, plus per-phase L1/L2/L3 when the meter reports
/// them) and std::nullopt when the selected reading has no power value. Per-phase current
/// is in Ampere; phases the meter does not report stay std::nullopt (a single-phase meter
/// reports only L1) and must not be read as zero. Per-phase values are what per-phase
/// trading and asymmetric load limits are expressed in, the latter as a threshold in
/// ampere per phase.
///
/// The measurement time is the reading's own, not the time of the run. A reading without a
/// usable timestamp has no age a consumer could check, so it is reported as absent rather
/// than as "now": EnergyNode and EvseManager republish the last reading they received on
/// every request, which makes a meter that stopped updating indistinguishable from one
/// holding steady unless its own timestamp is carried along. Parsing follows
/// parse_meter_timestamp(), the same rule the aggregator applies, so the two measurement
/// paths cannot disagree about which readings have a usable age.
///
/// \returns the observed measurement, all fields std::nullopt if the node carries no
/// measurement at all
ObservedMeasurement read_measurement(const types::energy::EnergyFlowRequest& node);

/// \brief Current the connector actually draws, per phase. Precedence: measured per-phase
/// current, then per-phase power divided by \p nominal_ac_voltage, then total power spread
/// over \p active_phases.
///
/// \param active_phases number of phases the connector currently uses, only consulted for
/// the total-power fallback. Pass 1 when the EVSE does not report it: unlike
/// BrokerFastCharging, which assumes 3 when the count is unknown, the safe assumption for
/// a limit derived from a total is 1 - it puts all the power on one phase, which yields
/// the highest per-phase current and therefore the least restrictive limit. Assuming 3
/// would cut a single-phase EV to a third of what it draws.
///
/// \returns the per-phase current, all phases std::nullopt when the measurement carries
/// nothing a current could be derived from (or \p nominal_ac_voltage is not positive)
PhaseCurrents measured_phase_currents(const ObservedMeasurement& measurement, float nominal_ac_voltage,
                                      int active_phases);

/// \brief True while \p measurement can carry a limit: it has a value, and it is fresh.
///
/// The age is judged by is_fresh(), the module's one staleness rule, so the per connector
/// limit and the site aggregate cannot disagree about which meters are alive - including at
/// the boundary, where a reading exactly \p max_age old is stale for both. EnergyNode and
/// EvseManager republish the last reading they received on every request, so without that
/// check a meter that stopped publishing would pin the allocation at whatever it last
/// reported.
///
/// What this adds on top of freshness is the value check: a reading that is fresh but
/// carries neither power nor current has nothing a limit could be derived from.
///
/// \param max_age zero accepts any age
bool measurement_can_limit(const ObservedMeasurement& measurement, date::utc_clock::time_point now,
                           std::chrono::seconds max_age);

/// \brief The single ac_max_current_A the energy interface can express today: the highest
/// of the known phases, std::nullopt when no phase is known. Highest, not lowest - the
/// value is applied to every phase, so the lowest would starve the phase that legitimately
/// draws most.
///
/// This is the seam a per-phase split of the broker removes: everything upstream of it
/// (measurement, margin, cap state) is already per phase, but
/// types::energy::LimitsReq::ac_max_current_A is one number applied to all three phases,
/// so trading per phase first needs a per-phase limit in types/energy.yaml.
std::optional<float> to_scalar_cap(const PhaseCurrents& cap);

/// \brief Broker of the PowerRedistribution strategy: trades with the BrokerFastCharging
/// algorithm, but limits the connector to its measured current plus a configured margin,
/// so an under-consuming connector frees its unused allocation for the other connectors
/// on the same fuse while the margin still lets its current rise every optimizer run.
///
/// The cap only ever lowers what FastCharging would allocate, never raises it: fuse
/// limits and the equal split between connectors remain entirely with the market. It
/// applies only while the connector is actually drawing (state Charging), only to the
/// schedule slot covering now (future slots are forecast, and the measurement describes
/// now), and never below the connector's minimum current. Reductions of the cap are
/// applied only after they have been pending for the configured hold time; increases are
/// applied immediately. A connector without a usable, fresh measurement is limited to its
/// minimum current plus the margin. Nodes offering no AC current limit (DC) are traded
/// like FastCharging.
///
/// Operates on a single connector: the measurement is read from this broker's own market
/// node, and the cap state survives between runs in the connector's BrokerContext.
class BrokerPowerRedistribution : public BrokerFastCharging {
public:
    BrokerPowerRedistribution(Market& market, BrokerContext& context, EnergyManagerConfig config);

    /// \brief Reads the connector's measurement into the broker context and decides the
    /// current cap for this run. Called once per optimizer run from the broker loop,
    /// before any trading round.
    void observe() override;

    /// \brief Narrows the offer to the cap decided by observe(), then trades with the
    /// unchanged BrokerFastCharging algorithm.
    void tradeImpl() override;

private:
    void decide_cap(const types::energy::EnergyFlowRequest& request);
    void limit_offer_to_cap();
    float sold_current_A(int slot);

    // Cap decided by observe() for this run, applied to every trading round of the run by
    // tradeImpl(). Per run by construction: a broker is built once per EVSE per run, so
    // nothing here survives a run - the state that has to lives in BrokerContext.
    std::optional<PhaseCurrents> run_cap_A;
    std::string run_cap_source;
};

} // namespace module
