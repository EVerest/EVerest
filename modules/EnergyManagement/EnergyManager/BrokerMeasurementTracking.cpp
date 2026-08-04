// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "BrokerMeasurementTracking.hpp"

#include <algorithm>
#include <string>

#include <everest/logging.hpp>

namespace module {

namespace {
constexpr int ASSUMED_PHASE_COUNT = 3;
}

std::optional<float> get_measured_power_W(const types::energy::EnergyFlowRequest& node) {
    if (node.energy_usage_leaves.has_value() and node.energy_usage_leaves.value().power_W.has_value()) {
        return node.energy_usage_leaves.value().power_W.value().total;
    }

    if (node.energy_usage_root.has_value() and node.energy_usage_root.value().power_W.has_value()) {
        return node.energy_usage_root.value().power_W.value().total;
    }

    return std::nullopt;
}

BrokerMeasurementTracking::BrokerMeasurementTracking(Market& market, BrokerContext& context,
                                                     EnergyManagerConfig config) :
    BrokerFastCharging(market, context, config) {
    // Computed exactly once per optimizer run: EnergyManagerImpl builds a fresh broker for
    // every run, while `context` survives for the whole charging session. tradeImpl() runs
    // once per trading round and must not recompute this - doing so would flip
    // context.tracking_active after the first round and lose the initial current.
    tracking_limit_W = compute_tracking_limit_W();
}

std::optional<float> BrokerMeasurementTracking::compute_tracking_limit_W() {
    const auto& request = local_market.energy_flow_request;

    if (request.schedule_import.empty()) {
        return std::nullopt;
    }

    const auto& limits = request.schedule_import[0].limits_to_root;
    const auto phase_count =
        limits.ac_max_phase_count.has_value() ? limits.ac_max_phase_count.value().value : ASSUMED_PHASE_COUNT;
    const float watt_per_ampere = static_cast<float>(phase_count) * local_market.nominal_ac_voltage();

    float limit_W = 0.f;

    if (not context.tracking_active) {
        // First optimizer run of this session: ask for the configured starting current.
        // The context is cleared on unplug, so this happens once per session.
        context.tracking_active = true;
        limit_W = config.tracking_initial_current_A * watt_per_ampere;
    } else {
        const auto measured_W = get_measured_power_W(request);

        if (not measured_W.has_value()) {
            // No measurement for this connector. Do not starve it - leave the static limits
            // in place and let BrokerFastCharging allocate as usual.
            EVLOG_warning << request.uuid
                          << ": power meter tracking enabled but no measurement available, "
                             "falling back to static limits";
            return std::nullopt;
        }

        limit_W = measured_W.value() + config.tracking_margin_W;
    }

    // Never track below the minimum current the EVSE can signal. BrokerFastCharging buys
    // ac_min_current_A with allow_less=false, so a lower budget makes that purchase fail and
    // the connector receives nothing at all - the session stops instead of being trimmed.
    if (limits.ac_min_current_A.has_value()) {
        limit_W = std::max(limit_W, limits.ac_min_current_A.value().value * watt_per_ampere);
    }

    return limit_W;
}

void BrokerMeasurementTracking::tradeImpl() {
    if (tracking_limit_W.has_value()) {
        const std::string source = local_market.energy_flow_request.uuid + "/PowerMeterTracking";
        const auto sold = local_market.get_sold_energy();

        for (int i = 0; i < globals.schedule_length; i++) {
            // Subtract what this connector already bought during this run. Market only
            // subtracts sold watts from an offer when the tree itself declares a watt limit
            // (Market.cpp:281), so without this the full budget is re-offered every round and
            // the allocation climbs slice by slice to the static limit.
            float already_bought_W = 0.f;
            if (static_cast<ScheduleRes::size_type>(i) < sold.size() and
                sold[i].limits_to_root.total_power_W.has_value()) {
                already_bought_W = std::max(0.f, sold[i].limits_to_root.total_power_W.value().value);
            }

            const float remaining_W = std::max(0.f, tracking_limit_W.value() - already_bought_W);

            // apply_limit_if_smaller never raises an existing limit, so a tighter static or
            // external limit still wins.
            apply_limit_if_smaller(offer->import_offer[i].limits_to_root.total_power_W, remaining_W, source);
        }
    }

    // Let the proven fast charging algorithm do the actual trading on the narrowed offer.
    BrokerFastCharging::tradeImpl();
}

} // namespace module
