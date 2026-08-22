// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <optional>

#include "BrokerFastCharging.hpp"

namespace module {

/// \brief Extracts the total imported power measurement from a node of the energy tree.
/// Prefers the leaves side measurement (what EvseManager reports for an EVSE) and falls
/// back to the root side measurement.
/// The reading's own timestamp is not checked here: staleness handling of aggregated
/// measurements is WP1.b's PowerMeterAggregator's job, and a per-connector broker
/// tracking a frozen value converges to that value plus the margin, which is safe.
/// \returns measured power in Watt, or std::nullopt if the node carries no power measurement
std::optional<float> get_measured_power_W(const types::energy::EnergyFlowRequest& node);

/// \brief A broker that follows live power meter measurements instead of relying purely on
/// the static fuse limits.
///
/// On the first optimizer run of a charging session it requests
/// config.tracking_initial_current_A. On every later run it limits its request to the
/// measured power plus config.tracking_margin_W, floored at the power needed for the EVSE's
/// minimum current so a session is never starved below what it can signal.
///
/// The limit is a budget for the whole optimizer run. Because tradeImpl() is called once per
/// trading round against a fresh Offer, it applies the budget minus what this connector has
/// already bought during the run, and then delegates to BrokerFastCharging::tradeImpl() so
/// that phase count handling, hysteresis and existing limits behave exactly as before.
///
/// Operates on a single connector: the measurement is read from this broker's own market node.
class BrokerMeasurementTracking : public BrokerFastCharging {
public:
    BrokerMeasurementTracking(Market& market, BrokerContext& context, EnergyManagerConfig config);

    void tradeImpl() override;

protected:
    /// \brief Computes the power budget for this optimizer run.
    /// Called exactly once, from the constructor - never per trading round.
    /// \returns the budget in Watt, or std::nullopt to leave the static limits untouched
    virtual std::optional<float> compute_tracking_limit_W();

private:
    std::optional<float> tracking_limit_W;
};

} // namespace module
