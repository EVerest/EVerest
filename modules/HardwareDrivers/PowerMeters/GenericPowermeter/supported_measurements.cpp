// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "supported_measurements.hpp"

#include <set>

namespace module {
namespace generic_powermeter {

std::optional<types::powermeter::Measurement> measurement_for_register(int register_index) {
    using R = PowermeterRegisterIndex;
    using M = types::powermeter::Measurement;

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

std::vector<types::powermeter::Measurement> unique_supported_measurements(const std::vector<int>& register_indices) {
    // Keep declaration order of Measurement enum for a stable, readable list.
    static constexpr types::powermeter::Measurement k_order[] = {
        types::powermeter::Measurement::EnergyImport, types::powermeter::Measurement::EnergyExport,
        types::powermeter::Measurement::Power,        types::powermeter::Measurement::Voltage,
        types::powermeter::Measurement::ReactivePower, types::powermeter::Measurement::Current,
        types::powermeter::Measurement::Frequency,
    };

    std::set<types::powermeter::Measurement> present;
    for (const int index : register_indices) {
        if (const auto measurement = measurement_for_register(index)) {
            present.insert(*measurement);
        }
    }

    std::vector<types::powermeter::Measurement> result;
    result.reserve(present.size());
    for (const auto measurement : k_order) {
        if (present.count(measurement) != 0) {
            result.push_back(measurement);
        }
    }
    return result;
}

} // namespace generic_powermeter
} // namespace module
