// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <optional>

#include <generated/types/powermeter.hpp>

namespace everest::helpers {

enum class PhaseRotation {
    RST, ///< no rotation, the reported phases already match the grid phases
    TRS, ///< reported L1/L2/L3 are grid L3/L1/L2
    STR, ///< reported L1/L2/L3 are grid L2/L3/L1
};

/// \returns the PhaseRotation for the given OCPP-style notation, RST for any unknown value
PhaseRotation phase_rotation_from_string(const std::string& phase_rotation);

/// \brief Remaps the per-phase (L1/L2/L3) members of \p powermeter according to \p phase_rotation , to correct
/// for a physical wiring rotation between the meter and the grid.
/// Uses OCPP-style notation: "RST" is the identity, "TRS" means the reported L1/L2/L3 are grid L3/L1/L2 and
/// "STR" means they are grid L2/L3/L1. Unknown values are treated as "RST".
/// Members that are invariant under a rotation (total, DC, N, frequency) are left untouched.
/// \returns the rotated powermeter reading
types::powermeter::Powermeter apply_phase_rotation(types::powermeter::Powermeter powermeter,
                                                   PhaseRotation phase_rotation);

/// \brief Whether \p a and \p b carry current above \p noise_floor_A on the same phases, for two meters in series
/// such as an EVSE's grid and car side meter.
/// \returns std::nullopt when it cannot be told: a meter reports no per-phase current, or either load is on no phase
/// or on all three
std::optional<bool> same_loaded_phases(const types::powermeter::Powermeter& a, const types::powermeter::Powermeter& b,
                                       float noise_floor_A);

} // namespace everest::helpers
