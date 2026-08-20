// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <iostream>

#include <everest/helpers/helpers.hpp>

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

        const auto rotated = apply_phase_rotation(pm, phase_rotation);

        EXPECT_EQ(rotated.voltage_V->L1, pm.voltage_V->L1);
        EXPECT_EQ(rotated.voltage_V->L2, pm.voltage_V->L2);
        EXPECT_EQ(rotated.voltage_V->L3, pm.voltage_V->L3);
    }
}

/// "TRS": reported L1 is grid L3, reported L2 is grid L1, reported L3 is grid L2
TEST(HelpersTest, phase_rotation_TRS) {
    const auto rotated = apply_phase_rotation(create_test_powermeter(), "TRS");

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
    const auto rotated = apply_phase_rotation(create_test_powermeter(), "STR");

    EXPECT_FLOAT_EQ(*rotated.voltage_V->L1, 233.0f);
    EXPECT_FLOAT_EQ(*rotated.voltage_V->L2, 231.0f);
    EXPECT_FLOAT_EQ(*rotated.voltage_V->L3, 232.0f);
}

/// Missing optional per-phase fields must not crash and stay unset
TEST(HelpersTest, phase_rotation_handles_missing_optional_fields) {
    types::powermeter::Powermeter pm;
    pm.timestamp = "2024-01-01T00:00:00Z";
    pm.energy_Wh_import = types::units::Energy{0.0f};

    const auto rotated = apply_phase_rotation(pm, "TRS");

    EXPECT_FALSE(rotated.voltage_V.has_value());
    EXPECT_FALSE(rotated.current_A.has_value());
}

// Helper to create a signed meter value tagged with the phase it was originally measured on
static types::units_signed::SignedMeterValue signed_value(const std::string& phase) {
    types::units_signed::SignedMeterValue value;
    value.signed_meter_data = phase;
    value.signing_method = "ECDSA-secp256r1-SHA256";
    value.encoding_method = "Base64";
    return value;
}

// Asserts that a per-phase measurement holding 1/2/3 now holds 2/3/1, i.e. was corrected for "TRS" wiring
template <typename T> static void expect_rotated(const std::string& member, const T& measurement) {
    SCOPED_TRACE(member);
    EXPECT_FLOAT_EQ(*measurement.L1, 2.0f);
    EXPECT_FLOAT_EQ(*measurement.L2, 3.0f);
    EXPECT_FLOAT_EQ(*measurement.L3, 1.0f);
}

// Same, for the signed measurements whose per-phase values are tagged "L1"/"L2"/"L3"
template <typename T> static void expect_rotated_signed(const std::string& member, const T& measurement) {
    SCOPED_TRACE(member);
    EXPECT_EQ(measurement.L1->signed_meter_data, "L2");
    EXPECT_EQ(measurement.L2->signed_meter_data, "L3");
    EXPECT_EQ(measurement.L3->signed_meter_data, "L1");
}

/// Every per-phase member of the Powermeter must be rotated, not only the ones exercised above.
/// This guards the member list inside apply_phase_rotation against a field being forgotten, which
/// matters because types::powermeter::Powermeter is autogenerated and gains members over time.
TEST(HelpersTest, phase_rotation_covers_every_per_phase_member) {
    types::powermeter::Powermeter pm;
    pm.timestamp = "2024-01-01T00:00:00Z";

    pm.energy_Wh_import = types::units::Energy{0.0f, 1.0f, 2.0f, 3.0f};
    pm.energy_Wh_export = types::units::Energy{0.0f, 1.0f, 2.0f, 3.0f};
    pm.power_W = types::units::Power{0.0f, 1.0f, 2.0f, 3.0f};
    pm.voltage_V = types::units::Voltage{std::nullopt, 1.0f, 2.0f, 3.0f};
    pm.VAR = types::units::ReactivePower{0.0f, 1.0f, 2.0f, 3.0f};
    pm.current_A = types::units::Current{std::nullopt, 1.0f, 2.0f, 3.0f, std::nullopt};

    pm.energy_Wh_import_signed =
        types::units_signed::Energy{std::nullopt, signed_value("L1"), signed_value("L2"), signed_value("L3")};
    pm.energy_Wh_export_signed =
        types::units_signed::Energy{std::nullopt, signed_value("L1"), signed_value("L2"), signed_value("L3")};
    pm.power_W_signed =
        types::units_signed::Power{std::nullopt, signed_value("L1"), signed_value("L2"), signed_value("L3")};
    pm.voltage_V_signed =
        types::units_signed::Voltage{std::nullopt, signed_value("L1"), signed_value("L2"), signed_value("L3")};
    pm.VAR_signed =
        types::units_signed::ReactivePower{std::nullopt, signed_value("L1"), signed_value("L2"), signed_value("L3")};
    pm.current_A_signed = types::units_signed::Current{std::nullopt, signed_value("L1"), signed_value("L2"),
                                                       signed_value("L3"), std::nullopt};

    const auto rotated = apply_phase_rotation(pm, "TRS");

    expect_rotated("energy_Wh_import", rotated.energy_Wh_import);
    expect_rotated("energy_Wh_export", *rotated.energy_Wh_export);
    expect_rotated("power_W", *rotated.power_W);
    expect_rotated("voltage_V", *rotated.voltage_V);
    expect_rotated("VAR", *rotated.VAR);
    expect_rotated("current_A", *rotated.current_A);

    expect_rotated_signed("energy_Wh_import_signed", *rotated.energy_Wh_import_signed);
    expect_rotated_signed("energy_Wh_export_signed", *rotated.energy_Wh_export_signed);
    expect_rotated_signed("power_W_signed", *rotated.power_W_signed);
    expect_rotated_signed("voltage_V_signed", *rotated.voltage_V_signed);
    expect_rotated_signed("VAR_signed", *rotated.VAR_signed);
    expect_rotated_signed("current_A_signed", *rotated.current_A_signed);
}
