// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "phase_rotation_utils.hpp"

namespace module {
namespace energy_grid {

namespace {

// Rotates the L1/L2/L3 members of any per-phase measurement struct (Voltage, Current,
// Power, Energy, ReactivePower, and their _signed counterparts). "total"/"DC"/"N" members
// (if present) are left untouched since they are invariant under a phase rotation.
template <typename T> void rotate_l1_l2_l3(T& value, const std::string& phase_rotation) {
    const auto l1 = value.L1;
    const auto l2 = value.L2;
    const auto l3 = value.L3;
    if (phase_rotation == "312") {
        value.L1 = l3;
        value.L2 = l1;
        value.L3 = l2;
    } else if (phase_rotation == "231") {
        value.L1 = l2;
        value.L2 = l3;
        value.L3 = l1;
    }
}

} // namespace

void apply_phase_rotation(types::powermeter::Powermeter& powermeter, const std::string& phase_rotation) {
    if (phase_rotation.empty() || phase_rotation == "123") {
        return;
    }

    rotate_l1_l2_l3(powermeter.energy_Wh_import, phase_rotation);
    if (powermeter.energy_Wh_export) {
        rotate_l1_l2_l3(*powermeter.energy_Wh_export, phase_rotation);
    }
    if (powermeter.power_W) {
        rotate_l1_l2_l3(*powermeter.power_W, phase_rotation);
    }
    if (powermeter.voltage_V) {
        rotate_l1_l2_l3(*powermeter.voltage_V, phase_rotation);
    }
    if (powermeter.VAR) {
        rotate_l1_l2_l3(*powermeter.VAR, phase_rotation);
    }
    if (powermeter.current_A) {
        rotate_l1_l2_l3(*powermeter.current_A, phase_rotation);
    }
    if (powermeter.energy_Wh_import_signed) {
        rotate_l1_l2_l3(*powermeter.energy_Wh_import_signed, phase_rotation);
    }
    if (powermeter.energy_Wh_export_signed) {
        rotate_l1_l2_l3(*powermeter.energy_Wh_export_signed, phase_rotation);
    }
    if (powermeter.power_W_signed) {
        rotate_l1_l2_l3(*powermeter.power_W_signed, phase_rotation);
    }
    if (powermeter.voltage_V_signed) {
        rotate_l1_l2_l3(*powermeter.voltage_V_signed, phase_rotation);
    }
    if (powermeter.VAR_signed) {
        rotate_l1_l2_l3(*powermeter.VAR_signed, phase_rotation);
    }
    if (powermeter.current_A_signed) {
        rotate_l1_l2_l3(*powermeter.current_A_signed, phase_rotation);
    }
}

} // namespace energy_grid
} // namespace module
