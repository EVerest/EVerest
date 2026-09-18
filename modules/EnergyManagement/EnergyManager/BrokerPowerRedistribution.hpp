// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <optional>

#include "BrokerFastCharging.hpp"

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
/// holding steady unless its own timestamp is carried along. Everest::Date::from_rfc3339
/// does not throw - a default constructed time point is its only failure signal - so the
/// epoch doubles as the unparsable case, and a meter genuinely reporting 1970 is equally
/// unusable.
///
/// \returns the observed measurement, all fields std::nullopt if the node carries no
/// measurement at all
ObservedMeasurement read_measurement(const types::energy::EnergyFlowRequest& node);

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
