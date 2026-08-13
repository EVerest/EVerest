// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <string>

#include <generated/types/powermeter.hpp>

namespace everest {
namespace phase_rotation {

/// \brief Corrects for a physical L1/L2/L3 wiring rotation at a powermeter by remapping the per-phase
/// voltage/current/power/energy/VAR values of \p powermeter in place, so that the reported phases align with
/// the actual grid phases.
/// \param powermeter the powermeter reading to correct in place
/// \param phase_rotation one of "RST" (no rotation, the default), "TRS", or "STR"
void apply_phase_rotation(types::powermeter::Powermeter& powermeter, const std::string& phase_rotation);

} // namespace phase_rotation
} // namespace everest
