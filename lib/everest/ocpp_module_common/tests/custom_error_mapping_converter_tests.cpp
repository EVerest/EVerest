// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <everest/ocpp_module_common/custom_error_mapping_converter.hpp>

#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <stdexcept>
#include <string>

namespace {

using namespace ocpp_module_common;
using namespace ocpp_module_common::custom_error_mapping;
using ocpp::v16::ChargePointErrorCode;

std::shared_ptr<const CustomErrorMapping> mapping_of(const std::string& content) {
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

/// \brief Converts every error, reporting it as a fault with fixed fields
struct FixedV16 : ErrorMappingV16 {
    std::optional<ocpp::v16::ErrorInfo> try_convert(const Everest::error::Error& error) const override {
        auto info = make_v16_error_info(error);
        info.is_fault = true;
        info.error_code = ChargePointErrorCode::GroundFailure;
        info.vendor_id = ocpp::CiString<255>(std::string("fixed"));
        return info;
    }
};

/// \brief Handles no error
struct NoneV16 : ErrorMappingV16 {
    std::optional<ocpp::v16::ErrorInfo> try_convert(const Everest::error::Error&) const override {
        return std::nullopt;
    }
};

class CustomErrorMappingConverterTest : public testing::Test {
protected:
    const MrecErrorMapping mrec;
    const DefaultErrorMappingV16 fallback_v16;
    const DefaultErrorMappingV2X fallback_v2;

    CustomErrorMappingConverter converter_of(const std::string& content) {
        return CustomErrorMappingConverter{mapping_of(content), {&mrec, &fallback_v16}, {&mrec, &fallback_v2}};
    }
};

constexpr auto SLIDE7_INTERNAL = R"({
    "evse_board_support/MREC3HighTemperature": {
        "v16": {"error_code": "HighTemperature", "vendor_id": "com.example", "vendor_error_code": "T-210",
                "info": "Temperature error raised at ${actual_value} deg"},
        "v2": {"tech_code": "T-210", "component_name": "Connector", "variable_name": "Temperature",
               "severity": {"high": 3, "medium": 5, "low": 8}, "techInfo": "The Connector temperature is high"}
    }
})";

TEST_F(CustomErrorMappingConverterTest, ErrorWithoutEntryIsLeftToTheBase) {
    const auto converter = converter_of(SLIDE7_INTERNAL);
    const auto error = error_of("evse_board_support/MREC2GroundFailure");
    EXPECT_FALSE(converter.try_convert(error).has_value());
    EXPECT_FALSE(converter.try_convert(error, false, 1).has_value());
}

TEST_F(CustomErrorMappingConverterTest, ReplacesTheBuiltinMrecEntry) {
    const auto converter = converter_of(SLIDE7_INTERNAL);
    const auto error = error_of("evse_board_support/MREC3HighTemperature");

    const auto v16 = converter.try_convert(error);
    ASSERT_TRUE(v16.has_value());
    EXPECT_EQ(v16->error_code, ChargePointErrorCode::HighTemperature);
    EXPECT_EQ(v16->vendor_id.value().get(), "com.example");
    EXPECT_EQ(v16->vendor_error_code.value().get(), "T-210");
    EXPECT_EQ(v16->info.value().get(), "Temperature error raised at ${actual_value} deg");

    const auto v2 = converter.try_convert(error, false, 7);
    ASSERT_TRUE(v2.has_value());
    EXPECT_EQ(v2->eventId, 7);
    EXPECT_EQ(v2->techCode.value().get(), "T-210");
    EXPECT_EQ(v2->techInfo.value().get(), "The Connector temperature is high");
    EXPECT_EQ(v2->component.name.get(), "Connector");
    EXPECT_EQ(v2->variable.name.get(), "Temperature");
    EXPECT_EQ(v2->cleared, false);
}

TEST_F(CustomErrorMappingConverterTest, UnsetFieldsComeFromTheBase) {
    const auto converter = converter_of(R"({"evse_board_support/MREC3HighTemperature": {
        "v16": {"vendor_error_code": "T-210"}, "v2": {"tech_code": "T-210"}}})");
    const auto error = error_of("evse_board_support/MREC3HighTemperature");
    const auto expected_v16 = mrec.try_convert(error);
    const auto expected_v2 = mrec.try_convert(error, true, 3);
    ASSERT_TRUE(expected_v16.has_value());
    ASSERT_TRUE(expected_v2.has_value());

    const auto v16 = converter.try_convert(error);
    ASSERT_TRUE(v16.has_value());
    EXPECT_EQ(v16->error_code, expected_v16->error_code);
    EXPECT_EQ(v16->vendor_id, expected_v16->vendor_id);
    EXPECT_EQ(v16->info, expected_v16->info);
    EXPECT_EQ(v16->vendor_error_code.value().get(), "T-210");

    const auto v2 = converter.try_convert(error, true, 3);
    ASSERT_TRUE(v2.has_value());
    EXPECT_EQ(v2->techCode.value().get(), "T-210");
    EXPECT_EQ(v2->techInfo, expected_v2->techInfo);
    EXPECT_EQ(v2->component.name, expected_v2->component.name);
    EXPECT_EQ(v2->variable.name, expected_v2->variable.name);
    EXPECT_EQ(v2->cleared, true);
}

TEST_F(CustomErrorMappingConverterTest, MissingSectionIsLeftToTheBase) {
    const auto converter = converter_of(R"({"generic/VendorError#Spd": {"v2": {"tech_code": "SPD-1"}}})");
    const auto error = error_of("generic/VendorError", "Spd");
    EXPECT_FALSE(converter.try_convert(error).has_value());
    const auto v2 = converter.try_convert(error, false, 1);
    ASSERT_TRUE(v2.has_value());
    EXPECT_EQ(v2->techCode.value().get(), "SPD-1");
}

TEST_F(CustomErrorMappingConverterTest, SubTypeEntryAppliesOnlyToItsSubType) {
    const auto converter = converter_of(R"({"generic/VendorError#YourCustomErrorType": {
        "v16": {"error_code": "OtherError", "vendor_id": "com.example", "vendor_error_code": "T-210"}}})");
    const auto v16 = converter.try_convert(error_of("generic/VendorError", "YourCustomErrorType", "our message"));
    ASSERT_TRUE(v16.has_value());
    EXPECT_EQ(v16->error_code, ChargePointErrorCode::OtherError);
    EXPECT_EQ(v16->vendor_id.value().get(), "com.example");
    EXPECT_EQ(v16->vendor_error_code.value().get(), "T-210");

    EXPECT_FALSE(converter.try_convert(error_of("generic/VendorError", "Other")).has_value());
    EXPECT_FALSE(converter.try_convert(error_of("generic/VendorError")).has_value());
}

TEST_F(CustomErrorMappingConverterTest, KeepsTheFaultDecisionOfTheBase) {
    const FixedV16 fixed;
    const CustomErrorMappingConverter converter{
        mapping_of(R"({"generic/VendorError": {"v16": {"error_code": "OtherError"}}})"), {&fixed}, {}};
    const auto v16 = converter.try_convert(error_of("generic/VendorError"));
    ASSERT_TRUE(v16.has_value());
    EXPECT_TRUE(v16->is_fault);
    EXPECT_EQ(v16->error_code, ChargePointErrorCode::OtherError);
    EXPECT_EQ(v16->vendor_id.value().get(), "fixed");
}

TEST_F(CustomErrorMappingConverterTest, AsksTheBaseInOrder) {
    const NoneV16 none;
    const FixedV16 fixed;
    const CustomErrorMappingConverter converter{
        mapping_of(R"({"generic/VendorError": {"v16": {"vendor_error_code": "X"}}})"),
        {&none, &fixed, &fallback_v16},
        {}};
    const auto v16 = converter.try_convert(error_of("generic/VendorError"));
    ASSERT_TRUE(v16.has_value());
    EXPECT_EQ(v16->vendor_id.value().get(), "fixed");
}

TEST_F(CustomErrorMappingConverterTest, WithoutBaseStartsFromTheErrorAlone) {
    const CustomErrorMappingConverter converter{
        mapping_of(
            R"({"generic/VendorError": {"v16": {"vendor_error_code": "X"}, "v2": {"variable_name": "Tripped"}}})"),
        {},
        {}};
    const auto error = error_of("generic/VendorError");
    const auto v16 = converter.try_convert(error);
    ASSERT_TRUE(v16.has_value());
    EXPECT_EQ(v16->error_code, ChargePointErrorCode::OtherError);
    EXPECT_FALSE(v16->is_fault);
    EXPECT_FALSE(v16->vendor_id.has_value());
    EXPECT_EQ(v16->vendor_error_code.value().get(), "X");

    const auto v2 = converter.try_convert(error, false, 1);
    ASSERT_TRUE(v2.has_value());
    EXPECT_FALSE(v2->techCode.has_value());
    EXPECT_EQ(v2->variable.name.get(), "Tripped");
}

TEST_F(CustomErrorMappingConverterTest, TruncatesTextToTheOcppLimits) {
    const auto long_info = std::string(60, 'x');
    const auto converter = converter_of(R"({"generic/VendorError": {"v16": {"info": ")" + long_info + R"("}}})");
    const auto v16 = converter.try_convert(error_of("generic/VendorError"));
    ASSERT_TRUE(v16.has_value());
    EXPECT_EQ(v16->info.value().get(), std::string(50, 'x'));
}

TEST_F(CustomErrorMappingConverterTest, LeavesTheInoperativeErrorToItsOwnMapping) {
    const auto converter = converter_of(R"({"evse_manager/Inoperative": {"v16": {"vendor_error_code": "X"}}})");
    const auto error = error_of("evse_manager/Inoperative");
    EXPECT_FALSE(converter.try_convert(error).has_value());
    EXPECT_FALSE(converter.try_convert(error, false, 1).has_value());
}

TEST_F(CustomErrorMappingConverterTest, KeepsTheMappingAliveAfterTheLoadResultIsGone) {
    std::optional<CustomErrorMappingConverter> converter;
    {
        auto result = parse_error_mapping(R"({"generic/VendorError": {"v2": {"tech_code": "A"}}})");
        ASSERT_NE(result.error_mapping, nullptr);
        converter.emplace(result.error_mapping, std::vector<const ErrorMappingV16*>{},
                          std::vector<const ErrorMappingV2X*>{&fallback_v2});
    }
    EXPECT_NE(converter->mapping().find("generic/VendorError", ""), nullptr);
    const auto v2 = converter->try_convert(error_of("generic/VendorError"), false, 1);
    ASSERT_TRUE(v2.has_value());
    EXPECT_EQ(v2->techCode.value().get(), "A");
}

TEST_F(CustomErrorMappingConverterTest, RejectsAMissingMapping) {
    EXPECT_THROW(CustomErrorMappingConverter(nullptr, {}, {}), std::invalid_argument);
}

} // namespace
