// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#ifndef ENERGY_PHASE_ROTATION_UTILS_HPP
#define ENERGY_PHASE_ROTATION_UTILS_HPP

#include <generated/types/powermeter.hpp>
#include <string>

namespace module {
namespace energy_grid {

/**
 * @brief Applies a phase_rotation config value ("123", "312" or "231") to the per-phase
 * voltage/current/power/energy/VAR values of a powermeter reading, correcting for a
 * physical L1/L2/L3 wiring rotation. "123" is the identity mapping (no-op). frequency_Hz
 * is intentionally left untouched since grid frequency is not phase-specific.
 *
 * @param powermeter The powermeter reading to rotate in place
 * @param phase_rotation One of "123", "312", "231"
 */
void apply_phase_rotation(types::powermeter::Powermeter& powermeter, const std::string& phase_rotation);

  } // namespace energy_grid
} // namespace module

#endif // ENERGY_PHASE_ROTATION_UTILS_HPP
