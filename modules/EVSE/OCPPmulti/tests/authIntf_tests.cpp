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

types::evse_manager::SessionEvent auth_required() {
    types::evse_manager::SessionEvent session_event;
    session_event.event = types::evse_manager::SessionEventEnum::AuthRequired;
    return session_event;
}

const auto no_authorization_token =
    R"({"authorization_type":"OCPP","connectors":[1],"id_token":{"type":"NoAuthorization","value":""},"prevalidated":true})"_json;

std::vector<ocpp::v2::GetVariableResult> auth_ctrlr_enabled(const std::string& value) {
    ocpp::v2::GetVariableResult auth_enabled;
    auth_enabled.attributeStatus = ocpp::v2::GetVariableStatusEnum::Accepted;
    auth_enabled.component = {"AuthCtrlr"};
    auth_enabled.variable = {"Enabled"};
    auth_enabled.attributeValue = value;
    return {auth_enabled};
}

TEST_F(GenericOcppRequiresTester, authEnabledIsSeededFromDeviceModel) {
    using ::testing::_;
    using ::testing::Return;

    EXPECT_CALL(chargepoint, get_variables(_)).WillOnce(Return(auth_ctrlr_enabled("false")));

    std::vector<json> received;
    interfaces->subscribe_var("auth_provider", "provided_token",
                              [&received](const auto&, const auto&, const auto& data) { received.push_back(data); });

    ocpp->ready_module_configuration();
    ocpp->cb_session_event(1, auth_required());

    ASSERT_EQ(received.size(), 1);
    EXPECT_EQ(received[0], no_authorization_token);
}

TEST_F(GenericOcppRequiresTester, missingAuthEnabledCountsAsEnabled) {
    using ::testing::_;
    using ::testing::Return;

    ocpp::v2::GetVariableResult auth_enabled;
    auth_enabled.attributeStatus = ocpp::v2::GetVariableStatusEnum::UnknownVariable;
    auth_enabled.component = {"AuthCtrlr"};
    auth_enabled.variable = {"Enabled"};
    EXPECT_CALL(chargepoint, get_variables(_)).WillOnce(Return(std::vector<ocpp::v2::GetVariableResult>{auth_enabled}));

    std::vector<json> received;
    interfaces->subscribe_var("auth_provider", "provided_token",
                              [&received](const auto&, const auto&, const auto& data) { received.push_back(data); });

    ocpp->ready_module_configuration();
    ocpp->cb_session_event(1, auth_required());
    EXPECT_TRUE(received.empty());
}

TEST_F(GenericOcppRequiresTester, emptyAuthEnabledCountsAsEnabled) {
    using ::testing::_;
    using ::testing::Return;

    EXPECT_CALL(chargepoint, get_variables(_)).WillOnce(Return(auth_ctrlr_enabled("")));

    std::vector<json> received;
    interfaces->subscribe_var("auth_provider", "provided_token",
                              [&received](const auto&, const auto&, const auto& data) { received.push_back(data); });

    ocpp->ready_module_configuration();
    ocpp->cb_session_event(1, auth_required());
    EXPECT_TRUE(received.empty());
}

TEST_F(GenericOcppRequiresTester, authEnabledTargetIsIgnored) {
    std::vector<json> received;
    interfaces->subscribe_var("auth_provider", "provided_token",
                              [&received](const auto&, const auto&, const auto& data) { received.push_back(data); });

    ocpp->cb_variable_set({"true", {"AuthCtrlr"}, {"Enabled"}});
    ocpp->cb_variable_set({"false", {"AuthCtrlr"}, {"Enabled"}, ocpp::v2::AttributeEnum::Target});
    ocpp->cb_session_event(1, auth_required());
    EXPECT_TRUE(received.empty());

    ocpp->cb_variable_set({"false", {"AuthCtrlr"}, {"Enabled"}, ocpp::v2::AttributeEnum::Actual});
    EXPECT_EQ(received.size(), 1);
}

TEST_F(GenericOcppRequiresTester, authDisabledAuthorizesWaitingSession) {
    std::vector<json> received;
    interfaces->subscribe_var("auth_provider", "provided_token",
                              [&received](const auto&, const auto&, const auto& data) { received.push_back(data); });

    ocpp->cb_variable_set({"true", {"AuthCtrlr"}, {"Enabled"}});
    ocpp->cb_session_event(1, auth_required());
    EXPECT_TRUE(received.empty());

    // the EVSE that already waits is authorised when authorisation is switched off
    ocpp->cb_variable_set({"false", {"AuthCtrlr"}, {"Enabled"}});
    ASSERT_EQ(received.size(), 1);
    EXPECT_EQ(received[0], no_authorization_token);

    ocpp->cb_variable_set({"true", {"AuthCtrlr"}, {"Enabled"}});
    ocpp->cb_session_event(1, auth_required());
    EXPECT_EQ(received.size(), 1);
}

TEST_F(GenericOcppRequiresTester, noTokenWhileOcppIsStopped) {
    using ::testing::Return;
    EXPECT_CALL(chargepoint, stop());
    EXPECT_CALL(chargepoint, restart()).WillOnce(Return(true));

    std::vector<json> received;
    interfaces->subscribe_var("auth_provider", "provided_token",
                              [&received](const auto&, const auto&, const auto& data) { received.push_back(data); });

    ocpp->cb_variable_set({"false", {"AuthCtrlr"}, {"Enabled"}});
    ASSERT_TRUE(ocpp->handle_stop());
    ocpp->cb_session_event(1, auth_required());
    EXPECT_TRUE(received.empty());

    ASSERT_TRUE(ocpp->handle_restart());
    ASSERT_EQ(received.size(), 1);
    EXPECT_EQ(received.at(0).at("connectors"), json::array({1}));

    ocpp->cb_session_event(2, auth_required());
    EXPECT_EQ(received.size(), 2);
}

TEST_F(GenericOcppRequiresTester, noTokenWhileShuttingDown) {
    std::vector<json> received;
    interfaces->subscribe_var("auth_provider", "provided_token",
                              [&received](const auto&, const auto&, const auto& data) { received.push_back(data); });

    ocpp->cb_variable_set({"false", {"AuthCtrlr"}, {"Enabled"}});
    ocpp->shutdown();
    ocpp->cb_session_event(1, auth_required());
    EXPECT_TRUE(received.empty());
}

} // namespace
