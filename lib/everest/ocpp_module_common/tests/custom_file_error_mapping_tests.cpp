// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <everest/ocpp_module_common/custom_error_mapping.hpp>

#include <gtest/gtest.h>

#include <memory>
#include <string>

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
                "info": "Temperature error raised at ${actual_value} deg"},
        "v2": {"tech_code": "T-210", "component_name": "Connector", "variable_name": "Temperature",
               "severity": 3, "tech_info": "The Connector temperature is high"}
    }
})";

TEST(CustomFileErrorMappingTest, ErrorWithoutEntryIsNotHandled) {
    const auto mapping = mapping_of(TEMPERATURE_ENTRY);
    const auto error = error_of("evse_board_support/MREC2GroundFailure");
    EXPECT_FALSE(mapping->try_convert(error).has_value());
    EXPECT_FALSE(mapping->try_convert(error, false, 1).has_value());
}

TEST(CustomFileErrorMappingTest, ReportsTheFieldsOfTheEntry) {
    const auto mapping = mapping_of(TEMPERATURE_ENTRY);
    const auto error = error_of("evse_board_support/MREC3HighTemperature");

    const auto v16 = mapping->try_convert(error);
    ASSERT_TRUE(v16.has_value());
    EXPECT_EQ(v16->error_code, ChargePointErrorCode::HighTemperature);
    EXPECT_EQ(v16->vendor_id.value().get(), "com.example");
    EXPECT_EQ(v16->vendor_error_code.value().get(), "T-210");
    EXPECT_EQ(v16->info.value().get(), "Temperature error raised at ${actual_value} deg");

    const auto v2 = mapping->try_convert(error, false, 7);
    ASSERT_TRUE(v2.has_value());
    EXPECT_EQ(v2->eventId, 7);
    EXPECT_EQ(v2->techCode.value().get(), "T-210");
    EXPECT_EQ(v2->techInfo.value().get(), "The Connector temperature is high");
    EXPECT_EQ(v2->component.name.get(), "Connector");
    EXPECT_EQ(v2->variable.name.get(), "Temperature");
    EXPECT_EQ(v2->severity.value(), 3);
    EXPECT_EQ(v2->cleared, false);
}

TEST(CustomFileErrorMappingTest, UnsetFieldsFollowFromTheError) {
    const auto mapping = mapping_of(
        R"({"generic/VendorError": {"v16": {"vendor_error_code": "X"}, "v2": {"variable_name": "Tripped"}}})");
    const auto error = error_of("generic/VendorError");

    const auto v16 = mapping->try_convert(error);
    ASSERT_TRUE(v16.has_value());
    EXPECT_EQ(v16->error_code, ChargePointErrorCode::OtherError);
    EXPECT_FALSE(v16->is_fault);
    EXPECT_FALSE(v16->vendor_id.has_value());
    EXPECT_FALSE(v16->info.has_value());
    EXPECT_EQ(v16->vendor_error_code.value().get(), "X");

    const auto v2 = mapping->try_convert(error, false, 1);
    ASSERT_TRUE(v2.has_value());
    EXPECT_FALSE(v2->techCode.has_value());
    EXPECT_EQ(v2->techInfo.value().get(), "sensor reports fault");
    EXPECT_EQ(v2->variable.name.get(), "Tripped");
}

TEST(CustomFileErrorMappingTest, MissingSectionIsNotHandled) {
    const auto mapping = mapping_of(R"({"generic/VendorError#Spd": {"v2": {"tech_code": "SPD-1"}}})");
    const auto error = error_of("generic/VendorError", "Spd");
    EXPECT_FALSE(mapping->try_convert(error).has_value());
    const auto v2 = mapping->try_convert(error, false, 1);
    ASSERT_TRUE(v2.has_value());
    EXPECT_EQ(v2->techCode.value().get(), "SPD-1");
}

TEST(CustomFileErrorMappingTest, SubTypeEntryAppliesOnlyToItsSubType) {
    const auto mapping = mapping_of(R"({"generic/VendorError#YourCustomErrorType": {
        "v16": {"error_code": "OtherError", "vendor_id": "com.example", "vendor_error_code": "T-210"}}})");
    const auto v16 = mapping->try_convert(error_of("generic/VendorError", "YourCustomErrorType", "our message"));
    ASSERT_TRUE(v16.has_value());
    EXPECT_EQ(v16->vendor_id.value().get(), "com.example");

    EXPECT_FALSE(mapping->try_convert(error_of("generic/VendorError", "Other")).has_value());
    EXPECT_FALSE(mapping->try_convert(error_of("generic/VendorError")).has_value());
}

TEST(CustomFileErrorMappingTest, ReportsNoSeverityWithoutTheField) {
    const auto mapping = mapping_of(R"({"generic/VendorError": {"v2": {"tech_code": "A"}}})");
    const auto v2 = mapping->try_convert(error_of("generic/VendorError"), false, 1);
    ASSERT_TRUE(v2.has_value());
    EXPECT_FALSE(v2->severity.has_value());
}

TEST(CustomFileErrorMappingTest, ReportsTheSameSeverityForEveryOccurrence) {
    const auto mapping = mapping_of(R"({"generic/VendorError": {"v2": {"severity": 0}}})");
    for (const auto severity : {Everest::error::Severity::Low, Everest::error::Severity::High}) {
        auto error = error_of("generic/VendorError");
        error.severity = severity;
        const auto v2 = mapping->try_convert(error, false, 1);
        ASSERT_TRUE(v2.has_value());
        EXPECT_EQ(v2->severity.value(), 0);
    }
}

TEST(CustomFileErrorMappingTest, TruncatesTextToTheOcppLimits) {
    const auto long_info = std::string(60, 'x');
    const auto mapping = mapping_of(R"({"generic/VendorError": {"v16": {"info": ")" + long_info + R"("}}})");
    const auto v16 = mapping->try_convert(error_of("generic/VendorError"));
    ASSERT_TRUE(v16.has_value());
    EXPECT_EQ(v16->info.value().get(), std::string(50, 'x'));
}

TEST(CustomFileErrorMappingTest, NeverHandlesTheInoperativeError) {
    Entry entry{{"evse_manager/Inoperative", std::nullopt}, V16Identity{}, V2Identity{}};
    const CustomFileErrorMapping mapping{{{entry.key, entry}}};
    const auto error = error_of("evse_manager/Inoperative");
    EXPECT_EQ(mapping.find(error.type, error.sub_type), nullptr);
    EXPECT_FALSE(mapping.try_convert(error).has_value());
    EXPECT_FALSE(mapping.try_convert(error, false, 1).has_value());
}

TEST(CustomFileErrorMappingTest, AnEmptyMappingHandlesNothing) {
    const CustomFileErrorMapping mapping;
    const auto error = error_of("generic/VendorError");
    EXPECT_FALSE(mapping.try_convert(error).has_value());
    EXPECT_FALSE(mapping.try_convert(error, false, 1).has_value());
}

} // namespace
