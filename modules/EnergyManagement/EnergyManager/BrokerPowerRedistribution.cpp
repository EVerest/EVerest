// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "BrokerPowerRedistribution.hpp"

#include <algorithm>

#include <everest/logging.hpp>

namespace module {

namespace {

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

std::optional<date::utc_clock::time_point> measured_time_of(const types::powermeter::Powermeter& reading) {
    const auto measured_at = Everest::Date::from_rfc3339(reading.timestamp);
    if (measured_at == date::utc_clock::time_point{}) {
        return std::nullopt;
    }
    return measured_at;
}

// Outside Charging a connector measures zero for reasons unrelated to what the EV will draw.
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
    measurement.measured_at = measured_time_of(*reading);
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

    // Which phases carry a total is unknown; only the magnitude matters for a scalar cap.
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
    if (not measurement.measured_at.has_value()) {
        return false;
    }
    if (max_age <= std::chrono::seconds::zero()) {
        return true;
    }
    return now - measurement.measured_at.value() <= max_age;
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

    if (not in_session(request)) {
        return;
    }

    context.last_observed_measurement = read_measurement(request);

    const auto& measurement = context.last_observed_measurement;
    const bool has_reading = measurement.power_W.has_value() or measurement.current_A.L1.has_value() or
                             measurement.current_A.L2.has_value() or measurement.current_A.L3.has_value();
    if (measurement.power_W.has_value()) {
        EVLOG_debug << request.uuid << ": measured power " << measurement.power_W.value().total << " W";
    } else if (not has_reading and not context.redistribution_warned_no_measurement) {
        context.redistribution_warned_no_measurement = true;
        EVLOG_warning << request.uuid << ": power redistribution has no measurement, capping at minimum current";
    }

    decide_cap(request);
}

void BrokerPowerRedistribution::decide_cap(const types::energy::EnergyFlowRequest& request) {
    if (not consumption_is_demand(request)) {
        // A resume starts over like a new session, start value included.
        context.redistribution_cap_A = std::nullopt;
        context.redistribution_reduction_pending_since = std::nullopt;
        return;
    }

    // observe() runs before any trading round, so the available energy is the full offer.
    const auto available = local_market.get_available_energy_import();
    const auto& limits = available[globals.active_slot].limits_to_root;

    if (not limits.ac_max_current_A.has_value()) {
        // Watt-only node (DC): traded like FastCharging.
        context.redistribution_cap_A = std::nullopt;
        context.redistribution_reduction_pending_since = std::nullopt;
        return;
    }

    const auto& redistribution = config.redistribution;
    const float min_current_A = limits.ac_min_current_A.has_value() ? limits.ac_min_current_A.value().value : 0.0f;
    const float lower_limit_A = min_current_A + redistribution.margin_A;

    if (not context.redistribution_cap_A.has_value()) {
        const float start_A =
            redistribution.start_with_lower_limit ? lower_limit_A : limits.ac_max_current_A.value().value;
        context.redistribution_cap_A = uniform_phases(start_A);
        m_run_cap_source = "BrokerPowerRedistribution_SessionStart";
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
        // A dead meter must not hold an allocation open.
        candidate = uniform_phases(lower_limit_A);
        source = "BrokerPowerRedistribution_NoMeasurement";
    }

    const auto applied_scalar = to_scalar_cap(context.redistribution_cap_A.value());
    const auto candidate_scalar = to_scalar_cap(candidate);
    if (applied_scalar.has_value() and candidate_scalar.value() < applied_scalar.value()) {
        if (not context.redistribution_reduction_pending_since.has_value()) {
            context.redistribution_reduction_pending_since = globals.start_time;
        }
        if (globals.start_time - context.redistribution_reduction_pending_since.value() <
            redistribution.reduction_hold) {
            m_run_cap_A = context.redistribution_cap_A;
            if (m_run_cap_source.empty()) {
                m_run_cap_source = "BrokerPowerRedistribution_ReductionHold";
            }
            return;
        }
    }

    context.redistribution_reduction_pending_since = std::nullopt;
    context.redistribution_cap_A = candidate;
    m_run_cap_A = candidate;
    m_run_cap_source = source;
}

void BrokerPowerRedistribution::tradeImpl() {
    limit_offer_to_cap();
    BrokerFastCharging::tradeImpl();
}

void BrokerPowerRedistribution::limit_offer_to_cap() {
    if (not m_run_cap_A.has_value()) {
        return;
    }
    const auto cap = to_scalar_cap(m_run_cap_A.value());
    if (not cap.has_value()) {
        return;
    }

    const int slot = globals.active_slot;
    auto& limits = offer->import_offer[slot].limits_to_root;
    if (not limits.ac_max_current_A.has_value()) {
        return;
    }

    // Never below what the all-or-nothing first trade must buy, and never 0: FastCharging
    // reads a max current of 0 as "cannot import" and flips the slot to export.
    const float min_current_A = limits.ac_min_current_A.has_value() ? limits.ac_min_current_A.value().value : 0.0f;
    const float floor_A = std::max(min_current_A, globals.slice_ampere);

    // The offer is what is left after earlier trading rounds, so the cap is too.
    const float allowance_A = std::max(cap.value(), floor_A) - sold_current_A(slot);

    apply_limit_if_smaller(limits.ac_max_current_A, std::max(0.0f, allowance_A), m_run_cap_source);
}

float BrokerPowerRedistribution::sold_current_A(int slot) const {
    const auto& sold = local_market.get_sold_energy();
    if (slot >= static_cast<int>(sold.size()) or not sold[slot].limits_to_root.ac_max_current_A.has_value()) {
        return 0.0f;
    }
    return std::max(0.0f, sold[slot].limits_to_root.ac_max_current_A.value().value);
}

} // namespace module
