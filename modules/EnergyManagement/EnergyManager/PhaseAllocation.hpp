// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <string>

#include <generated/interfaces/energy/Interface.hpp>

namespace module {

/// \brief Current allocated to, or available on, each of the three AC phases [A].
///
/// The optimizer historically worked with a single "per phase" current that applied
/// identically to L1, L2 and L3. This type makes the three phases independent so that a
/// single phase vehicle occupies capacity on one phase only, and so that an unbalanced
/// installation can be modelled accurately.
struct PhaseAllocation {
    float l1_A{0.f};
    float l2_A{0.f};
    float l3_A{0.f};

    /// \brief Access a phase by 1 based index (1 -> L1). Out of range maps to L3.
    float& phase(int index);
    float phase(int index) const;

    float min() const;
    float max() const;

    /// \brief Spread between the most and least loaded phase [A].
    float imbalance_A() const;
};

PhaseAllocation operator+(const PhaseAllocation& a, const PhaseAllocation& b);

/// \brief Resolves the effective current limit of each phase from a limits object.
///
/// Combines the symmetric ac_max_current_A with the optional per phase
/// ac_max_current_per_phase_A, taking the smaller of the two on each phase so that
/// neither can widen the other. Phases beyond \p active_phase_count are zero, since a
/// connector charging on one phase cannot draw on the others.
/// A limits object with no current limit at all yields all zeros.
PhaseAllocation effective_per_phase_limit(const types::energy::LimitsReq& limits, int active_phase_count);

/// \brief True when no two phases differ by more than \p max_imbalance_A.
bool is_within_symmetry(const PhaseAllocation& allocation, float max_imbalance_A);

/// \brief Converts an allocation into the wire type, stamping every phase with \p source.
types::energy::NumberWithSourcePerPhase to_per_phase_limit(const PhaseAllocation& allocation,
                                                           const std::string& source);

} // namespace module
