// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#include "SerializationTestHelpers.hpp"
#include "everest_api_types/yeti_simulator/API.hpp"
#include "everest_api_types/yeti_simulator/codec.hpp"
#include "nlohmann/json.hpp"
#include <gtest/gtest.h>
#include <string>

using namespace everest::lib::API::V1_0::types::yeti_simulator;

TEST(yeti_simulator, RaiseError_minimal_roundtrip) {
    RaiseError original;
    original.type = "evse_board_support/MREC6UnderVoltage";

    auto ser = serialize(original);
    auto des = deserialize<RaiseError>(ser);

    EXPECT_EQ(des.type, original.type);
    EXPECT_FALSE(des.sub_type.has_value());
    EXPECT_FALSE(des.message.has_value());
    EXPECT_FALSE(des.severity.has_value());
}

TEST(yeti_simulator, RaiseError_all_fields_roundtrip) {
    RaiseError original;
    original.type = "evse_board_support/MREC6UnderVoltage";
    original.sub_type.emplace("phase_L1");
    original.message.emplace("Undervoltage detected on L1");
    original.severity.emplace(Severity::High);

    auto ser = serialize(original);
    auto des = deserialize<RaiseError>(ser);

    EXPECT_EQ(des.type, original.type);
    ASSERT_TRUE(des.sub_type.has_value());
    EXPECT_EQ(des.sub_type.value(), original.sub_type.value());
    ASSERT_TRUE(des.message.has_value());
    EXPECT_EQ(des.message.value(), original.message.value());
    ASSERT_TRUE(des.severity.has_value());
    EXPECT_EQ(des.severity.value(), original.severity.value());
}

TEST(yeti_simulator, ClearError_minimal_roundtrip) {
    ClearError original;
    original.type = "evse_board_support/MREC6UnderVoltage";

    auto ser = serialize(original);
    auto des = deserialize<ClearError>(ser);

    EXPECT_EQ(des.type, original.type);
    EXPECT_FALSE(des.sub_type.has_value());
}

TEST(yeti_simulator, ClearError_with_sub_type_roundtrip) {
    ClearError original;
    original.type = "evse_board_support/MREC6UnderVoltage";
    original.sub_type.emplace("phase_L1");

    auto ser = serialize(original);
    auto des = deserialize<ClearError>(ser);

    EXPECT_EQ(des.type, original.type);
    ASSERT_TRUE(des.sub_type.has_value());
    EXPECT_EQ(des.sub_type.value(), original.sub_type.value());
}

TEST(yeti_simulator, Severity_all_values_roundtrip) {
    codec_test_all<Severity>({Severity::Low, Severity::Medium, Severity::High});
}

TEST(yeti_simulator, Severity_unknown_value_throws) {
    EXPECT_THROW(deserialize<Severity>(R"("Unknown")"), std::out_of_range);
}
