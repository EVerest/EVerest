// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2025 Pionix GmbH and Contributors to EVerest

#pragma once

#include <optional>
#include <string>

namespace everest::lib::API::V1_0::types::power_supply_DC {

struct Capabilities {
    bool bidirectional;
    double current_regulation_tolerance_A;
    double peak_current_ripple_A;
    double max_export_voltage_V;
    std::optional<double> nominal_max_export_voltage_V;
    double min_export_voltage_V;
    std::optional<double> nominal_min_export_voltage_V;
    double max_export_current_A;
    std::optional<double> nominal_max_export_current_A;
    double min_export_current_A;
    std::optional<double> nominal_min_export_current_A;
    double max_export_power_W;
    std::optional<double> nominal_max_export_power_W;
    std::optional<double> max_import_voltage_V;
    std::optional<double> nominal_max_import_voltage_V;
    std::optional<double> min_import_voltage_V;
    std::optional<double> nominal_min_import_voltage_V;
    std::optional<double> max_import_current_A;
    std::optional<double> nominal_max_import_current_A;
    std::optional<double> min_import_current_A;
    std::optional<double> nominal_min_import_current_A;
    std::optional<double> max_import_power_W;
    std::optional<double> nominal_max_import_power_W;
    std::optional<double> conversion_efficiency_import;
    std::optional<double> conversion_efficiency_export;
};

enum class Mode {
    Off,
    Export,
    Import,
    Fault,
};

enum class ChargingPhase {
    Other,
    CableCheck,
    PreCharge,
    Charging,
};

struct ModeRequest {
    Mode mode;
    ChargingPhase charging_phase;
};

struct VoltageCurrent {
    double voltage_V; ///< Voltage in V
    double current_A; ///< Current in A
};

enum class ErrorEnum {
    CommunicationFault,
    HardwareFault,
    OverTemperature,
    UnderTemperature,
    UnderVoltageAC,
    OverVoltageAC,
    UnderVoltageDC,
    OverVoltageDC,
    OverCurrentAC,
    OverCurrentDC,
    VendorError,
    VendorWarning
};

struct Error {
    ErrorEnum type;
    std::optional<std::string> sub_type;
    std::optional<std::string> message;
};

} // namespace everest::lib::API::V1_0::types::power_supply_DC
