// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <iso15118/ev/config.hpp>
#include <iso15118/ev/config_validation.hpp>
#include <iso15118/sae_modes.hpp>

#include "sae_profile_config.hpp"

namespace {

using iso15118::ev::SaeInverterProfile;
using iso15118::sae::DerBitMapFunctions;
using iso15118::sae::sae_function_bit;
namespace sae_types = iso15118::message_20::datatypes::sae;

using P = SaeInverterProfile;

const std::vector<std::pair<std::string, float P::*>> FLOAT_FIELDS{
    {"max_apparent_power_charging_var_absorption_va", &P::max_apparent_power_charging_var_absorption_va},
    {"max_apparent_power_charging_var_injection_va", &P::max_apparent_power_charging_var_injection_va},
    {"max_apparent_power_discharging_var_absorption_va", &P::max_apparent_power_discharging_var_absorption_va},
    {"max_apparent_power_discharging_var_injection_va", &P::max_apparent_power_discharging_var_injection_va},
    {"max_var_absorption_charging_var", &P::max_var_absorption_charging_var},
    {"max_var_injection_charging_var", &P::max_var_injection_charging_var},
    {"max_var_absorption_discharging_var", &P::max_var_absorption_discharging_var},
    {"max_var_injection_discharging_var", &P::max_var_injection_discharging_var},
    {"reactive_susceptance_s", &P::reactive_susceptance_s},
    {"over_excited_power_factor", &P::over_excited_power_factor},
    {"over_excited_discharge_power_w", &P::over_excited_discharge_power_w},
    {"under_excited_power_factor", &P::under_excited_power_factor},
    {"under_excited_discharge_power_w", &P::under_excited_discharge_power_w},
    {"nominal_voltage_v", &P::nominal_voltage_v},
    {"maximum_voltage_v", &P::maximum_voltage_v},
    {"minimum_voltage_v", &P::minimum_voltage_v},
    {"nominal_voltage_offset_v", &P::nominal_voltage_offset_v},
    {"nominal_frequency_hz", &P::nominal_frequency_hz},
};

const std::vector<std::pair<std::string, std::uint32_t P::*>> UINT32_FIELDS{
    {"useable_watt_hours", &P::useable_watt_hours},
    {"minimum_charging_duration_s", &P::minimum_charging_duration_s},
    {"duration_maximum_charge_rate_s", &P::duration_maximum_charge_rate_s},
    {"duration_maximum_discharge_rate_s", &P::duration_maximum_discharge_rate_s},
};

class SaeProfileConfigTest : public ::testing::Test {
protected:
    void SetUp() override {
        auto pattern = (std::filesystem::temp_directory_path() / "sae_profile_XXXXXX").string();
        ASSERT_NE(mkdtemp(pattern.data()), nullptr);
        dir = pattern;
    }

    void TearDown() override {
        std::filesystem::remove_all(dir);
    }

    std::string write_profile(const std::string& content) {
        const auto path = dir / "profile.json";
        std::ofstream file(path);
        file << content;
        return path.string();
    }

    std::optional<SaeInverterProfile> parse(const std::string& content) {
        return module::parse_sae_inverter_profile(write_profile(content), error);
    }

    // Expects a failure whose error names the key.
    void expect_rejected(const std::string& key, const std::string& json_value) {
        SCOPED_TRACE(key + " = " + json_value);
        EXPECT_FALSE(parse("{\"" + key + "\": " + json_value + "}").has_value());
        EXPECT_NE(error.find(key), std::string::npos) << error;
    }

    std::filesystem::path dir;
    std::string error;
};

TEST_F(SaeProfileConfigTest, EmptyPathReturnsDefaultsWithoutError) {
    const auto profile = module::parse_sae_inverter_profile("", error);

    ASSERT_TRUE(profile.has_value());
    EXPECT_TRUE(error.empty());

    const SaeInverterProfile defaults{};
    EXPECT_EQ(profile->inverter_manufacturer, defaults.inverter_manufacturer);
    EXPECT_EQ(profile->inverter_model, defaults.inverter_model);
    EXPECT_EQ(profile->supported_modes, defaults.supported_modes);
    EXPECT_FLOAT_EQ(profile->nominal_voltage_v, defaults.nominal_voltage_v);
    EXPECT_EQ(profile->operational_state, defaults.operational_state);
}

TEST_F(SaeProfileConfigTest, MissingPathOrDirectoryFailsNamingThePath) {
    for (const auto& path : {(dir / "missing.json").string(), dir.string()}) {
        EXPECT_FALSE(module::parse_sae_inverter_profile(path, error).has_value());
        EXPECT_NE(error.find(path), std::string::npos) << error;
    }
}

TEST_F(SaeProfileConfigTest, MalformedJsonFails) {
    EXPECT_FALSE(parse("{ not json").has_value());
    EXPECT_NE(error.find("not valid JSON"), std::string::npos) << error;
}

TEST_F(SaeProfileConfigTest, TopLevelArrayFails) {
    EXPECT_FALSE(parse("[1, 2, 3]").has_value());
    EXPECT_NE(error.find("JSON object"), std::string::npos) << error;
}

TEST_F(SaeProfileConfigTest, EmptyObjectKeepsDefaults) {
    const auto profile = parse("{}");

    ASSERT_TRUE(profile.has_value()) << error;
    EXPECT_EQ(profile->supported_modes, SaeInverterProfile{}.supported_modes);
    EXPECT_FALSE(profile->inverter_hw_version.has_value());
}

TEST_F(SaeProfileConfigTest, UnknownOrRepeatedKeyFailsNamingTheKey) {
    expect_rejected("inverter_frobnication", "3");
    expect_rejected("useable_watt_hours", R"(1, "useable_watt_hours": 2)");
}

TEST_F(SaeProfileConfigTest, WrongValueTypeFailsNamingTheKey) {
    expect_rejected("nominal_voltage_v", "\"230\"");
    expect_rejected("j3072_certified", "1");
    expect_rejected("inverter_model", "7");
    expect_rejected("inverter_hw_version", "null");
    expect_rejected("useable_watt_hours", "-1");
    expect_rejected("useable_watt_hours", "1.5");
    expect_rejected("supported_modes", "\"ChargeFunction\"");
    expect_rejected("supported_modes", "[\"ChargeFunction\", 1]");
    expect_rejected("operational_state", "0");
}

TEST_F(SaeProfileConfigTest, FloatOutsideFloatRangeFailsNamingTheKey) {
    expect_rejected("nominal_voltage_v", "1e39");
    expect_rejected("nominal_voltage_v", "-1e39");
}

TEST_F(SaeProfileConfigTest, Uint32FieldsAcceptTheirBoundsAndRejectBeyond) {
    for (const auto& [key, field] : UINT32_FIELDS) {
        SCOPED_TRACE(key);

        auto profile = parse("{\"" + key + "\": 0}");
        ASSERT_TRUE(profile.has_value()) << error;
        EXPECT_EQ((*profile).*field, 0u);

        profile = parse("{\"" + key + "\": 4294967295}");
        ASSERT_TRUE(profile.has_value()) << error;
        EXPECT_EQ((*profile).*field, 4294967295u);

        // A silent narrowing would wrap 4294967296 to 0.
        expect_rejected(key, "4294967296");
        EXPECT_NE(error.find("4294967295"), std::string::npos) << error;
    }
}

TEST_F(SaeProfileConfigTest, CertificationDateAcceptsTheFullUint64Range) {
    auto profile = parse(R"({"j3072_certification_date": 5000000000})");
    ASSERT_TRUE(profile.has_value()) << error;
    EXPECT_EQ(profile->j3072_certification_date, 5000000000ull);

    profile = parse(R"({"j3072_certification_date": 18446744073709551615})");
    ASSERT_TRUE(profile.has_value()) << error;
    EXPECT_EQ(profile->j3072_certification_date, 18446744073709551615ull);

    expect_rejected("j3072_certification_date", "18446744073709551616");
    expect_rejected("j3072_certification_date", "-1");
}

TEST_F(SaeProfileConfigTest, PartialObjectOverridesOnlyNamedKeys) {
    const auto profile = parse(R"({
        "inverter_model": "TestModel",
        "nominal_frequency_hz": 60.0,
        "inverter_hw_version": "hw-1",
        "j3072_certified": true
    })");

    ASSERT_TRUE(profile.has_value()) << error;
    EXPECT_TRUE(error.empty());
    EXPECT_EQ(profile->inverter_model, "TestModel");
    EXPECT_FLOAT_EQ(profile->nominal_frequency_hz, 60.0f);
    ASSERT_TRUE(profile->inverter_hw_version.has_value());
    EXPECT_EQ(profile->inverter_hw_version.value(), "hw-1");
    EXPECT_TRUE(profile->j3072_certified);

    const SaeInverterProfile defaults{};
    EXPECT_EQ(profile->inverter_manufacturer, defaults.inverter_manufacturer);
    EXPECT_EQ(profile->inverter_serial_number, defaults.inverter_serial_number);
    EXPECT_FLOAT_EQ(profile->nominal_voltage_v, defaults.nominal_voltage_v);
    EXPECT_EQ(profile->supported_modes, defaults.supported_modes);
    EXPECT_EQ(profile->useable_watt_hours, defaults.useable_watt_hours);
}

TEST_F(SaeProfileConfigTest, EveryStringFieldOverridesItsDefault) {
    const auto profile = parse(R"({
        "inverter_sw_version": "1.2.3",
        "inverter_manufacturer": "Acme",
        "inverter_model": "M-1",
        "inverter_serial_number": "SN-9"
    })");

    ASSERT_TRUE(profile.has_value()) << error;
    EXPECT_EQ(profile->inverter_sw_version, "1.2.3");
    EXPECT_EQ(profile->inverter_manufacturer, "Acme");
    EXPECT_EQ(profile->inverter_model, "M-1");
    EXPECT_EQ(profile->inverter_serial_number, "SN-9");
}

TEST_F(SaeProfileConfigTest, EveryNumericFieldOverridesItsDefault) {
    const P defaults{};

    for (const auto& [key, field] : FLOAT_FIELDS) {
        SCOPED_TRACE(key);
        // 1234.5 is no field's default; the integer form checks integers reach float fields.
        for (const auto& [literal, expected] : {std::pair{"1234.5", 1234.5f}, std::pair{"1234", 1234.0f}}) {
            ASSERT_NE(defaults.*field, expected);
            const auto profile = parse("{\"" + key + "\": " + literal + "}");
            ASSERT_TRUE(profile.has_value()) << error;
            EXPECT_FLOAT_EQ((*profile).*field, expected);
        }
    }
    for (const auto& [key, field] : UINT32_FIELDS) {
        SCOPED_TRACE(key);
        ASSERT_NE(defaults.*field, 1234u);
        const auto profile = parse("{\"" + key + "\": 1234}");
        ASSERT_TRUE(profile.has_value()) << error;
        EXPECT_EQ((*profile).*field, 1234u);
    }

    ASSERT_NE(defaults.j3072_certification_date, 1767225600u);
    const auto profile = parse(R"({"j3072_certification_date": 1767225600})");
    ASSERT_TRUE(profile.has_value()) << error;
    EXPECT_EQ(profile->j3072_certification_date, 1767225600u);
}

TEST_F(SaeProfileConfigTest, SupportedModesBuildTheExpectedBitmask) {
    const auto profile = parse(R"({"supported_modes": ["ChargeFunction", "DischargeFunction", "VoltVarFunction"]})");

    ASSERT_TRUE(profile.has_value()) << error;
    EXPECT_TRUE(error.empty());
    const auto expected = sae_function_bit(DerBitMapFunctions::ChargeFunction) |
                          sae_function_bit(DerBitMapFunctions::DischargeFunction) |
                          sae_function_bit(DerBitMapFunctions::VoltVarFunction);
    EXPECT_EQ(profile->supported_modes, expected);
}

TEST_F(SaeProfileConfigTest, EmptyModesParseToAnEmptyBitmap) {
    const auto profile = parse(R"({"supported_modes": []})");

    ASSERT_TRUE(profile.has_value()) << error;
    EXPECT_EQ(profile->supported_modes, 0u);
}

TEST_F(SaeProfileConfigTest, UnknownModeNameFailsNamingIt) {
    EXPECT_FALSE(parse(R"({"supported_modes": ["ChargeFunction", "flux capacitor"]})").has_value());
    EXPECT_NE(error.find("flux capacitor"), std::string::npos) << error;
    EXPECT_NE(error.find("supported_modes"), std::string::npos) << error;
}

TEST_F(SaeProfileConfigTest, ModeNamesAreCaseSensitive) {
    EXPECT_FALSE(parse(R"({"supported_modes": ["chargeFunction"]})").has_value());
    EXPECT_NE(error.find("chargeFunction"), std::string::npos) << error;
}

TEST_F(SaeProfileConfigTest, EveryNamedModeSetsItsBit) {
    std::string modes = R"({"supported_modes": [)";
    std::uint32_t expected = 0;
    iso15118::sae::for_each_sae_function([&modes, &expected](DerBitMapFunctions function) {
        if (expected != 0) {
            modes += ", ";
        }
        modes += '"';
        modes += iso15118::sae::sae_function_name(function);
        modes += '"';
        expected |= sae_function_bit(function);
    });
    modes += "]}";

    const auto profile = parse(modes);

    ASSERT_TRUE(profile.has_value()) << error;
    EXPECT_EQ(profile->supported_modes, expected);
    EXPECT_EQ(profile->supported_modes & ~iso15118::sae::SAE_MODE_BITMAP_MASK, 0u);
}

TEST_F(SaeProfileConfigTest, ErrorIsClearedOnEachCall) {
    EXPECT_FALSE(parse("{ not json").has_value());
    ASSERT_FALSE(error.empty());

    ASSERT_TRUE(parse(R"({"supported_modes": ["ChargeFunction", "DischargeFunction"]})").has_value());
    EXPECT_TRUE(error.empty());

    EXPECT_FALSE(parse(R"({"bogus": 1})").has_value());
    ASSERT_FALSE(error.empty());

    ASSERT_TRUE(module::parse_sae_inverter_profile("", error).has_value());
    EXPECT_TRUE(error.empty());
}

TEST_F(SaeProfileConfigTest, Ieee1547NormalCategoryAcceptsLegalStringsOnly) {
    for (const auto& [name, value] : {std::pair{"CategoryA", sae_types::IEEE1547NormalCategory::CategoryA},
                                      std::pair{"CategoryB", sae_types::IEEE1547NormalCategory::CategoryB}}) {
        const auto profile = parse(std::string(R"({"ieee1547_normal_category": ")") + name + "\"}");
        ASSERT_TRUE(profile.has_value()) << error;
        EXPECT_EQ(profile->ieee1547_normal_category, value);
    }
    expect_rejected("ieee1547_normal_category", "\"CategoryC\"");
    expect_rejected("ieee1547_normal_category", "\"categorya\"");
}

TEST_F(SaeProfileConfigTest, Ieee1547AbnormalCategoryAcceptsLegalStringsOnly) {
    for (const auto& [name, value] : {std::pair{"CategoryI", sae_types::IEEE1547AbnormalCategory::CategoryI},
                                      std::pair{"CategoryII", sae_types::IEEE1547AbnormalCategory::CategoryII},
                                      std::pair{"CategoryIII", sae_types::IEEE1547AbnormalCategory::CategoryIII}}) {
        const auto profile = parse(std::string(R"({"ieee1547_abnormal_category": ")") + name + "\"}");
        ASSERT_TRUE(profile.has_value()) << error;
        EXPECT_EQ(profile->ieee1547_abnormal_category, value);
    }
    expect_rejected("ieee1547_abnormal_category", "\"CategoryIV\"");
}

TEST_F(SaeProfileConfigTest, OperationalStateAcceptsLegalStringsOnly) {
    for (const auto& [name, value] :
         {std::pair{"On", sae_types::DEROperationalState::On}, std::pair{"Off", sae_types::DEROperationalState::Off}}) {
        const auto profile = parse(std::string(R"({"operational_state": ")") + name + "\"}");
        ASSERT_TRUE(profile.has_value()) << error;
        EXPECT_EQ(profile->operational_state, value);
    }
    expect_rejected("operational_state", "\"Standby\"");
}

TEST_F(SaeProfileConfigTest, ConnectionStatusAcceptsLegalStringsOnly) {
    for (const auto& [name, value] : {std::pair{"Connected", sae_types::DERConnectionStatus::Connected},
                                      std::pair{"Disconnected", sae_types::DERConnectionStatus::Disconnected}}) {
        const auto profile = parse(std::string(R"({"connection_status": ")") + name + "\"}");
        ASSERT_TRUE(profile.has_value()) << error;
        EXPECT_EQ(profile->connection_status, value);
    }
    expect_rejected("connection_status", "\"Pending\"");
    EXPECT_NE(error.find("expected one of Disconnected, Connected"), std::string::npos) << error;
}

iso15118::ev::EvConfig sae_config(const SaeInverterProfile& profile) {
    iso15118::ev::EvConfig config{};
    config.interface_name = "lo";
    config.evcc_id = "02:00:00:00:00:01";
    config.energy_service = iso15118::message_20::datatypes::ServiceCategory::AC_DER_SAE;
    config.sae_profile = profile;
    return config;
}

TEST_F(SaeProfileConfigTest, FullProfilePassesValidateConfig) {
    const auto profile = parse(R"({
        "inverter_sw_version": "2.4.1",
        "inverter_hw_version": "rev-C",
        "inverter_manufacturer": "Acme",
        "inverter_model": "BiDi-11",
        "inverter_serial_number": "SN-0042",
        "supported_modes": [
            "ChargeFunction", "DischargeFunction", "VoltVarFunction", "ConstantPowerFactorOverExcitedFunction"
        ],
        "max_apparent_power_charging_var_absorption_va": 11000,
        "max_apparent_power_charging_var_injection_va": 11000,
        "max_apparent_power_discharging_var_absorption_va": 10000,
        "max_apparent_power_discharging_var_injection_va": 10000,
        "max_var_absorption_charging_var": 4800,
        "max_var_injection_charging_var": 4800,
        "max_var_absorption_discharging_var": 4400,
        "max_var_injection_discharging_var": 4400,
        "reactive_susceptance_s": 0.01,
        "over_excited_power_factor": 0.95,
        "over_excited_discharge_power_w": 9500,
        "under_excited_power_factor": 0.95,
        "under_excited_discharge_power_w": 9500,
        "nominal_voltage_v": 240,
        "maximum_voltage_v": 264,
        "minimum_voltage_v": 211,
        "nominal_voltage_offset_v": 0.5,
        "nominal_frequency_hz": 60,
        "ieee1547_normal_category": "CategoryA",
        "ieee1547_abnormal_category": "CategoryIII",
        "j3072_certified": true,
        "j3072_certification_date": 1767225600,
        "useable_watt_hours": 75000,
        "minimum_charging_duration_s": 600,
        "duration_maximum_charge_rate_s": 3600,
        "duration_maximum_discharge_rate_s": 1800,
        "operational_state": "On",
        "connection_status": "Connected"
    })");

    ASSERT_TRUE(profile.has_value()) << error;
    const auto problems = iso15118::ev::validate_config(sae_config(*profile));
    EXPECT_TRUE(problems.empty()) << problems.front();
}

// The parser checks shape only; a well formed but unusable profile is validate_config's to reject.
TEST_F(SaeProfileConfigTest, WellFormedButUnusableProfileIsLeftToValidateConfig) {
    const auto profile = parse(R"({"supported_modes": ["ChargeFunction"], "over_excited_power_factor": 1.5})");

    ASSERT_TRUE(profile.has_value()) << error;
    const auto problems = iso15118::ev::validate_config(sae_config(*profile));
    EXPECT_THAT(problems, ::testing::Contains(::testing::HasSubstr("supported_modes")));
    EXPECT_THAT(problems, ::testing::Contains(::testing::HasSubstr("over_excited_power_factor")));
}

} // namespace
