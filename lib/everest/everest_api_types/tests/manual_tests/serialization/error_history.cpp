// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "everest_api_types/error_history/codec.hpp"
#include "everest_api_types/utilities/codec.hpp"
#include "nlohmann/json.hpp"
#include <gtest/gtest.h>
#include <string>

using namespace everest::lib::API::V1_0::types::error_history;
using namespace everest::lib::API::V1_0::types;

namespace {

ErrorObject make_error_object() {
    ErrorObject error;
    error.type = "evse_board_support/MREC3HighTemperature";
    error.description = "High temperature";
    error.message = "";
    error.severity = Severity::High;
    error.origin.module_id = "bsp_1";
    error.origin.implementation_id = "main";
    error.timestamp = "2026-11-09T10:00:00.000Z";
    error.uuid = "the-uuid";
    error.state = State::Active;
    return error;
}

} // namespace

TEST(error_history, error_object_serializes_vendor_id) {
    auto error = make_error_object();
    error.vendor_id = "com.example";

    const auto serialized = nlohmann::json::parse(serialize(error));
    EXPECT_EQ(serialized.at("vendor_id"), "com.example");

    ErrorObject result;
    ASSERT_TRUE(everest::lib::API::deserialize(serialized.dump(), result));
    EXPECT_EQ(result.vendor_id, "com.example");
}

TEST(error_history, error_object_without_vendor_id_omits_it) {
    const auto serialized = nlohmann::json::parse(serialize(make_error_object()));
    EXPECT_FALSE(serialized.contains("vendor_id"));
}

TEST(error_history, error_object_serializes_trigger_value) {
    auto error = make_error_object();
    error.trigger_value = generic::TriggerValue{"85.3 degC"};

    const auto serialized = nlohmann::json::parse(serialize(error));
    EXPECT_EQ(serialized.at("trigger_value"), nlohmann::json({{"text_repr", "85.3 degC"}}));

    ErrorObject result;
    ASSERT_TRUE(everest::lib::API::deserialize(serialized.dump(), result));
    ASSERT_TRUE(result.trigger_value.has_value());
    EXPECT_EQ(result.trigger_value->text_repr, "85.3 degC");
}

TEST(error_history, error_object_without_trigger_value_omits_it) {
    const auto serialized = nlohmann::json::parse(serialize(make_error_object()));
    EXPECT_FALSE(serialized.contains("trigger_value"));
}
