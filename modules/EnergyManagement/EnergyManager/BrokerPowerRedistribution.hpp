// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <chrono>
#include <optional>
#include <string>

#include "BrokerFastCharging.hpp"

namespace module {

/// \brief Measurement of one node, from a single reading so no value carries another meter's
/// timestamp. The reading with power wins, leaves side before root side, then current.
/// measured_at stays empty without a parsable timestamp: the last reading is republished on
/// every request, so only its own timestamp reveals a frozen meter.
/// \returns all fields std::nullopt if the node carries none
ObservedMeasurement read_measurement(const types::energy::EnergyFlowRequest& node);

/// \brief Per-phase current: measured current, else per-phase power / \p nominal_ac_voltage,
/// else total power over \p active_phases (pass 1 when unknown, the least restrictive).
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

/// \brief Trades like BrokerFastCharging, but caps a charging connector at its measured
/// current plus a margin, freeing the rest for connectors on the same fuse. The cap lowers
/// only the slot covering now, never below the minimum current; reductions wait out the hold.
/// Without a fresh measurement the cap is the minimum plus the margin. DC nodes are not capped.
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
