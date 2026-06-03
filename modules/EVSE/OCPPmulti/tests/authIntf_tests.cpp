// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

// cmds:
//   set_connection_timeout:
//   set_master_pass_group_id:
//   withdraw_authorization: <not used>
// vars:
//   token_validation_status: <not used>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <generic_ocpp.hpp>

#include "stubs/generic_ocpp_stub.hpp"

namespace {
using namespace stubs;

TEST_F(GenericOcppRequiresTester, callSetConnectionTimeout) {
    // call_set_connection_timeout() used in
    // cb_variable_set
    // ready_module_configuration

    using ocpp::v2::AttributeEnum;

    std::vector<json> received;
    interfaces->subscribe_var("auth", "call_set_connection_timeout",
                              [&received](const auto&, const auto&, const auto& data) { received.push_back(data); });

    ocpp::v2::SetVariableData set_data{"800", {"TxCtrlr"}, {"EVConnectionTimeOut"}};
    // CiString<2500> attributeValue;
    // Component component;
    // Variable variable;
    // std::optional<AttributeEnum> attributeType;
    // std::optional<CustomData> customData;

    ocpp->ready_module_configuration();
    ocpp->cb_variable_set(set_data);

    ASSERT_EQ(received.size(), 2);
    ASSERT_EQ(chargepoint.get_int32(set_data.component, set_data.variable, AttributeEnum::Actual), 60);
    EXPECT_EQ(received[0], R"({"connection_timeout":60})"_json);
    EXPECT_EQ(received[1], R"({"connection_timeout":800})"_json);
}

TEST_F(GenericOcppRequiresTester, callSetMasterPassGroupId) {
    // call_set_master_pass_group_id() used in
    // cb_variable_set
    // ready_module_configuration

    using ocpp::v2::AttributeEnum;

    std::vector<json> received;
    interfaces->subscribe_var("auth", "call_set_master_pass_group_id",
                              [&received](const auto&, const auto&, const auto& data) { received.push_back(data); });

    ocpp::v2::SetVariableData set_data{"Students", {"AuthCtrlr"}, {"MasterPassGroupId"}};
    // CiString<2500> attributeValue;
    // Component component;
    // Variable variable;
    // std::optional<AttributeEnum> attributeType;
    // std::optional<CustomData> customData;

    ocpp->ready_module_configuration();
    ocpp->cb_variable_set(set_data);

    ASSERT_EQ(received.size(), 2);
    ASSERT_EQ(chargepoint.get_string(set_data.component, set_data.variable, AttributeEnum::Actual), "Managers");
    EXPECT_EQ(received[0], R"({"master_pass_group_id":"Managers"})"_json);
    EXPECT_EQ(received[1], R"({"master_pass_group_id":"Students"})"_json);
}

} // namespace
