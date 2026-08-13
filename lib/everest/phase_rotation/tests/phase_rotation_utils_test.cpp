// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <everest/phase_rotation/phase_rotation_utils.hpp>

#include <generated/types/powermeter.hpp>

#include <gtest/gtest.h>

namespace everest {
namespace phase_rotation {

// Helper to create a powermeter reading with distinct, identifiable per-phase values
static types::powermeter::Powermeter create_test_powermeter() {
    types::powermeter::Powermeter pm;
    pm.timestamp = "2024-01-01T00:00:00Z";
    pm.energy_Wh_import = types::units::Energy{0.0f, 1.0f, 2.0f, 3.0f};
    pm.voltage_V = types::units::Voltage{std::nullopt, 231.0f, 232.0f, 233.0f};
    pm.current_A = types::units::Current{std::nullopt, 11.0f, 12.0f, 13.0f, std::nullopt};
    pm.power_W = types::units::Power{0.0f, 21.0f, 22.0f, 23.0f};
    pm.frequency_Hz = types::units::Frequency{50.0f, 50.0f, 50.0f};
    return pm;
}

/// Identity rotation ("RST") must not change any values
TEST(PhaseRotationUtilsTest, PhaseRotationIdentity) {
    auto pm = create_test_powermeter();
    auto rotated = pm;

    apply_phase_rotation(rotated, "RST");

    EXPECT_EQ(rotated.voltage_V->L1, pm.voltage_V->L1);
    EXPECT_EQ(rotated.voltage_V->L2, pm.voltage_V->L2);
    EXPECT_EQ(rotated.voltage_V->L3, pm.voltage_V->L3);
}

/// "TRS": reported L1 becomes grid L3, reported L2 becomes grid L1, reported L3 becomes grid L2
TEST(PhaseRotationUtilsTest, PhaseRotationTRS) {
    auto pm = create_test_powermeter();

    apply_phase_rotation(pm, "TRS");

    EXPECT_FLOAT_EQ(*pm.voltage_V->L1, 233.0f);
    EXPECT_FLOAT_EQ(*pm.voltage_V->L2, 231.0f);
    EXPECT_FLOAT_EQ(*pm.voltage_V->L3, 232.0f);

    EXPECT_FLOAT_EQ(*pm.current_A->L1, 13.0f);
    EXPECT_FLOAT_EQ(*pm.current_A->L2, 11.0f);
    EXPECT_FLOAT_EQ(*pm.current_A->L3, 12.0f);

    EXPECT_FLOAT_EQ(*pm.power_W->L1, 23.0f);
    EXPECT_FLOAT_EQ(*pm.power_W->L2, 21.0f);
    EXPECT_FLOAT_EQ(*pm.power_W->L3, 22.0f);

    EXPECT_FLOAT_EQ(pm.energy_Wh_import.L1.value(), 3.0f);
    EXPECT_FLOAT_EQ(pm.energy_Wh_import.L2.value(), 1.0f);
    EXPECT_FLOAT_EQ(pm.energy_Wh_import.L3.value(), 2.0f);

    // total is invariant under rotation
    EXPECT_FLOAT_EQ(pm.power_W->total, 0.0f);
    EXPECT_FLOAT_EQ(pm.energy_Wh_import.total, 0.0f);

    // frequency is intentionally left untouched
    EXPECT_FLOAT_EQ(pm.frequency_Hz->L1, 50.0f);
}

/// "STR": reported L1 becomes grid L2, reported L2 becomes grid L3, reported L3 becomes grid L1
TEST(PhaseRotationUtilsTest, PhaseRotationSTR) {
    auto pm = create_test_powermeter();

    apply_phase_rotation(pm, "STR");

    EXPECT_FLOAT_EQ(*pm.voltage_V->L1, 232.0f);
    EXPECT_FLOAT_EQ(*pm.voltage_V->L2, 233.0f);
    EXPECT_FLOAT_EQ(*pm.voltage_V->L3, 231.0f);
}

/// Unsupported/unknown rotation strings must be a no-op
TEST(PhaseRotationUtilsTest, PhaseRotationInvalidValueIsNoOp) {
    auto pm = create_test_powermeter();
    auto rotated = pm;

    apply_phase_rotation(rotated, "garbage");

    EXPECT_EQ(rotated.voltage_V->L1, pm.voltage_V->L1);
    EXPECT_EQ(rotated.voltage_V->L2, pm.voltage_V->L2);
    EXPECT_EQ(rotated.voltage_V->L3, pm.voltage_V->L3);
}

/// Missing optional per-phase fields must not crash and stay unset
TEST(PhaseRotationUtilsTest, PhaseRotationHandlesMissingOptionalFields) {
    types::powermeter::Powermeter pm;
    pm.timestamp = "2024-01-01T00:00:00Z";
    pm.energy_Wh_import = types::units::Energy{0.0f};

    apply_phase_rotation(pm, "TRS");

    EXPECT_FALSE(pm.voltage_V.has_value());
    EXPECT_FALSE(pm.current_A.has_value());
}

} // namespace phase_rotation
} // namespace everest
