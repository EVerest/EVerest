// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <everest/helpers/phase_rotation.hpp>

namespace everest::helpers {

namespace {

/// Remaps the L1/L2/L3 members of a single per-phase measurement according to \p rotation .
/// Members that are invariant under a phase rotation (total, DC, N) are left untouched.
template <typename T> void rotate_phases(T& measurement, PhaseRotation rotation) {
    const auto l1 = measurement.L1;
    const auto l2 = measurement.L2;
    const auto l3 = measurement.L3;
    switch (rotation) {
    case PhaseRotation::TRS:
        measurement.L1 = l2;
        measurement.L2 = l3;
        measurement.L3 = l1;
        break;
    case PhaseRotation::STR:
        measurement.L1 = l3;
        measurement.L2 = l1;
        measurement.L3 = l2;
        break;
    case PhaseRotation::RST:
        break;
    }
}

template <typename T> void rotate_phases(std::optional<T>& measurement, PhaseRotation rotation) {
    if (measurement.has_value()) {
        rotate_phases(*measurement, rotation);
    }
}

template <typename... T> void rotate_multiple_phases(PhaseRotation rotation, T&... measurements) {
    ((rotate_phases(measurements, rotation)), ...);
}

} // namespace

/// \returns the PhaseRotation for the given OCPP-style notation, RST for any unknown value
PhaseRotation phase_rotation_from_string(const std::string& phase_rotation) {
    if (phase_rotation == "TRS") {
        return PhaseRotation::TRS;
    }
    if (phase_rotation == "STR") {
        return PhaseRotation::STR;
    }
    return PhaseRotation::RST;
}

types::powermeter::Powermeter apply_phase_rotation(types::powermeter::Powermeter powermeter,
                                                   PhaseRotation phase_rotation) {

    if (phase_rotation == PhaseRotation::RST) {
        return powermeter;
    }

    rotate_multiple_phases(phase_rotation, // clang-format off
        powermeter.energy_Wh_import,
        powermeter.energy_Wh_export,
        powermeter.power_W,
        powermeter.voltage_V,
        powermeter.VAR,
        powermeter.current_A,
        powermeter.energy_Wh_import_signed,
        powermeter.energy_Wh_export_signed,
        powermeter.power_W_signed,
        powermeter.voltage_V_signed,
        powermeter.VAR_signed,
        powermeter.current_A_signed // clang-format on
    );

    return powermeter;
}

} // namespace everest::helpers
