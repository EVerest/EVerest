// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "PhaseAllocation.hpp"

#include <algorithm>

namespace module {

float& PhaseAllocation::phase(int index) {
    switch (index) {
    case 1:
        return l1_A;
    case 2:
        return l2_A;
    default:
        return l3_A;
    }
}

float PhaseAllocation::phase(int index) const {
    switch (index) {
    case 1:
        return l1_A;
    case 2:
        return l2_A;
    default:
        return l3_A;
    }
}

float PhaseAllocation::min() const {
    return std::min({l1_A, l2_A, l3_A});
}

float PhaseAllocation::max() const {
    return std::max({l1_A, l2_A, l3_A});
}

float PhaseAllocation::imbalance_A() const {
    return max() - min();
}

PhaseAllocation operator+(const PhaseAllocation& a, const PhaseAllocation& b) {
    return {a.l1_A + b.l1_A, a.l2_A + b.l2_A, a.l3_A + b.l3_A};
}

PhaseAllocation effective_per_phase_limit(const types::energy::LimitsReq& limits, int active_phase_count) {
    PhaseAllocation effective;

    if (not limits.ac_max_current_A.has_value() and not limits.ac_max_current_per_phase_A.has_value()) {
        return effective;
    }

    // Start from the symmetric limit, which applies to every phase equally.
    const auto symmetric_A = limits.ac_max_current_A.has_value() ? limits.ac_max_current_A.value().value : 0.f;

    for (int p = 1; p <= 3; p++) {
        if (p > active_phase_count) {
            // This phase is not in use by the connector, so nothing can flow on it.
            effective.phase(p) = 0.f;
            continue;
        }

        effective.phase(p) = symmetric_A;

        if (not limits.ac_max_current_per_phase_A.has_value()) {
            continue;
        }

        const auto& per_phase = limits.ac_max_current_per_phase_A.value();
        const auto& phase_limit = (p == 1) ? per_phase.L1 : (p == 2) ? per_phase.L2 : per_phase.L3;

        if (not phase_limit.has_value()) {
            // Phase not constrained individually: the symmetric limit stands.
            continue;
        }

        if (limits.ac_max_current_A.has_value()) {
            // Both present: neither may widen the other.
            effective.phase(p) = std::min(symmetric_A, phase_limit.value().value);
        } else {
            effective.phase(p) = phase_limit.value().value;
        }
    }

    return effective;
}

bool is_within_symmetry(const PhaseAllocation& allocation, float max_imbalance_A) {
    return allocation.imbalance_A() <= max_imbalance_A;
}

types::energy::NumberWithSourcePerPhase to_per_phase_limit(const PhaseAllocation& allocation,
                                                           const std::string& source) {
    types::energy::NumberWithSourcePerPhase limit;
    limit.L1 = types::energy::NumberWithSource{allocation.l1_A, source};
    limit.L2 = types::energy::NumberWithSource{allocation.l2_A, source};
    limit.L3 = types::energy::NumberWithSource{allocation.l3_A, source};
    return limit;
}

} // namespace module
