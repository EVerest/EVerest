// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <optional>

#include "BrokerFastCharging.hpp"

namespace module {

/// \brief Extracts the imported power measurement from a node of the energy tree.
/// Prefers the leaves side measurement (what EvseManager reports for an EVSE) and falls
/// back to the root side measurement.
/// \returns measured power in Watt (total, plus per-phase L1/L2/L3 when the meter reports
/// them), or std::nullopt if the node carries no power measurement
std::optional<types::units::Power> get_measured_power_W(const types::energy::EnergyFlowRequest& node);

/// \brief Measurement timestamp of the reading get_measured_power_W() took its value from,
/// using the same leaves-before-root precedence so the age always belongs to the value.
///
/// A power meter reading without a usable timestamp has no age a consumer could check, so
/// it is reported as absent rather than as "now": EnergyNode and EvseManager republish the
/// last reading they received on every request, which makes a meter that stopped updating
/// indistinguishable from one holding steady unless its own timestamp is carried along.
/// Everest::Date::from_rfc3339 does not throw - a default constructed time point is its
/// only failure signal - so the epoch doubles as the unparsable case, and a meter genuinely
/// reporting 1970 is equally unusable.
/// \returns the reading's measurement time, or std::nullopt when the node carries no power
/// measurement or its timestamp is unusable
std::optional<date::utc_clock::time_point> get_measured_time(const types::energy::EnergyFlowRequest& node);

/// \brief Extracts the per-phase current measurement (L1/L2/L3) from a node of the energy
/// tree. Prefers the leaves side measurement and falls back to the root side, like
/// get_measured_power_W(). Phases the meter does not report stay nullopt (a single-phase
/// meter reports only L1) - they must not be read as zero.
/// Per-phase values are what per-phase trading and asymmetric load limits are expressed in,
/// the latter as a threshold in ampere per phase.
/// \returns measured current per phase in Ampere; all phases nullopt if no current measurement
types::units::Current get_measured_current_A(const types::energy::EnergyFlowRequest& node);

/// \brief Broker of the PowerRedistribution strategy. In this stage it trades exactly like
/// BrokerFastCharging and additionally observes the live power meter measurement of its
/// connector; redistributing energy based on that observation is future work.
///
/// It never modifies the allocation: trading is delegated unchanged to the base class. The
/// class exists to give that future change somewhere to land - it is the seat the
/// redistribution logic takes once allocations are actually modified - and until then its
/// only behaviour is the observe() override below.
///
/// Operates on a single connector: the measurement is read from this broker's own market node.
class BrokerPowerRedistribution : public BrokerFastCharging {
public:
    BrokerPowerRedistribution(Market& market, BrokerContext& context, EnergyManagerConfig config);

    /// \brief Reads and logs the connector's measurement into the broker context. Called
    /// once per optimizer run from the broker loop, before any trading round.
    void observe() override;
};

} // namespace module
