// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <iostream>

#include <everest/helpers/helpers.hpp>
#include <everest/helpers/phase_rotation.hpp>

#include <generated/types/powermeter.hpp>

using namespace everest::helpers;
using ::testing::StartsWith;

TEST(HelpersTest, redact_token) {
    std::string token = "secret token";

    auto redacted = redact(token);

    EXPECT_THAT(redacted, StartsWith("[redacted] hash: "));
}

TEST(HelpersTest, get_uuid) {
    auto uuid1 = get_uuid();
    auto uuid2 = get_uuid();

    EXPECT_GT(uuid1.length(), 0);
    EXPECT_GT(uuid2.length(), 0);
    EXPECT_EQ(uuid1.length(), uuid2.length());
    EXPECT_NE(uuid1, uuid2);
}

TEST(HelpersTest, get_base64_uuid) {
    auto id1 = get_base64_uuid();
    auto id2 = get_base64_uuid();

    EXPECT_EQ(id1.length(), 22);
    EXPECT_EQ(id2.length(), 22);
    EXPECT_NE(id1, id2);
}

TEST(HelpersTest, get_base64_id) {
    auto id1 = get_base64_id();
    auto id2 = get_base64_id();

    EXPECT_EQ(id1.length(), 16);
    EXPECT_EQ(id2.length(), 16);
    EXPECT_NE(id1, id2);
}

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

/// "RST" is the identity; an empty or unsupported notation must be treated the same way, i.e. no rotation
TEST(HelpersTest, phase_rotation_no_op_values) {
    const auto pm = create_test_powermeter();

    for (const auto* phase_rotation : {"RST", "", "garbage"}) {
        SCOPED_TRACE(phase_rotation);

        const auto rotated = apply_phase_rotation(pm, phase_rotation_from_string(phase_rotation));

        EXPECT_EQ(rotated.voltage_V->L1, pm.voltage_V->L1);
        EXPECT_EQ(rotated.voltage_V->L2, pm.voltage_V->L2);
        EXPECT_EQ(rotated.voltage_V->L3, pm.voltage_V->L3);
    }
}

/// "TRS": reported L1 is grid L3, reported L2 is grid L1, reported L3 is grid L2
TEST(HelpersTest, phase_rotation_TRS) {
    const auto rotated = apply_phase_rotation(create_test_powermeter(), PhaseRotation::TRS);

    EXPECT_FLOAT_EQ(*rotated.voltage_V->L1, 232.0f);
    EXPECT_FLOAT_EQ(*rotated.voltage_V->L2, 233.0f);
    EXPECT_FLOAT_EQ(*rotated.voltage_V->L3, 231.0f);

    EXPECT_FLOAT_EQ(*rotated.current_A->L1, 12.0f);
    EXPECT_FLOAT_EQ(*rotated.current_A->L2, 13.0f);
    EXPECT_FLOAT_EQ(*rotated.current_A->L3, 11.0f);

    EXPECT_FLOAT_EQ(*rotated.power_W->L1, 22.0f);
    EXPECT_FLOAT_EQ(*rotated.power_W->L2, 23.0f);
    EXPECT_FLOAT_EQ(*rotated.power_W->L3, 21.0f);

    EXPECT_FLOAT_EQ(*rotated.energy_Wh_import.L1, 2.0f);
    EXPECT_FLOAT_EQ(*rotated.energy_Wh_import.L2, 3.0f);
    EXPECT_FLOAT_EQ(*rotated.energy_Wh_import.L3, 1.0f);

    // total is invariant under rotation
    EXPECT_FLOAT_EQ(rotated.power_W->total, 0.0f);
    EXPECT_FLOAT_EQ(rotated.energy_Wh_import.total, 0.0f);

    // frequency is intentionally left untouched
    EXPECT_FLOAT_EQ(rotated.frequency_Hz->L1, 50.0f);
}

/// "STR": reported L1 is grid L2, reported L2 is grid L3, reported L3 is grid L1
TEST(HelpersTest, phase_rotation_STR) {
    const auto rotated = apply_phase_rotation(create_test_powermeter(), PhaseRotation::STR);

    EXPECT_FLOAT_EQ(*rotated.voltage_V->L1, 233.0f);
    EXPECT_FLOAT_EQ(*rotated.voltage_V->L2, 231.0f);
    EXPECT_FLOAT_EQ(*rotated.voltage_V->L3, 232.0f);
}

/// Missing optional per-phase fields must not crash and stay unset
TEST(HelpersTest, phase_rotation_handles_missing_optional_fields) {
    types::powermeter::Powermeter pm;
    pm.timestamp = "2024-01-01T00:00:00Z";
    pm.energy_Wh_import = types::units::Energy{0.0f};

    const auto rotated = apply_phase_rotation(pm, PhaseRotation::TRS);

    EXPECT_FALSE(rotated.voltage_V.has_value());
    EXPECT_FALSE(rotated.current_A.has_value());
}

namespace {

types::powermeter::Powermeter with_current(float l1, float l2, float l3) {
    types::powermeter::Powermeter pm;
    pm.timestamp = "2024-01-01T00:00:00Z";
    pm.energy_Wh_import = types::units::Energy{0.0f};
    types::units::Current current;
    current.L1 = l1;
    current.L2 = l2;
    current.L3 = l3;
    pm.current_A = current;
    return pm;
}

} // namespace

TEST(HelpersTest, same_loaded_phases_compares_the_phases_above_the_noise_floor) {
    EXPECT_EQ(same_loaded_phases(with_current(16.0f, 0.2f, 0.0f), with_current(15.5f, 0.0f, 0.4f), 1.0f), true);
    EXPECT_EQ(same_loaded_phases(with_current(16.0f, 0.0f, 0.0f), with_current(0.0f, 16.0f, 0.0f), 1.0f), false);
    EXPECT_EQ(same_loaded_phases(with_current(16.0f, 0.0f, 0.0f),
                                 apply_phase_rotation(with_current(16.0f, 0.0f, 0.0f), PhaseRotation::STR), 1.0f),
              false);
}

TEST(HelpersTest, same_loaded_phases_cannot_tell_idle_or_three_phase_loads) {
    EXPECT_FALSE(same_loaded_phases(with_current(0.0f, 0.0f, 0.0f), with_current(0.0f, 16.0f, 0.0f), 1.0f));
    EXPECT_FALSE(same_loaded_phases(with_current(16.0f, 16.0f, 16.0f), with_current(16.0f, 16.0f, 16.0f), 1.0f));
    types::powermeter::Powermeter without_current = with_current(0.0f, 0.0f, 0.0f);
    without_current.current_A.reset();
    EXPECT_FALSE(same_loaded_phases(without_current, with_current(16.0f, 0.0f, 0.0f), 1.0f));
}
