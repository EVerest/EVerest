// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <generated/types/powermeter.hpp>

#include <optional>
#include <vector>

namespace module {
namespace generic_powermeter {

/// Register indices must match powermeterImpl::PowermeterRegisters (order and values).
enum class PowermeterRegisterIndex : int {
    ENERGY_WH_IMPORT_TOTAL = 0,
    ENERGY_WH_IMPORT_L1,
    ENERGY_WH_IMPORT_L2,
    ENERGY_WH_IMPORT_L3,
    ENERGY_WH_EXPORT_TOTAL,
    ENERGY_WH_EXPORT_L1,
    ENERGY_WH_EXPORT_L2,
    ENERGY_WH_EXPORT_L3,
    POWER_W_TOTAL,
    POWER_W_L1,
    POWER_W_L2,
    POWER_W_L3,
    VOLTAGE_V_DC,
    VOLTAGE_V_L1,
    VOLTAGE_V_L2,
    VOLTAGE_V_L3,
    REACTIVE_POWER_VAR_TOTAL,
    REACTIVE_POWER_VAR_L1,
    REACTIVE_POWER_VAR_L2,
    REACTIVE_POWER_VAR_L3,
    CURRENT_A_DC,
    CURRENT_A_L1,
    CURRENT_A_L2,
    CURRENT_A_L3,
    FREQUENCY_HZ_L1,
    FREQUENCY_HZ_L2,
    FREQUENCY_HZ_L3,
    NUM_PM_REGISTERS
};

/// Map a configured register to the top-level Measurement it feeds.
std::optional<types::powermeter::Measurement> measurement_for_register(int register_index);

/// Unique, stable-ordered list of measurements implied by the given register indices.
std::vector<types::powermeter::Measurement> unique_supported_measurements(const std::vector<int>& register_indices);

} // namespace generic_powermeter
} // namespace module
