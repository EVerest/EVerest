// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "supported_measurands.hpp"

#include <set>

namespace module {
namespace generic_powermeter {

std::optional<types::powermeter::Measurand> measurand_for_register(int register_index) {
    using R = PowermeterRegisterIndex;
    using M = types::powermeter::Measurand;

    if (register_index < 0 or register_index >= static_cast<int>(R::NUM_PM_REGISTERS)) {
        return std::nullopt;
    }

    if (register_index <= static_cast<int>(R::ENERGY_WH_IMPORT_L3)) {
        return M::EnergyImport;
    }
    if (register_index <= static_cast<int>(R::ENERGY_WH_EXPORT_L3)) {
        return M::EnergyExport;
    }
    if (register_index <= static_cast<int>(R::POWER_W_L3)) {
        return M::Power;
    }
    if (register_index <= static_cast<int>(R::VOLTAGE_V_L3)) {
        return M::Voltage;
    }
    if (register_index <= static_cast<int>(R::REACTIVE_POWER_VAR_L3)) {
        return M::ReactivePower;
    }
    if (register_index <= static_cast<int>(R::CURRENT_A_L3)) {
        return M::Current;
    }
    return M::Frequency;
}

std::vector<types::powermeter::Measurand> unique_supported_measurands(const std::vector<int>& register_indices) {
    // Keep declaration order of Measurand enum for a stable, readable list.
    static constexpr types::powermeter::Measurand k_order[] = {
        types::powermeter::Measurand::EnergyImport,  types::powermeter::Measurand::EnergyExport,
        types::powermeter::Measurand::Power,         types::powermeter::Measurand::Voltage,
        types::powermeter::Measurand::ReactivePower, types::powermeter::Measurand::Current,
        types::powermeter::Measurand::Frequency,
    };

    std::set<types::powermeter::Measurand> present;
    for (const int index : register_indices) {
        if (const auto measurand = measurand_for_register(index)) {
            present.insert(*measurand);
        }
    }

    std::vector<types::powermeter::Measurand> result;
    result.reserve(present.size());
    for (const auto measurand : k_order) {
        if (present.count(measurand) != 0) {
            result.push_back(measurand);
        }
    }
    return result;
}

} // namespace generic_powermeter
} // namespace module
