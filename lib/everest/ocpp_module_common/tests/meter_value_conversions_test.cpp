// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <vector>

#include <everest/ocpp_module_common/conversions.hpp>

namespace {

using ocpp_module_common::conversions::to_ocpp_meter_value;
using M = ocpp::v2::MeasurandEnum;
using P = ocpp::v2::PhaseEnum;
using Signed = types::units_signed::SignedMeterValue;

constexpr auto TIMESTAMP = "2026-01-01T12:00:00.000Z";

struct Expected {
    M measurand;
    std::optional<P> phase;
    float value;
    std::string unit;
    std::optional<std::string> signed_data;
};

Signed signed_value(const std::string& data) {
    Signed value;
    value.signed_meter_data = data;
    value.signing_method = "ECDSA-secp256r1-SHA256";
    value.encoding_method = "OCMF";
    value.public_key = "key-" + data;
    return value;
}

types::powermeter::Powermeter minimal_reading() {
    types::powermeter::Powermeter power_meter;
    power_meter.timestamp = TIMESTAMP;
    power_meter.energy_Wh_import.total = 1000.0F;
    return power_meter;
}

types::powermeter::Powermeter three_phase_reading() {
    auto power_meter = minimal_reading();
    power_meter.energy_Wh_import = {1000.0F, 300.0F, 330.0F, 370.0F};
    power_meter.energy_Wh_export = types::units::Energy{50.0F, 11.0F, 17.0F, 22.0F};
    power_meter.power_W = types::units::Power{11000.0F, 3600.0F, 3650.0F, 3750.0F};
    power_meter.VAR = types::units::ReactivePower{120.0F, 40.0F, 41.0F, 39.0F};
    power_meter.current_A = types::units::Current{std::nullopt, 16.0F, 15.5F, 15.0F, 0.5F};
    power_meter.voltage_V = types::units::Voltage{std::nullopt, 230.0F, 231.0F, 229.0F};
    power_meter.frequency_Hz = types::units::Frequency{50.0F, 50.01F, 49.99F};
    return power_meter;
}

void add_signed_values(types::powermeter::Powermeter& power_meter) {
    power_meter.energy_Wh_import_signed = types::units_signed::Energy{
        signed_value("import"), signed_value("import.L1"), signed_value("import.L2"), signed_value("import.L3")};
    power_meter.energy_Wh_export_signed = types::units_signed::Energy{
        signed_value("export"), signed_value("export.L1"), signed_value("export.L2"), signed_value("export.L3")};
    power_meter.power_W_signed = types::units_signed::Power{signed_value("power"), signed_value("power.L1"),
                                                            signed_value("power.L2"), signed_value("power.L3")};
    power_meter.VAR_signed = types::units_signed::ReactivePower{signed_value("var"), signed_value("var.L1"),
                                                                signed_value("var.L2"), signed_value("var.L3")};
    power_meter.current_A_signed =
        types::units_signed::Current{std::nullopt, signed_value("current.L1"), signed_value("current.L2"),
                                     signed_value("current.L3"), signed_value("current.N")};
    power_meter.voltage_V_signed = types::units_signed::Voltage{std::nullopt, signed_value("voltage.L1"),
                                                                signed_value("voltage.L2"), signed_value("voltage.L3")};
    power_meter.frequency_Hz_signed = types::units_signed::Frequency{
        signed_value("frequency.L1"), signed_value("frequency.L2"), signed_value("frequency.L3")};
}

std::vector<Expected> three_phase_expected(bool with_signed) {
    auto tag = [with_signed](const std::string& data) {
        return with_signed ? std::optional<std::string>{data} : std::nullopt;
    };
    return {
        {M::Energy_Active_Import_Register, std::nullopt, 1000.0F, "Wh", tag("import")},
        {M::Energy_Active_Import_Register, P::L1, 300.0F, "Wh", tag("import.L1")},
        {M::Energy_Active_Import_Register, P::L2, 330.0F, "Wh", tag("import.L2")},
        {M::Energy_Active_Import_Register, P::L3, 370.0F, "Wh", tag("import.L3")},
        {M::Energy_Active_Export_Register, std::nullopt, 50.0F, "Wh", tag("export")},
        {M::Energy_Active_Export_Register, P::L1, 11.0F, "Wh", tag("export.L1")},
        {M::Energy_Active_Export_Register, P::L2, 17.0F, "Wh", tag("export.L2")},
        {M::Energy_Active_Export_Register, P::L3, 22.0F, "Wh", tag("export.L3")},
        {M::Power_Active_Import, std::nullopt, 11000.0F, "W", tag("power")},
        {M::Power_Active_Import, P::L1, 3600.0F, "W", tag("power.L1")},
        {M::Power_Active_Import, P::L2, 3650.0F, "W", tag("power.L2")},
        {M::Power_Active_Import, P::L3, 3750.0F, "W", tag("power.L3")},
        {M::Power_Reactive_Import, std::nullopt, 120.0F, "var", tag("var")},
        {M::Power_Reactive_Import, P::L1, 40.0F, "var", tag("var.L1")},
        {M::Power_Reactive_Import, P::L2, 41.0F, "var", tag("var.L2")},
        {M::Power_Reactive_Import, P::L3, 39.0F, "var", tag("var.L3")},
        {M::Current_Import, P::L1, 16.0F, "A", tag("current.L1")},
        {M::Current_Import, P::L2, 15.5F, "A", tag("current.L2")},
        {M::Current_Import, P::L3, 15.0F, "A", tag("current.L3")},
        {M::Current_Import, P::N, 0.5F, "A", tag("current.N")},
        {M::Voltage, P::L1_N, 230.0F, "V", tag("voltage.L1")},
        {M::Voltage, P::L2_N, 231.0F, "V", tag("voltage.L2")},
        {M::Voltage, P::L3_N, 229.0F, "V", tag("voltage.L3")},
        {M::Frequency, P::L1, 50.0F, "Hz", tag("frequency.L1")},
        {M::Frequency, P::L2, 50.01F, "Hz", tag("frequency.L2")},
        {M::Frequency, P::L3, 49.99F, "Hz", tag("frequency.L3")},
    };
}

void expect_sampled_values(const ocpp::v2::MeterValue& meter_value, const std::vector<Expected>& expected,
                           ocpp::v2::ReadingContextEnum context) {
    ASSERT_EQ(meter_value.sampledValue.size(), expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i) {
        SCOPED_TRACE("sampled value " + std::to_string(i));
        const auto& sampled_value = meter_value.sampledValue[i];
        EXPECT_EQ(sampled_value.measurand, expected[i].measurand);
        EXPECT_EQ(sampled_value.phase, expected[i].phase);
        EXPECT_FLOAT_EQ(sampled_value.value, expected[i].value);
        ASSERT_TRUE(sampled_value.unitOfMeasure.has_value());
        ASSERT_TRUE(sampled_value.unitOfMeasure->unit.has_value());
        EXPECT_EQ(sampled_value.unitOfMeasure->unit->get(), expected[i].unit);
        EXPECT_FALSE(sampled_value.unitOfMeasure->multiplier.has_value());
        EXPECT_EQ(sampled_value.context, context);
        EXPECT_EQ(sampled_value.location, ocpp::v2::LocationEnum::Outlet);
        if (expected[i].signed_data.has_value()) {
            ASSERT_TRUE(sampled_value.signedMeterValue.has_value());
            EXPECT_EQ(sampled_value.signedMeterValue->signedMeterData.get(), expected[i].signed_data.value());
        } else {
            EXPECT_FALSE(sampled_value.signedMeterValue.has_value());
        }
    }
}

TEST(MeterValueConversions, minimal_reading_has_import_register_only) {
    const auto meter_value =
        to_ocpp_meter_value(minimal_reading(), ocpp::v2::ReadingContextEnum::Sample_Periodic, std::nullopt);

    expect_sampled_values(meter_value, {{M::Energy_Active_Import_Register, std::nullopt, 1000.0F, "Wh", std::nullopt}},
                          ocpp::v2::ReadingContextEnum::Sample_Periodic);
}

TEST(MeterValueConversions, three_phase_reading) {
    const auto meter_value =
        to_ocpp_meter_value(three_phase_reading(), ocpp::v2::ReadingContextEnum::Sample_Periodic, std::nullopt);

    expect_sampled_values(meter_value, three_phase_expected(false), ocpp::v2::ReadingContextEnum::Sample_Periodic);
}

TEST(MeterValueConversions, three_phase_reading_with_signed_values) {
    auto power_meter = three_phase_reading();
    add_signed_values(power_meter);

    const auto meter_value =
        to_ocpp_meter_value(power_meter, ocpp::v2::ReadingContextEnum::Transaction_End, std::nullopt);

    expect_sampled_values(meter_value, three_phase_expected(true), ocpp::v2::ReadingContextEnum::Transaction_End);
}

TEST(MeterValueConversions, reading_context_applies_to_every_sampled_value) {
    for (const auto context :
         {ocpp::v2::ReadingContextEnum::Interruption_Begin, ocpp::v2::ReadingContextEnum::Interruption_End,
          ocpp::v2::ReadingContextEnum::Other, ocpp::v2::ReadingContextEnum::Sample_Clock,
          ocpp::v2::ReadingContextEnum::Sample_Periodic, ocpp::v2::ReadingContextEnum::Transaction_Begin,
          ocpp::v2::ReadingContextEnum::Transaction_End, ocpp::v2::ReadingContextEnum::Trigger}) {
        SCOPED_TRACE(ocpp::v2::conversions::reading_context_enum_to_string(context));
        const auto meter_value = to_ocpp_meter_value(three_phase_reading(), context, std::nullopt);
        expect_sampled_values(meter_value, three_phase_expected(false), context);
    }
}

TEST(MeterValueConversions, dc_reading) {
    auto power_meter = minimal_reading();
    power_meter.power_W = types::units::Power{50000.0F, std::nullopt, std::nullopt, std::nullopt};
    power_meter.current_A = types::units::Current{125.0F, std::nullopt, std::nullopt, std::nullopt, std::nullopt};
    power_meter.voltage_V = types::units::Voltage{400.0F, std::nullopt, std::nullopt, std::nullopt};
    power_meter.current_A_signed = types::units_signed::Current{signed_value("current.DC"), std::nullopt, std::nullopt,
                                                                std::nullopt, std::nullopt};
    power_meter.voltage_V_signed =
        types::units_signed::Voltage{signed_value("voltage.DC"), std::nullopt, std::nullopt, std::nullopt};

    const auto meter_value =
        to_ocpp_meter_value(power_meter, ocpp::v2::ReadingContextEnum::Sample_Periodic, std::nullopt);

    expect_sampled_values(meter_value,
                          {
                              {M::Energy_Active_Import_Register, std::nullopt, 1000.0F, "Wh", std::nullopt},
                              {M::Power_Active_Import, std::nullopt, 50000.0F, "W", std::nullopt},
                              {M::Current_Import, std::nullopt, 125.0F, "A", "current.DC"},
                              {M::Voltage, std::nullopt, 400.0F, "V", "voltage.DC"},
                          },
                          ocpp::v2::ReadingContextEnum::Sample_Periodic);
}

TEST(MeterValueConversions, single_phase_reading) {
    auto power_meter = minimal_reading();
    power_meter.energy_Wh_import = {1000.0F, 1000.0F, std::nullopt, std::nullopt};
    power_meter.current_A = types::units::Current{std::nullopt, 16.0F, std::nullopt, std::nullopt, std::nullopt};
    power_meter.voltage_V = types::units::Voltage{std::nullopt, 230.0F, std::nullopt, std::nullopt};
    power_meter.frequency_Hz = types::units::Frequency{50.0F, std::nullopt, std::nullopt};

    const auto meter_value =
        to_ocpp_meter_value(power_meter, ocpp::v2::ReadingContextEnum::Sample_Periodic, std::nullopt);

    expect_sampled_values(meter_value,
                          {
                              {M::Energy_Active_Import_Register, std::nullopt, 1000.0F, "Wh", std::nullopt},
                              {M::Energy_Active_Import_Register, P::L1, 1000.0F, "Wh", std::nullopt},
                              {M::Current_Import, P::L1, 16.0F, "A", std::nullopt},
                              {M::Voltage, P::L1_N, 230.0F, "V", std::nullopt},
                              {M::Frequency, P::L1, 50.0F, "Hz", std::nullopt},
                          },
                          ocpp::v2::ReadingContextEnum::Sample_Periodic);
}

TEST(MeterValueConversions, empty_optional_groups_add_nothing) {
    auto power_meter = minimal_reading();
    power_meter.current_A = types::units::Current{};
    power_meter.voltage_V = types::units::Voltage{};

    const auto meter_value =
        to_ocpp_meter_value(power_meter, ocpp::v2::ReadingContextEnum::Sample_Periodic, std::nullopt);

    expect_sampled_values(meter_value, {{M::Energy_Active_Import_Register, std::nullopt, 1000.0F, "Wh", std::nullopt}},
                          ocpp::v2::ReadingContextEnum::Sample_Periodic);
}

TEST(MeterValueConversions, global_signed_meter_value_goes_to_import_register_total) {
    auto power_meter = three_phase_reading();

    const auto meter_value =
        to_ocpp_meter_value(power_meter, ocpp::v2::ReadingContextEnum::Transaction_Begin, signed_value("transaction"));

    auto expected = three_phase_expected(false);
    expected[0].signed_data = "transaction";
    expect_sampled_values(meter_value, expected, ocpp::v2::ReadingContextEnum::Transaction_Begin);
}

TEST(MeterValueConversions, signed_import_total_takes_precedence_over_global_signed_meter_value) {
    auto power_meter = minimal_reading();
    power_meter.energy_Wh_import_signed =
        types::units_signed::Energy{signed_value("import"), std::nullopt, std::nullopt, std::nullopt};

    const auto meter_value =
        to_ocpp_meter_value(power_meter, ocpp::v2::ReadingContextEnum::Transaction_End, signed_value("transaction"));

    expect_sampled_values(meter_value, {{M::Energy_Active_Import_Register, std::nullopt, 1000.0F, "Wh", "import"}},
                          ocpp::v2::ReadingContextEnum::Transaction_End);
}

TEST(MeterValueConversions, signed_import_phases_without_total_keep_a_single_total) {
    auto power_meter = minimal_reading();
    power_meter.energy_Wh_import = {1000.0F, 1000.0F, std::nullopt, std::nullopt};
    power_meter.energy_Wh_import_signed =
        types::units_signed::Energy{std::nullopt, signed_value("import.L1"), std::nullopt, std::nullopt};

    const auto with_global =
        to_ocpp_meter_value(power_meter, ocpp::v2::ReadingContextEnum::Transaction_End, signed_value("transaction"));
    expect_sampled_values(with_global,
                          {
                              {M::Energy_Active_Import_Register, std::nullopt, 1000.0F, "Wh", "transaction"},
                              {M::Energy_Active_Import_Register, P::L1, 1000.0F, "Wh", "import.L1"},
                          },
                          ocpp::v2::ReadingContextEnum::Transaction_End);

    const auto without_global =
        to_ocpp_meter_value(power_meter, ocpp::v2::ReadingContextEnum::Transaction_End, std::nullopt);
    expect_sampled_values(without_global,
                          {
                              {M::Energy_Active_Import_Register, std::nullopt, 1000.0F, "Wh", std::nullopt},
                              {M::Energy_Active_Import_Register, P::L1, 1000.0F, "Wh", "import.L1"},
                          },
                          ocpp::v2::ReadingContextEnum::Transaction_End);
}

TEST(MeterValueConversions, signed_meter_value_fields) {
    auto power_meter = minimal_reading();
    power_meter.energy_Wh_import_signed =
        types::units_signed::Energy{signed_value("import"), std::nullopt, std::nullopt, std::nullopt};

    const auto meter_value =
        to_ocpp_meter_value(power_meter, ocpp::v2::ReadingContextEnum::Transaction_End, std::nullopt);

    ASSERT_EQ(meter_value.sampledValue.size(), 1);
    ASSERT_TRUE(meter_value.sampledValue[0].signedMeterValue.has_value());
    const auto& signed_meter_value = meter_value.sampledValue[0].signedMeterValue.value();
    EXPECT_EQ(signed_meter_value.signedMeterData.get(), "import");
    ASSERT_TRUE(signed_meter_value.signingMethod.has_value());
    EXPECT_EQ(signed_meter_value.signingMethod->get(), "ECDSA-secp256r1-SHA256");
    EXPECT_EQ(signed_meter_value.encodingMethod.get(), "OCMF");
    ASSERT_TRUE(signed_meter_value.publicKey.has_value());
    EXPECT_EQ(signed_meter_value.publicKey->get(), "key-import");
}

TEST(MeterValueConversions, signed_meter_value_without_public_key) {
    auto transaction = signed_value("transaction");
    transaction.public_key.reset();

    const auto meter_value =
        to_ocpp_meter_value(minimal_reading(), ocpp::v2::ReadingContextEnum::Transaction_End, transaction);

    ASSERT_EQ(meter_value.sampledValue.size(), 1);
    ASSERT_TRUE(meter_value.sampledValue[0].signedMeterValue.has_value());
    ASSERT_TRUE(meter_value.sampledValue[0].signedMeterValue->publicKey.has_value());
    EXPECT_EQ(meter_value.sampledValue[0].signedMeterValue->publicKey->get(), "");
}

TEST(MeterValueConversions, timestamp_from_reading) {
    const auto meter_value =
        to_ocpp_meter_value(minimal_reading(), ocpp::v2::ReadingContextEnum::Sample_Periodic, std::nullopt);

    EXPECT_EQ(meter_value.timestamp, ocpp::DateTime(TIMESTAMP));
}

TEST(MeterValueConversions, unparsable_timestamp_falls_back_to_now) {
    auto power_meter = minimal_reading();
    power_meter.timestamp = "not a timestamp";

    const ocpp::DateTime before;
    const auto meter_value =
        to_ocpp_meter_value(power_meter, ocpp::v2::ReadingContextEnum::Sample_Periodic, std::nullopt);
    const ocpp::DateTime after;

    EXPECT_TRUE(before <= meter_value.timestamp);
    EXPECT_TRUE(meter_value.timestamp <= after);
}

} // namespace
