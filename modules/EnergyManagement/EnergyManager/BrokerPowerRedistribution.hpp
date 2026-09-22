// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <chrono>
#include <optional>
#include <string>

#include "BrokerFastCharging.hpp"
#include "PowerMeterAggregator.hpp"

namespace module {

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
