// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <chrono>
#include <optional>
#include <string>

#include "BrokerFastCharging.hpp"

namespace module {

/// \brief Reads the power meter measurement of one node of the energy tree.
///
/// All fields come from a single reading, so a value never carries another meter's
/// timestamp. The reading that reports power wins, leaves side before root side; current
/// decides only when neither reports power. Phases a meter does not report stay
/// std::nullopt.
///
/// A reading without a parsable timestamp gets no measured_at: EnergyNode and EvseManager
/// republish the last reading on every request, so only its own timestamp tells a frozen
/// meter from a steady one. Everest::Date::from_rfc3339 signals failure with the epoch.
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

/// \brief True while \p measurement has a value and its own timestamp is at most
/// \p max_age old. A timestamp in the future is accepted.
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
/// configured hold; increases apply immediately. Without a fresh measurement the connector
/// is capped at its minimum current plus the margin. Nodes without an AC current limit (DC)
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
