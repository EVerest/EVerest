// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <optional>

#include "BrokerFastCharging.hpp"

namespace module {

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
