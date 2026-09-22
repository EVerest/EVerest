// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "BrokerPowerRedistribution.hpp"

#include <algorithm>

#include <everest/logging.hpp>

namespace module {

namespace {

// Selects the single power meter reading an observation is taken from. One selection for
// power, current and timestamp: resolving them through independent lookups is how a value
// ends up carrying another meter's age, or how a per-phase current ends up next to a total
// that came from a different meter.
//
// Power decides the choice, and only when no side reports power does current decide it.
// Within each of those two passes the leaves side (what EvseManager reports for an EVSE)
// wins over the root side. Selecting on "carries anything usable" instead would let a
// leaves reading with current but no power hide a root reading that does have power - the
// measurement every consumer of this actually compares an allocation against.
const types::powermeter::Powermeter* find_reading(const types::energy::EnergyFlowRequest& node) {
    const auto pick =
        [&node](bool (*carries)(const types::powermeter::Powermeter&)) -> const types::powermeter::Powermeter* {
        if (node.energy_usage_leaves.has_value() and carries(node.energy_usage_leaves.value())) {
            return &node.energy_usage_leaves.value();
        }
        if (node.energy_usage_root.has_value() and carries(node.energy_usage_root.value())) {
            return &node.energy_usage_root.value();
        }
        return nullptr;
    };

    if (const auto* reading = pick([](const types::powermeter::Powermeter& p) { return p.power_W.has_value(); })) {
        return reading;
    }

    return pick([](const types::powermeter::Powermeter& p) { return p.current_A.has_value(); });
}

// True while the connector's measured consumption can be taken for the demand of its EV.
// A session that has not started drawing (WaitForAuth, PrepareCharging) or has stopped
// (PausedEV, PausedEVSE) measures zero for a reason that says nothing about what the EV
// asks for once it draws, so its measurement must not become its limit. A node that
// declares no state at all is not excluded, for the same reason in_session() does not
// exclude it.
bool consumption_is_demand(const types::energy::EnergyFlowRequest& node) {
    return not node.evse_state.has_value() or node.evse_state.value() == types::energy::EvseState::Charging;
}

PhaseCurrents uniform_phases(float value) {
    return {value, value, value};
}

std::optional<float> add_margin(const std::optional<float>& phase, float margin_A) {
    if (not phase.has_value()) {
        return std::nullopt;
    }
    return phase.value() + margin_A;
}

} // namespace

ObservedMeasurement read_measurement(const types::energy::EnergyFlowRequest& node) {
    const auto* reading = find_reading(node);
    if (reading == nullptr) {
        return {};
    }

    ObservedMeasurement measurement;
    measurement.power_W = reading->power_W;
    measurement.current_A = reading->current_A.value_or(types::units::Current{});
    measurement.measured_at = parse_meter_timestamp(reading->timestamp);
    return measurement;
}

PhaseCurrents measured_phase_currents(const ObservedMeasurement& measurement, float nominal_ac_voltage,
                                      int active_phases) {
    const auto& current = measurement.current_A;
    if (current.L1.has_value() or current.L2.has_value() or current.L3.has_value()) {
        return {current.L1, current.L2, current.L3};
    }

    if (not measurement.power_W.has_value() or nominal_ac_voltage <= 0.0f) {
        return {};
    }

    const auto& power = measurement.power_W.value();
    const auto phase_power_to_current = [nominal_ac_voltage](const std::optional<float>& watt) -> std::optional<float> {
        if (not watt.has_value()) {
            return std::nullopt;
        }
        return watt.value() / nominal_ac_voltage;
    };
    if (power.L1.has_value() or power.L2.has_value() or power.L3.has_value()) {
        return {phase_power_to_current(power.L1), phase_power_to_current(power.L2), phase_power_to_current(power.L3)};
    }

    // Only the total is known: spread it over the active phases. Which physical phases
    // those are is unknowable from a total, but the collapse to a single limit makes the
    // assignment irrelevant - only the per-phase magnitude matters.
    const int phases = std::max(active_phases, 1);
    const float per_phase = power.total / nominal_ac_voltage / static_cast<float>(phases);
    PhaseCurrents currents;
    currents.L1 = per_phase;
    if (phases >= 2) {
        currents.L2 = per_phase;
    }
    if (phases >= 3) {
        currents.L3 = per_phase;
    }
    return currents;
}

bool measurement_can_limit(const ObservedMeasurement& measurement, date::utc_clock::time_point now,
                           std::chrono::seconds max_age) {
    const bool has_value = measurement.power_W.has_value() or measurement.current_A.L1.has_value() or
                           measurement.current_A.L2.has_value() or measurement.current_A.L3.has_value();
    if (not has_value) {
        return false;
    }
    return is_fresh(measurement.measured_at, now, max_age);
}

std::optional<float> to_scalar_cap(const PhaseCurrents& cap) {
    std::optional<float> scalar;
    for (const auto& phase : {cap.L1, cap.L2, cap.L3}) {
        if (phase.has_value() and (not scalar.has_value() or phase.value() > scalar.value())) {
            scalar = phase;
        }
    }
    return scalar;
}

BrokerPowerRedistribution::BrokerPowerRedistribution(Market& market, BrokerContext& context,
                                                     EnergyManagerConfig config) :
    BrokerFastCharging(market, context, config) {
}

void BrokerPowerRedistribution::observe() {
    const auto& request = local_market.energy_flow_request;

    // Only sessions have a consumption worth observing. The optimizer runs continuously
    // and constructs a broker for every EVSE on every run; without this guard an idle
    // meterless connector would trip the missing-measurement warning.
    if (not in_session(request)) {
        return;
    }

    context.last_observed_measurement = read_measurement(request);

    const auto& measurement = context.last_observed_measurement;
    const bool has_reading = measurement.power_W.has_value() or measurement.current_A.L1.has_value() or
                             measurement.current_A.L2.has_value() or measurement.current_A.L3.has_value();
    if (measurement.power_W.has_value()) {
        EVLOG_debug << request.uuid << ": measured power " << measurement.power_W.value().total << " W";
    } else if (not has_reading and not context.tracking_warned_no_measurement) {
        // Warn once per session, not once per optimizer run. A current-only reading is not
        // warned about: it is a usable measurement for the current cap.
        context.tracking_warned_no_measurement = true;
        EVLOG_warning << request.uuid << ": power meter tracking enabled but no measurement available";
    }

    decide_cap(request);
}

void BrokerPowerRedistribution::decide_cap(const types::energy::EnergyFlowRequest& request) {
    if (not consumption_is_demand(request)) {
        // WaitForAuth, PrepareCharging or a pause: the connector measures zero for a
        // reason that says nothing about its demand. Dropping the whole state (not just
        // the cap) makes a resume start over like a new session, start value included.
        context.redistribution_cap_A = std::nullopt;
        context.redistribution_reduction_pending_since = std::nullopt;
        return;
    }

    // The limits the cap is expressed against, from the slot covering now. observe() runs
    // before any trading round, so the available energy still equals the full offer.
    const auto available = local_market.get_available_energy_import();
    int slot = 0;
    for (int i = 0; i < static_cast<int>(available.size()); i++) {
        if (time_slot_active(i, available)) {
            slot = i;
            break;
        }
    }
    const auto& limits = available[slot].limits_to_root;

    if (not limits.ac_max_current_A.has_value()) {
        // Watt-only node (DC): redistribution trades ampere. Left to the FastCharging
        // algorithm, see the manifest's broker_strategy description.
        context.redistribution_cap_A = std::nullopt;
        context.redistribution_reduction_pending_since = std::nullopt;
        return;
    }

    const auto& redistribution = config.redistribution;
    const float min_current_A = limits.ac_min_current_A.has_value() ? limits.ac_min_current_A.value().value : 0.0f;
    const float lower_limit_A = min_current_A + redistribution.margin_A;

    // First drawing run of the session: set the start value the tracking departs from.
    if (not context.redistribution_cap_A.has_value()) {
        const float start_A =
            redistribution.start_with_lower_limit ? lower_limit_A : limits.ac_max_current_A.value().value;
        context.redistribution_cap_A = uniform_phases(start_A);
        run_cap_source = "BrokerPowerRedistribution_SessionStart";
    }

    PhaseCurrents candidate;
    std::string source;
    if (measurement_can_limit(context.last_observed_measurement, globals.start_time,
                              redistribution.measurement_max_age)) {
        const auto measured =
            measured_phase_currents(context.last_observed_measurement, local_market.nominal_ac_voltage(),
                                    limits.ac_number_of_active_phases.value_or(1));
        candidate = {add_margin(measured.L1, redistribution.margin_A), add_margin(measured.L2, redistribution.margin_A),
                     add_margin(measured.L3, redistribution.margin_A)};
        source = "BrokerPowerRedistribution_MeasuredPlusMargin";
    }
    if (not to_scalar_cap(candidate).has_value()) {
        // No usable, fresh reading (or one no current can be derived from): the connector
        // is limited to its minimum plus the margin rather than left uncapped - a dead
        // meter must not hold an allocation open.
        candidate = uniform_phases(lower_limit_A);
        source = "BrokerPowerRedistribution_NoMeasurement";
    }

    // Reductions wait out the hold; increases are applied immediately - the immediacy is
    // the per-interval headroom the margin exists for.
    const auto applied_scalar = to_scalar_cap(context.redistribution_cap_A.value());
    const auto candidate_scalar = to_scalar_cap(candidate);
    if (applied_scalar.has_value() and candidate_scalar.value() < applied_scalar.value()) {
        if (not context.redistribution_reduction_pending_since.has_value()) {
            context.redistribution_reduction_pending_since = globals.start_time;
        }
        if (globals.start_time - context.redistribution_reduction_pending_since.value() <
            redistribution.reduction_hold) {
            run_cap_A = context.redistribution_cap_A;
            if (run_cap_source.empty()) {
                run_cap_source = "BrokerPowerRedistribution_ReductionHold";
            }
            return;
        }
    }

    context.redistribution_reduction_pending_since = std::nullopt;
    context.redistribution_cap_A = candidate;
    run_cap_A = candidate;
    run_cap_source = source;
}

void BrokerPowerRedistribution::tradeImpl() {
    limit_offer_to_cap();
    BrokerFastCharging::tradeImpl();
}

void BrokerPowerRedistribution::limit_offer_to_cap() {
    if (not run_cap_A.has_value()) {
        return;
    }
    const auto cap = to_scalar_cap(run_cap_A.value());
    if (not cap.has_value()) {
        return;
    }

    // Only the slot covering now: the measurement describes now, and the future slots are
    // forecast the market still plans with the full request.
    for (int i = 0; i < static_cast<int>(offer->import_offer.size()); i++) {
        if (not time_slot_active(i, offer->import_offer)) {
            continue;
        }
        auto& limits = offer->import_offer[i].limits_to_root;
        if (not limits.ac_max_current_A.has_value()) {
            return;
        }

        // Never below the minimum the all-or-nothing first trade must be able to buy, and
        // never 0: FastCharging reads a max current of 0 as "cannot import" and would flip
        // the slot to export.
        const float min_current_A = limits.ac_min_current_A.has_value() ? limits.ac_min_current_A.value().value : 0.0f;
        const float floor_A = std::max(min_current_A, globals.slice_ampere);

        // The offer is what is left of the path to the root, so the cap has to be
        // expressed as what is left of it too: tradeImpl() runs once per trading round and
        // every round buys on top of the rounds before it.
        const float allowance_A = std::max(cap.value(), floor_A) - sold_current_A(i);

        apply_limit_if_smaller(limits.ac_max_current_A, std::max(0.0f, allowance_A), run_cap_source);
        return;
    }
}

float BrokerPowerRedistribution::sold_current_A(int slot) {
    const auto sold = local_market.get_sold_energy();
    if (slot >= static_cast<int>(sold.size()) or not sold[slot].limits_to_root.ac_max_current_A.has_value()) {
        return 0.0f;
    }
    return std::max(0.0f, sold[slot].limits_to_root.ac_max_current_A.value().value);
}

} // namespace module
