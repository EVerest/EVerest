// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <everest/ocpp_module_common/custom_error_mapping.hpp>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

namespace {

using namespace ocpp_module_common;
using namespace ocpp_module_common::custom_error_mapping;
using ocpp::v16::ChargePointErrorCode;

std::shared_ptr<const CustomFileErrorMapping> mapping_of(const std::string& content) {
    auto result = parse_error_mapping(content);
    EXPECT_NE(result.error_mapping, nullptr) << (result.findings.empty() ? "" : result.findings.front().to_string());
    return result.error_mapping;
}

Everest::error::Error error_of(const std::string& type, const std::string& sub_type = "",
                               const std::string& message = "sensor reports fault") {
    Everest::error::Error error;
    error.type = type;
    error.sub_type = sub_type;
    error.message = message;
    return error;
}

constexpr auto TEMPERATURE_ENTRY = R"({
    "evse_board_support/MREC3HighTemperature": {
        "v16": {"error_code": "HighTemperature", "vendor_id": "com.example", "vendor_error_code": "T-210",
                "info": "Temperature error: ${message}"},
        "v2": {"tech_code": "T-210", "component_name": "Connector", "variable_name": "Temperature",
               "severity": 3, "tech_info": "The Connector temperature is high"}
    }
})";

// what the built-in mapping reports for an error without a custom error mapping file
ocpp::v16::ErrorInfo built_in_v16(const Everest::error::Error& error) {
    return to_v16_error_info(error);
}

ocpp::v2::EventData built_in_v2(const Everest::error::Error& error, const std::int32_t event_id = 1) {
    return to_v2_event_data(error, false, event_id);
}

void expect_equal(const ocpp::v16::ErrorInfo& actual, const ocpp::v16::ErrorInfo& expected) {
    EXPECT_EQ(actual.uuid, expected.uuid);
    EXPECT_EQ(actual.error_code, expected.error_code);
    EXPECT_EQ(actual.is_fault, expected.is_fault);
    EXPECT_EQ(actual.info, expected.info);
    EXPECT_EQ(actual.vendor_id, expected.vendor_id);
    EXPECT_EQ(actual.vendor_error_code, expected.vendor_error_code);
    EXPECT_EQ(actual.timestamp.to_rfc3339(), expected.timestamp.to_rfc3339());
}

void expect_equal(const ocpp::v2::EventData& actual, const ocpp::v2::EventData& expected) {
    EXPECT_EQ(nlohmann::json(actual), nlohmann::json(expected));
}

TEST(CustomFileErrorMappingTest, ErrorWithoutEntryKeepsTheBuiltInResult) {
    const auto mapping = mapping_of(TEMPERATURE_ENTRY);
    const auto error = error_of("evse_board_support/MREC2GroundFailure");
    expect_equal(mapping->overlay(error, built_in_v16(error)), built_in_v16(error));
    expect_equal(mapping->overlay(error, built_in_v2(error)), built_in_v2(error));
}

TEST(CustomFileErrorMappingTest, OverridesTheFieldsOfTheEntry) {
    const auto mapping = mapping_of(TEMPERATURE_ENTRY);
    const auto error = error_of("evse_board_support/MREC3HighTemperature");

    const auto v16 = mapping->overlay(error, built_in_v16(error));
    EXPECT_EQ(v16.error_code, ChargePointErrorCode::HighTemperature);
    EXPECT_EQ(v16.vendor_id.value().get(), "com.example");
    EXPECT_EQ(v16.vendor_error_code.value().get(), "T-210");
    EXPECT_EQ(v16.info.value().get(), "Temperature error: sensor reports fault");

    const auto v2 = mapping->overlay(error, built_in_v2(error, 7));
    EXPECT_EQ(v2.eventId, 7);
    EXPECT_EQ(v2.techCode.value().get(), "T-210");
    EXPECT_EQ(v2.techInfo.value().get(), "The Connector temperature is high");
    EXPECT_EQ(v2.component.name.get(), "Connector");
    EXPECT_EQ(v2.variable.name.get(), "Temperature");
    EXPECT_EQ(v2.severity.value(), 3);
    EXPECT_EQ(v2.cleared, false);
}

TEST(CustomFileErrorMappingTest, SubstitutesErrorPlaceholdersInTexts) {
    const auto mapping = mapping_of(R"({"generic/VendorError": {
        "v16": {"info": "${severity}: ${message}"},
        "v2": {"tech_info": "${type}#${sub_type}: ${message}"}}})");
    auto error = error_of("generic/VendorError", "Spd");
    error.severity = Everest::error::Severity::Medium;

    EXPECT_EQ(mapping->overlay(error, built_in_v16(error)).info.value().get(), "Medium: sensor reports fault");
    EXPECT_EQ(mapping->overlay(error, built_in_v2(error)).techInfo.value().get(),
              "generic/VendorError#Spd: sensor reports fault");
}

TEST(CustomFileErrorMappingTest, SubstitutesErrorPlaceholdersInCodes) {
    const auto mapping = mapping_of(R"({"generic/VendorError": {
        "v16": {"vendor_id": "${origin_module}", "vendor_error_code": "E-${sub_type}"},
        "v2": {"tech_code": "${vendor_id}-${sub_type}"}}})");
    auto error = error_of("generic/VendorError", "Spd");
    error.vendor_id = "com.example";
    error.origin = ImplementationIdentifier("bsp_1", "main");

    const auto v16 = mapping->overlay(error, built_in_v16(error));
    EXPECT_EQ(v16.vendor_id.value().get(), "bsp_1");
    EXPECT_EQ(v16.vendor_error_code.value().get(), "E-Spd");
    EXPECT_EQ(mapping->overlay(error, built_in_v2(error)).techCode.value().get(), "com.example-Spd");
}

TEST(CustomFileErrorMappingTest, TruncatesCodesAfterSubstitution) {
    const auto mapping = mapping_of(
        R"({"generic/VendorError": {"v16": {"vendor_error_code": "${message}"}, "v2": {"tech_code": "${message}"}}})");
    const auto error = error_of("generic/VendorError", "", std::string(60, 'm'));
    EXPECT_EQ(mapping->overlay(error, built_in_v16(error)).vendor_error_code.value().get(), std::string(50, 'm'));
    EXPECT_EQ(mapping->overlay(error, built_in_v2(error)).techCode.value().get(), std::string(50, 'm'));
}

TEST(CustomFileErrorMappingTest, TruncatesInfoAfterSubstitution) {
    const auto mapping = mapping_of(R"({"generic/VendorError": {"v16": {"info": "${message}"}}})");
    const auto error = error_of("generic/VendorError", "", std::string(60, 'm'));
    EXPECT_EQ(mapping->overlay(error, built_in_v16(error)).info.value().get(), std::string(50, 'm'));
}

TEST(CustomFileErrorMappingTest, FieldsTheEntryLeavesOutKeepTheBuiltInValue) {
    const auto mapping = mapping_of(
        R"({"generic/VendorError": {"v16": {"vendor_error_code": "X"}, "v2": {"variable_name": "Tripped"}}})");
    auto error = error_of("generic/VendorError", "Spd");
    error.origin = ImplementationIdentifier("bsp_1", "main", Mapping(1, 2));

    auto expected_v16 = built_in_v16(error);
    expected_v16.vendor_error_code = ocpp::CiString<50>("X");
    expect_equal(mapping->overlay(error, built_in_v16(error)), expected_v16);

    auto expected_v2 = built_in_v2(error);
    expected_v2.variable.name = "Tripped";
    expect_equal(mapping->overlay(error, built_in_v2(error)), expected_v2);
}

TEST(CustomFileErrorMappingTest, KeepsTheBuiltInMrecValuesTheEntryLeavesOut) {
    const auto mapping = mapping_of(R"({"evse_board_support/MREC3HighTemperature": {"v16": {"vendor_id": "com.example"},
                                        "v2": {"tech_info": "too hot"}}})");
    const auto error = error_of("evse_board_support/MREC3HighTemperature");

    auto expected_v16 = built_in_v16(error);
    expected_v16.vendor_id = ocpp::CiString<255>("com.example");
    expect_equal(mapping->overlay(error, built_in_v16(error)), expected_v16);
    EXPECT_EQ(expected_v16.error_code, ChargePointErrorCode::HighTemperature);

    auto expected_v2 = built_in_v2(error);
    expected_v2.techInfo = ocpp::CiString<500>("too hot");
    expect_equal(mapping->overlay(error, built_in_v2(error)), expected_v2);
    EXPECT_EQ(expected_v2.techCode.value().get(), "CX003");
}

TEST(CustomFileErrorMappingTest, ChargingStationComponentHasNoEvse) {
    const auto mapping = mapping_of(R"({"generic/VendorError": {"v2": {"component_name": "ChargingStation"}}})");
    auto error = error_of("generic/VendorError");
    error.origin = ImplementationIdentifier("bsp_1", "main", Mapping(1, 2));
    ASSERT_TRUE(built_in_v2(error).component.evse.has_value());

    const auto v2 = mapping->overlay(error, built_in_v2(error));
    EXPECT_EQ(v2.component.name.get(), "ChargingStation");
    EXPECT_FALSE(v2.component.evse.has_value());
}

TEST(CustomFileErrorMappingTest, MissingSectionKeepsTheBuiltInResult) {
    const auto mapping = mapping_of(R"({"generic/VendorError#Spd": {"v2": {"tech_code": "SPD-1"}}})");
    const auto error = error_of("generic/VendorError", "Spd");
    expect_equal(mapping->overlay(error, built_in_v16(error)), built_in_v16(error));
    EXPECT_EQ(mapping->overlay(error, built_in_v2(error)).techCode.value().get(), "SPD-1");
}

TEST(CustomFileErrorMappingTest, SubTypeEntryAppliesOnlyToItsSubType) {
    const auto mapping = mapping_of(R"({"generic/VendorError#YourCustomErrorType": {
        "v16": {"error_code": "OtherError", "vendor_id": "com.example", "vendor_error_code": "T-210"}}})");
    const auto error = error_of("generic/VendorError", "YourCustomErrorType", "our message");
    EXPECT_EQ(mapping->overlay(error, built_in_v16(error)).vendor_id.value().get(), "com.example");

    for (const auto& other : {error_of("generic/VendorError", "Other"), error_of("generic/VendorError")}) {
        expect_equal(mapping->overlay(other, built_in_v16(other)), built_in_v16(other));
    }
}

TEST(CustomFileErrorMappingTest, ReportsNoSeverityWithoutTheField) {
    const auto mapping = mapping_of(R"({"generic/VendorError": {"v2": {"tech_code": "A"}}})");
    const auto error = error_of("generic/VendorError");
    EXPECT_FALSE(mapping->overlay(error, built_in_v2(error)).severity.has_value());
}

TEST(CustomFileErrorMappingTest, ReportsTheSameSeverityForEveryOccurrence) {
    const auto mapping = mapping_of(R"({"generic/VendorError": {"v2": {"severity": 0}}})");
    for (const auto severity : {Everest::error::Severity::Low, Everest::error::Severity::High}) {
        auto error = error_of("generic/VendorError");
        error.severity = severity;
        EXPECT_EQ(mapping->overlay(error, built_in_v2(error)).severity.value(), 0);
    }
}

TEST(CustomFileErrorMappingTest, TruncatesTextToTheOcppLimits) {
    const auto long_info = std::string(60, 'x');
    const auto mapping = mapping_of(R"({"generic/VendorError": {"v16": {"info": ")" + long_info + R"("}}})");
    const auto error = error_of("generic/VendorError");
    EXPECT_EQ(mapping->overlay(error, built_in_v16(error)).info.value().get(), std::string(50, 'x'));
}

TEST(CustomFileErrorMappingTest, NeverOverridesTheInoperativeError) {
    Entry entry{
        {"evse_manager/Inoperative", std::nullopt}, V16Identity{ChargePointErrorCode::GroundFailure}, V2Identity{"T"}};
    const CustomFileErrorMapping mapping{{{entry.key, entry}}};
    const auto error = error_of("evse_manager/Inoperative");
    EXPECT_EQ(mapping.find(error.type, error.sub_type), nullptr);

    expect_equal(mapping.overlay(error, built_in_v16(error)), built_in_v16(error));
    EXPECT_TRUE(built_in_v16(error).is_fault);
    expect_equal(mapping.overlay(error, built_in_v2(error)), built_in_v2(error));
}

TEST(CustomFileErrorMappingTest, AnEmptyMappingOverridesNothing) {
    const CustomFileErrorMapping mapping;
    const auto error = error_of("generic/VendorError");
    expect_equal(mapping.overlay(error, built_in_v16(error)), built_in_v16(error));
    expect_equal(mapping.overlay(error, built_in_v2(error)), built_in_v2(error));
}

} // namespace
