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

// component, variable and value of each write, e.g. "AuthCtrlr.Enabled=false"
using Writes = std::vector<std::string>;

std::vector<ocpp_multi::SetVariableOutcome> record_writes(Writes& writes,
                                                          const std::vector<ocpp::v2::SetVariableData>& data) {
    std::vector<ocpp_multi::SetVariableOutcome> outcomes;
    for (const auto& entry : data) {
        writes.push_back(entry.component.name.get() + entry.component.instance.value_or("").get() + "." +
                         entry.variable.name.get() + "=" + entry.attributeValue.get());
        ocpp_multi::SetVariableOutcome outcome;
        outcome.result.component = entry.component;
        outcome.result.variable = entry.variable;
        outcome.result.attributeStatus = ocpp::v2::SetVariableStatusEnum::Accepted;
        outcomes.push_back(outcome);
    }
    return outcomes;
}

TEST_F(GenericOcppRequiresTester, authEnabledIsSeededFromDeviceModel) {
    using ::testing::_;
    using ::testing::Return;

    EXPECT_CALL(chargepoint, get_variables(_)).WillOnce(Return(auth_ctrlr_enabled("false")));
    // the module parameter is overwritten with the device model value
    Writes writes;
    EXPECT_CALL(chargepoint, set_variables(_, "internal")).WillRepeatedly([&writes](const auto& data, const auto&) {
        return record_writes(writes, data);
    });

    std::vector<json> received;
    interfaces->subscribe_var("auth_provider", "provided_token",
                              [&received](const auto&, const auto&, const auto& data) { received.push_back(data); });

    ocpp->ready_module_configuration();
    ocpp->cb_session_event(1, auth_required());

    ASSERT_EQ(received.size(), 1);
    EXPECT_EQ(received[0], no_authorization_token);

    ocpp->wait_for_device_model_writes();
    EXPECT_EQ(writes, (Writes{"AuthCtrlr.Enabled=false", "nameocpp.AuthEnabled=false"}));
}

TEST_F(GenericOcppRequiresTester, authEnabledParameterSetsAuthCtrlrEnabled) {
    using ::testing::_;
    using ::testing::Return;

    EXPECT_CALL(chargepoint, get_variables(_)).WillOnce(Return(auth_ctrlr_enabled("true")));
    ocpp->ready_module_configuration();

    Writes writes;
    EXPECT_CALL(chargepoint, set_variables(_, "internal")).WillRepeatedly([&writes](const auto& data, const auto&) {
        return record_writes(writes, data);
    });
    std::vector<json> received;
    interfaces->subscribe_var("auth_provider", "provided_token",
                              [&received](const auto&, const auto&, const auto& data) { received.push_back(data); });

    EXPECT_EQ(ocpp->on_auth_enabled_config_changed(false).status, Everest::config::SetResponseStatus::Accepted);
    ocpp->cb_session_event(1, auth_required());
    ocpp->wait_for_device_model_writes();
    EXPECT_EQ(writes, (Writes{"AuthCtrlr.Enabled=false", "nameocpp.AuthEnabled=false"}));
    // free charging waits for AuthCtrlr.Enabled to be written, so libocpp already treats the transaction as unvalidated
    EXPECT_TRUE(received.empty());

    // the write reports AuthCtrlr.Enabled back, which authorises the waiting EVSE and writes nothing more, nor does
    // the echo of the parameter
    ocpp->cb_variable_set({"false", {"AuthCtrlr"}, {"Enabled"}});
    EXPECT_EQ(received.size(), 1);
    EXPECT_EQ(ocpp->on_auth_enabled_config_changed(false).status, Everest::config::SetResponseStatus::Accepted);
    ocpp->wait_for_device_model_writes();
    EXPECT_EQ(writes.size(), 2);
}

TEST_F(GenericOcppRequiresTester, authCtrlrEnabledIsMirroredToParameter) {
    using ::testing::_;
    using ::testing::Return;

    EXPECT_CALL(chargepoint, get_variables(_)).WillOnce(Return(auth_ctrlr_enabled("true")));
    ocpp->ready_module_configuration();

    Writes writes;
    EXPECT_CALL(chargepoint, set_variables(_, "internal")).WillRepeatedly([&writes](const auto& data, const auto&) {
        return record_writes(writes, data);
    });

    ocpp->cb_variable_set({"false", {"AuthCtrlr"}, {"Enabled"}});
    ocpp->wait_for_device_model_writes();
    EXPECT_EQ(writes, (Writes{"AuthCtrlr.Enabled=false", "nameocpp.AuthEnabled=false"}));

    // the config service forwards the write back to the module
    EXPECT_EQ(ocpp->on_auth_enabled_config_changed(false).status, Everest::config::SetResponseStatus::Accepted);
    ocpp->cb_variable_set({"false", {"AuthCtrlr"}, {"Enabled"}});
    ocpp->wait_for_device_model_writes();
    EXPECT_EQ(writes.size(), 2);
}

TEST_F(GenericOcppRequiresTester, concurrentAuthEnabledChangesConverge) {
    using ::testing::_;
    using ::testing::Return;

    EXPECT_CALL(chargepoint, get_variables(_)).WillOnce(Return(auth_ctrlr_enabled("true")));
    ocpp->ready_module_configuration();

    Writes writes;
    // reports every write back, like libocpp does for AuthCtrlr.Enabled and the config service for the parameter
    EXPECT_CALL(chargepoint, set_variables(_, "internal"))
        .WillRepeatedly([this, &writes](const auto& data, const auto&) {
            auto outcomes = record_writes(writes, data);
            for (const auto& entry : data) {
                if (entry.component.name.get() == "AuthCtrlr") {
                    ocpp->cb_variable_set(entry);
                } else {
                    ocpp->on_auth_enabled_config_changed(ocpp::conversions::string_to_bool(entry.attributeValue.get()));
                }
            }
            return outcomes;
        });

    // the CSMS switches authorisation off while the parameter is switched back on locally
    ocpp->cb_variable_set({"false", {"AuthCtrlr"}, {"Enabled"}});
    EXPECT_EQ(ocpp->on_auth_enabled_config_changed(true).status, Everest::config::SetResponseStatus::Accepted);
    ocpp->wait_for_device_model_writes();
    ocpp->wait_for_device_model_writes();

    ASSERT_GE(writes.size(), 2);
    EXPECT_LE(writes.size(), 4);
    EXPECT_EQ(writes.at(writes.size() - 2), "AuthCtrlr.Enabled=true");
    EXPECT_EQ(writes.back(), "nameocpp.AuthEnabled=true");
}

TEST_F(GenericOcppRequiresTester, authEnabledParameterFollowsAuthCtrlrEnabledWhenItsWriteFails) {
    using ::testing::_;
    using ::testing::Return;

    EXPECT_CALL(chargepoint, get_variables(_)).Times(2).WillRepeatedly(Return(auth_ctrlr_enabled("true")));
    ocpp->ready_module_configuration();

    Writes writes;
    EXPECT_CALL(chargepoint, set_variables(_, "internal")).WillRepeatedly([&writes](const auto& data, const auto&) {
        auto outcomes = record_writes(writes, data);
        if (data.front().component.name.get() == "AuthCtrlr") {
            outcomes.front().result.attributeStatus = ocpp::v2::SetVariableStatusEnum::Rejected;
        }
        return outcomes;
    });
    std::vector<json> received;
    interfaces->subscribe_var("auth_provider", "provided_token",
                              [&received](const auto&, const auto&, const auto& data) { received.push_back(data); });

    EXPECT_EQ(ocpp->on_auth_enabled_config_changed(false).status, Everest::config::SetResponseStatus::Accepted);
    ocpp->wait_for_device_model_writes();
    // the parameter takes the value of AuthCtrlr.Enabled back, and free charging stays off
    EXPECT_EQ(writes, (Writes{"AuthCtrlr.Enabled=false", "nameocpp.AuthEnabled=true"}));
    ocpp->cb_session_event(1, auth_required());
    EXPECT_TRUE(received.empty());

    // the echo of the parameter write finds the mirror at that value and writes nothing more
    EXPECT_EQ(ocpp->on_auth_enabled_config_changed(true).status, Everest::config::SetResponseStatus::Accepted);
    ocpp->wait_for_device_model_writes();
    EXPECT_EQ(writes.size(), 2);
}

TEST_F(GenericOcppRequiresTester, authEnabledParameterRejectedWithoutAuthCtrlrEnabled) {
    using ::testing::_;
    EXPECT_CALL(chargepoint, set_variables(_, _)).Times(0);

    // the default device model of the fixture does not report AuthCtrlr.Enabled, like the OCPP 1.6 JSON backend
    EXPECT_EQ(ocpp->on_auth_enabled_config_changed(false).status, Everest::config::SetResponseStatus::Rejected);
    ocpp->cb_variable_set({"false", {"AuthCtrlr"}, {"Enabled"}});
    ocpp->wait_for_device_model_writes();
}

TEST_F(GenericOcppNotStartedTester, authEnabledParameterRejectedBeforeOcppRuns) {
    EXPECT_EQ(ocpp->on_auth_enabled_config_changed(false).status, Everest::config::SetResponseStatus::Rejected);
    start();
}

TEST_F(GenericOcppRequiresTester, emptyAuthEnabledCountsAsEnabled) {
    using ::testing::_;
    using ::testing::Return;

    ocpp::v2::GetVariableResult auth_enabled;
    auth_enabled.attributeStatus = ocpp::v2::GetVariableStatusEnum::Accepted;
    auth_enabled.component = {"AuthCtrlr"};
    auth_enabled.variable = {"Enabled"};
    auth_enabled.attributeValue = "";
    EXPECT_CALL(chargepoint, get_variables(_)).WillOnce(Return(std::vector<ocpp::v2::GetVariableResult>{auth_enabled}));

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
