// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include <everest/ocpp_module_common/conversions.hpp>

namespace {

using ocpp_module_common::conversions::to_ocpp_data_transfer_response;

TEST(DataTransferConversions, response_json_data_is_parsed) {
    types::ocpp::DataTransferResponse response;
    response.status = types::ocpp::DataTransferStatus::Accepted;
    response.data = R"({"emergencyACLimit": 32})";

    const auto result = to_ocpp_data_transfer_response(response);
    EXPECT_EQ(result.status, ocpp::v2::DataTransferStatusEnum::Accepted);
    ASSERT_TRUE(result.data.has_value());
    EXPECT_EQ(result.data.value(), nlohmann::json::parse(R"({"emergencyACLimit": 32})"));
}

TEST(DataTransferConversions, response_plain_text_data_is_kept_as_json_string) {
    types::ocpp::DataTransferResponse response;
    response.status = types::ocpp::DataTransferStatus::Accepted;
    response.data = "plain text, not json";

    const auto result = to_ocpp_data_transfer_response(response);
    ASSERT_TRUE(result.data.has_value());
    ASSERT_TRUE(result.data.value().is_string());
    EXPECT_EQ(result.data.value().get<std::string>(), "plain text, not json");
}

TEST(DataTransferConversions, response_invalid_custom_data_is_dropped) {
    types::ocpp::DataTransferResponse response;
    response.status = types::ocpp::DataTransferStatus::Accepted;
    response.custom_data = types::ocpp::CustomData{"Pionix", "not json"};

    const auto result = to_ocpp_data_transfer_response(response);
    EXPECT_FALSE(result.customData.has_value());
}

} // namespace
