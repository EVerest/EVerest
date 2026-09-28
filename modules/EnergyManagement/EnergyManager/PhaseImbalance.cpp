// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "PhaseImbalance.hpp"

#include <algorithm>
#include <array>
#include <map>

namespace module {

namespace {

constexpr std::array<Phase, 3> ALL_PHASES{Phase::L1, Phase::L2, Phase::L3};

std::optional<float> phase_value(const PhaseCurrents& currents, Phase phase) {
    switch (phase) {
    case Phase::L1:
        return currents.L1;
    case Phase::L2:
        return currents.L2;
    case Phase::L3:
    default:
        return currents.L3;
    }
}

// Per-phase current of a measurement, by the precedence phases_drawn_on() documents. A
// total is deliberately not spread: it cannot say which phase carries it.
PhaseCurrents per_phase_current(const ObservedMeasurement& measurement, float nominal_ac_voltage) {
    const auto& current = measurement.current_A;
    if (current.L1.has_value() or current.L2.has_value() or current.L3.has_value()) {
        return {current.L1, current.L2, current.L3};
    }
    if (not measurement.power_W.has_value() or nominal_ac_voltage <= 0.f) {
        return {};
    }
    const auto& power = measurement.power_W.value();
    const auto to_current = [nominal_ac_voltage](const std::optional<float>& watt) -> std::optional<float> {
        if (not watt.has_value()) {
            return std::nullopt;
        }
        return watt.value() / nominal_ac_voltage;
    };
    return {to_current(power.L1), to_current(power.L2), to_current(power.L3)};
}

PhaseReport& report_of(ImbalanceResult& result, Phase phase) {
    switch (phase) {
    case Phase::L1:
        return result.L1;
    case Phase::L2:
        return result.L2;
    case Phase::L3:
    default:
        return result.L3;
    }
}

} // namespace

const PhaseReport& ImbalanceResult::report(Phase phase) const {
    switch (phase) {
    case Phase::L1:
        return L1;
    case Phase::L2:
        return L2;
    case Phase::L3:
    default:
        return L3;
    }
}

const char* to_string(Phase phase) {
    switch (phase) {
    case Phase::L1:
        return "L1";
    case Phase::L2:
        return "L2";
    case Phase::L3:
    default:
        return "L3";
    }
}

std::set<Phase> phases_drawn_on(const ObservedMeasurement& measurement, float nominal_ac_voltage) {
    const auto currents = per_phase_current(measurement, nominal_ac_voltage);
    std::set<Phase> phases;
    for (const auto phase : ALL_PHASES) {
        const auto value = phase_value(currents, phase);
        if (value.has_value() and value.value() > PHASE_NOISE_FLOOR_A) {
            phases.insert(phase);
        }
    }
    return phases;
}

float measured_current_on(const ObservedMeasurement& measurement, const std::set<Phase>& phases,
                          float nominal_ac_voltage) {
    const auto currents = per_phase_current(measurement, nominal_ac_voltage);
    float highest = 0.f;
    for (const auto phase : phases) {
        const auto value = phase_value(currents, phase);
        if (value.has_value()) {
            highest = std::max(highest, value.value());
        }
    }
    return highest;
}

ImbalanceResult correct_phase_imbalance(const PhaseCurrents& site_A, const std::vector<ImbalanceConnector>& connectors,
                                        float max_phase_imbalance_A) {
    ImbalanceResult result;

    std::optional<float> reference_A;
    int known_phases = 0;
    for (const auto phase : ALL_PHASES) {
        const auto value = phase_value(site_A, phase);
        if (not value.has_value()) {
            continue;
        }
        known_phases++;
        if (not reference_A.has_value() or value.value() < reference_A.value()) {
            reference_A = value;
            result.reference = phase;
        }
    }
    if (known_phases < 2) {
        result.reference.reset();
        return result;
    }
    const auto reference = result.reference.value();

    // Each connector's share, the largest over the phases it is involved on.
    std::map<std::string, float> share_by_connector;

    for (const auto phase : ALL_PHASES) {
        const auto value = phase_value(site_A, phase);
        if (not value.has_value() or phase == reference) {
            continue;
        }
        auto& report = report_of(result, phase);
        report.imbalance_A = value.value() - reference_A.value();
        report.overshoot_A = std::max(0.f, report.imbalance_A - max_phase_imbalance_A);
        if (report.overshoot_A <= 0.f) {
            continue;
        }

        std::vector<const ImbalanceConnector*> involved;
        for (const auto& connector : connectors) {
            if (connector.draws_on.count(phase) > 0 and connector.draws_on.count(reference) == 0) {
                involved.push_back(&connector);
            }
        }
        if (involved.empty()) {
            report.residual_A = report.overshoot_A;
            continue;
        }

        const float share = report.overshoot_A / static_cast<float>(involved.size());
        for (const auto* connector : involved) {
            auto& current = share_by_connector[connector->uuid];
            current = std::max(current, share);
        }
    }

    for (const auto& connector : connectors) {
        const auto share = share_by_connector.find(connector.uuid);
        if (share == share_by_connector.end()) {
            continue;
        }
        const float base_A = std::min(connector.cap_A.value_or(connector.measured_A), connector.measured_A);
        const float new_cap_A = std::max(connector.min_A, base_A - share->second);
        const float delivered_A = base_A - new_cap_A;
        if (delivered_A <= 0.f) {
            continue;
        }
        result.cuts.push_back({connector.uuid, new_cap_A, delivered_A});
        for (const auto phase : connector.draws_on) {
            report_of(result, phase).corrected_A += delivered_A;
        }
    }

    for (const auto phase : ALL_PHASES) {
        auto& report = report_of(result, phase);
        if (report.overshoot_A > 0.f and report.residual_A == 0.f) {
            report.residual_A = std::max(0.f, report.overshoot_A - report.corrected_A);
        }
    }

    return result;
}

} // namespace module
