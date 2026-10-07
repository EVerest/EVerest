// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

// A custom error mapping file only overrides the fields its entries set: every other field of the
// reported error is the one the built-in mapping reports without a file.

#include "v16_error_test_helpers.hpp"

#include <everest/ocpp_module_common/custom_error_mapping.hpp>
#include <v2_chargepoint.hpp>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include <memory>
#include <string>

namespace {

using namespace ocpp_multi;
using namespace v16_error_test;
using ocpp_module_common::custom_error_mapping::CustomFileErrorMapping;
using testing::NiceMock;
using testing::Return;

constexpr auto MAPPING = R"({
    "evse_board_support/MREC3HighTemperature": {"v16": {"vendor_id": "com.example"}, "v2": {"tech_info": "too hot"}},
    "generic/VendorError": {"v16": {"error_code": "GroundFailure"}, "v2": {"variable_name": "Tripped"}}
})";

std::shared_ptr<const CustomFileErrorMapping> custom_mapping() {
    auto result = ocpp_module_common::custom_error_mapping::parse_error_mapping(MAPPING);
    EXPECT_NE(result.error_mapping, nullptr);
    return result.error_mapping;
}

class CustomErrorMappingOverlayV16 : public ChargePointV16ErrorTest {
protected:
    // the ErrorInfo reported for error, without and with the custom error mapping file
    std::pair<ocpp::v16::ErrorInfo, ocpp::v16::ErrorInfo> convert(const Everest::error::Error& error) {
        ON_CALL(m_callbacks, custom_error_mapping()).WillByDefault(Return(nullptr));
        auto built_in = m_chargepoint.convert_error(error);
        ON_CALL(m_callbacks, custom_error_mapping()).WillByDefault(Return(custom_mapping()));
        return {std::move(built_in), m_chargepoint.convert_error(error)};
    }
};

TEST_F(CustomErrorMappingOverlayV16, MrecErrorKeepsTheFieldsTheEntryLeavesOut) {
    const auto [built_in, overlaid] = convert(make_error("evse_board_support/MREC3HighTemperature", "too hot"));

    EXPECT_EQ(opt_str(overlaid.vendor_id), "com.example");
    EXPECT_EQ(overlaid.error_code, ocpp::v16::ChargePointErrorCode::HighTemperature);
    EXPECT_EQ(overlaid.error_code, built_in.error_code);
    EXPECT_EQ(opt_str(overlaid.vendor_error_code), "CX003");
    EXPECT_EQ(opt_str(overlaid.vendor_error_code), opt_str(built_in.vendor_error_code));
    EXPECT_EQ(opt_str(overlaid.info), "too hot");
    EXPECT_EQ(opt_str(overlaid.info), opt_str(built_in.info));
    EXPECT_EQ(overlaid.is_fault, built_in.is_fault);
}

TEST_F(CustomErrorMappingOverlayV16, DefaultMappedErrorKeepsTheFieldsTheEntryLeavesOut) {
    const auto [built_in, overlaid] = convert(make_error("generic/VendorError", "spd tripped", "Spd"));

    EXPECT_EQ(overlaid.error_code, ocpp::v16::ChargePointErrorCode::GroundFailure);
    EXPECT_EQ(opt_str(overlaid.vendor_id), "spd tripped");
    EXPECT_EQ(opt_str(overlaid.vendor_id), opt_str(built_in.vendor_id));
    EXPECT_EQ(opt_str(overlaid.vendor_error_code), "VendorError/Spd");
    EXPECT_EQ(opt_str(overlaid.vendor_error_code), opt_str(built_in.vendor_error_code));
    EXPECT_EQ(opt_str(overlaid.info), opt_str(built_in.info));
}

TEST_F(CustomErrorMappingOverlayV16, ErrorWithoutEntryIsReportedAsWithoutTheFile) {
    const auto [built_in, overlaid] = convert(make_error("evse_board_support/MREC2GroundFailure", "ground fault"));

    EXPECT_EQ(overlaid.error_code, built_in.error_code);
    EXPECT_EQ(opt_str(overlaid.vendor_id), opt_str(built_in.vendor_id));
    EXPECT_EQ(opt_str(overlaid.vendor_error_code), opt_str(built_in.vendor_error_code));
    EXPECT_EQ(opt_str(overlaid.info), opt_str(built_in.info));
}

// expose the protected test seams
struct TestChargePointV2 : public ChargePointV2 {
    using ChargePointV2::ChargePointV2;
    using ChargePointV2::convert_error;
};

class CustomErrorMappingOverlayV2 : public testing::Test {
protected:
    module::stub::QuietModuleAdapterStub m_adapter;
    Requirement m_requirement{"ocpp", 0};
    evse_securityIntf m_security{&m_adapter, m_requirement, "security", std::nullopt};
    NiceMock<stubs::GenericChargePointCallbacksMock> m_callbacks;
    TestChargePointV2 m_chargepoint{m_callbacks, m_security};

    // the EventData reported for error, without and with the custom error mapping file
    std::pair<nlohmann::json, ocpp::v2::EventData> convert(const Everest::error::Error& error) {
        GenericChargePointInterface::EventInfo event{};
        event.event_id = 1;
        event.evse_id = 1;
        event.error = error;
        event.event_cleared = false;

        ON_CALL(m_callbacks, custom_error_mapping()).WillByDefault(Return(nullptr));
        const auto built_in = m_chargepoint.convert_error(event);
        ON_CALL(m_callbacks, custom_error_mapping()).WillByDefault(Return(custom_mapping()));
        const auto overlaid = m_chargepoint.convert_error(event);
        EXPECT_TRUE(built_in.has_value());
        EXPECT_TRUE(overlaid.has_value());
        return {nlohmann::json(built_in.value_or(ocpp::v2::EventData{})), overlaid.value_or(ocpp::v2::EventData{})};
    }
};

TEST_F(CustomErrorMappingOverlayV2, MrecErrorKeepsTheFieldsTheEntryLeavesOut) {
    const auto [built_in, overlaid] = convert(make_error("evse_board_support/MREC3HighTemperature", "sensor fault"));

    EXPECT_EQ(overlaid.techInfo.value().get(), "too hot");
    EXPECT_EQ(overlaid.techCode.value().get(), "CX003");

    auto expected = built_in;
    expected["techInfo"] = "too hot";
    EXPECT_EQ(nlohmann::json(overlaid), expected);
}

TEST_F(CustomErrorMappingOverlayV2, DefaultMappedErrorKeepsTheFieldsTheEntryLeavesOut) {
    const auto [built_in, overlaid] = convert(make_error("generic/VendorError", "spd tripped", "Spd"));

    EXPECT_EQ(overlaid.variable.name.get(), "Tripped");
    EXPECT_EQ(overlaid.techCode.value().get(), "generic/VendorError");

    auto expected = built_in;
    expected["variable"]["name"] = "Tripped";
    EXPECT_EQ(nlohmann::json(overlaid), expected);
}

TEST_F(CustomErrorMappingOverlayV2, ErrorWithoutEntryIsReportedAsWithoutTheFile) {
    const auto [built_in, overlaid] = convert(make_error("evse_board_support/MREC2GroundFailure", "ground fault"));
    EXPECT_EQ(nlohmann::json(overlaid), built_in);
}

} // namespace
