// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "stubs/v2_chargepoint_stub.hpp"

#include <ModuleAdapterStub.hpp>
#include <v2_chargepoint.hpp>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace {

using namespace ocpp_multi;
using testing::_;
using testing::NiceMock;
using testing::Return;
using testing::SaveArg;
using testing::Truly;

constexpr const char* TIMESTAMP = "2026-01-01T12:00:00.000Z";
constexpr std::int32_t EVSE_ID = 1;
constexpr std::int32_t CONNECTOR_ID = 1;
constexpr const char* SESSION_ID = "session-uuid-1";

// expose the protected test seams
struct TestChargePointV2 : public ChargePointV2 {
    using ChargePointV2::ChargePointV2;
    using ChargePointV2::configure_callbacks;
    using ChargePointV2::set_charge_point;
};

types::powermeter::Powermeter meter_value_wh(float energy_wh_import) {
    types::powermeter::Powermeter meter;
    meter.timestamp = TIMESTAMP;
    meter.energy_Wh_import.total = energy_wh_import;
    return meter;
}

types::authorization::ProvidedIdToken provided_id_token(const std::string& token,
                                                        const std::optional<std::string>& parent_token = std::nullopt,
                                                        const std::optional<std::int32_t>& request_id = std::nullopt) {
    types::authorization::ProvidedIdToken id_tag;
    id_tag.id_token = {token, types::authorization::IdTokenType::ISO14443};
    id_tag.authorization_type = types::authorization::AuthorizationType::RFID;
    if (parent_token) {
        id_tag.parent_id_token =
            types::authorization::IdToken{parent_token.value(), types::authorization::IdTokenType::ISO14443};
    }
    id_tag.request_id = request_id;
    return id_tag;
}

auto opt_token_eq(const std::string& expected) {
    return Truly([expected](const std::optional<ocpp::v2::IdToken>& token) {
        return token.has_value() && token->idToken.get() == expected;
    });
}

class ChargePointV2Test : public testing::Test {
protected:
    module::stub::QuietModuleAdapterStub m_adapter;
    Requirement m_requirement{"ocpp", 0};
    evse_securityIntf m_security{&m_adapter, m_requirement, "security", std::nullopt};
    NiceMock<stubs::GenericChargePointCallbacksMock> m_callbacks;
    TestChargePointV2 m_chargepoint{m_callbacks, m_security};
    NiceMock<stubs::Ocpp2ChargePointMock>* m_libocpp{nullptr}; // owned by m_chargepoint

    void SetUp() override {
        auto mock = std::make_unique<NiceMock<stubs::Ocpp2ChargePointMock>>();
        m_libocpp = mock.get();
        m_chargepoint.set_charge_point(std::move(mock));
    }

    std::shared_ptr<module::TransactionData> make_transaction_data(ocpp::v2::TriggerReasonEnum trigger_reason,
                                                                   ocpp::v2::ChargingStateEnum charging_state) {
        auto transaction_data = std::make_shared<module::TransactionData>(
            CONNECTOR_ID, SESSION_ID, ocpp::DateTime(TIMESTAMP), trigger_reason, charging_state);
        ON_CALL(m_callbacks, transaction_data(EVSE_ID)).WillByDefault(Return(transaction_data));
        return transaction_data;
    }
};

// TransactionEvent(Started) carries token, group token, reservation and remote start id
TEST_F(ChargePointV2Test, transactionStartedReportsTokenData) {
    make_transaction_data(ocpp::v2::TriggerReasonEnum::CablePluggedIn, ocpp::v2::ChargingStateEnum::EVConnected);

    types::evse_manager::SessionEvent session_event;
    session_event.uuid = SESSION_ID;
    session_event.timestamp = TIMESTAMP;
    session_event.event = types::evse_manager::SessionEventEnum::TransactionStarted;
    session_event.connector_id = CONNECTOR_ID;
    types::evse_manager::TransactionStarted transaction_started;
    transaction_started.id_tag = provided_id_token("TOKEN123", "PARENT456", 77);
    transaction_started.meter_value = meter_value_wh(100.0F);
    transaction_started.reservation_id = 5;
    session_event.transaction_started = transaction_started;

    EXPECT_CALL(m_callbacks, transaction_event(EVSE_ID, module::TxEvent::AUTHORIZED))
        .WillOnce(Return(module::TxEventEffect::START_TRANSACTION));
    EXPECT_CALL(*m_libocpp, on_transaction_started(
                                EVSE_ID, CONNECTOR_ID, SESSION_ID, _, ocpp::v2::TriggerReasonEnum::Authorized, _,
                                opt_token_eq("TOKEN123"), opt_token_eq("PARENT456"), std::optional<std::int32_t>{5},
                                std::optional<std::int32_t>{77}, ocpp::v2::ChargingStateEnum::EVConnected));

    m_chargepoint.on_event_transaction_started(EVSE_ID, CONNECTOR_ID, session_event);
}

// TransactionEvent(Ended) reports the stop reason accumulated by the event handlers
TEST_F(ChargePointV2Test, transactionStopReportsAccumulatedStopReason) {
    make_transaction_data(ocpp::v2::TriggerReasonEnum::ChargingStateChanged, ocpp::v2::ChargingStateEnum::Charging);

    types::evse_manager::SessionEvent session_event;
    session_event.uuid = SESSION_ID;
    session_event.timestamp = TIMESTAMP;
    session_event.event = types::evse_manager::SessionEventEnum::ChargingPausedEV;
    session_event.connector_id = CONNECTOR_ID;
    session_event.charging_state_changed_event = types::evse_manager::ChargingStateChangedEvent{meter_value_wh(200.0F)};

    EXPECT_CALL(m_callbacks, transaction_event(EVSE_ID, module::TxEvent::ENERGY_TRANSFER_STOPPED))
        .WillOnce(Return(module::TxEventEffect::STOP_TRANSACTION));
    EXPECT_CALL(*m_libocpp, on_transaction_finished(EVSE_ID, _, _, ocpp::v2::ReasonEnum::StoppedByEV,
                                                    ocpp::v2::TriggerReasonEnum::ChargingStateChanged, _, _,
                                                    ocpp::v2::ChargingStateEnum::SuspendedEV, _));
    EXPECT_CALL(m_callbacks, transaction_reset(EVSE_ID));

    m_chargepoint.on_event_charging_paused_ev(EVSE_ID, CONNECTOR_ID, session_event);
}

// TransactionEvent(Ended) forwards the transaction-start signed meter value
TEST_F(ChargePointV2Test, transactionStopForwardsStartSignedMeterValue) {
    make_transaction_data(ocpp::v2::TriggerReasonEnum::ChargingStateChanged, ocpp::v2::ChargingStateEnum::Charging);

    types::units_signed::SignedMeterValue start_signed_meter_value;
    start_signed_meter_value.signed_meter_data = "START-OCMF-DATA";
    start_signed_meter_value.signing_method = "ECDSA";
    start_signed_meter_value.encoding_method = "OCMF";

    types::evse_manager::SessionEvent session_event;
    session_event.uuid = SESSION_ID;
    session_event.timestamp = TIMESTAMP;
    session_event.event = types::evse_manager::SessionEventEnum::TransactionFinished;
    session_event.connector_id = CONNECTOR_ID;
    types::evse_manager::TransactionFinished transaction_finished;
    transaction_finished.meter_value = meter_value_wh(300.0F);
    transaction_finished.reason = types::evse_manager::StopTransactionReason::EVDisconnected;
    transaction_finished.start_signed_meter_value = start_signed_meter_value;
    session_event.transaction_finished = transaction_finished;

    EXPECT_CALL(m_callbacks, transaction_event(EVSE_ID, module::TxEvent::EV_DISCONNECTED))
        .WillOnce(Return(module::TxEventEffect::STOP_TRANSACTION));
    EXPECT_CALL(m_callbacks, transaction_event(EVSE_ID, module::TxEvent::DEAUTHORIZED))
        .WillOnce(Return(module::TxEventEffect::NONE));
    EXPECT_CALL(*m_libocpp, on_transaction_finished(EVSE_ID, _, _, ocpp::v2::ReasonEnum::EVDisconnected, _, _, _,
                                                    ocpp::v2::ChargingStateEnum::Idle,
                                                    Truly([](const std::optional<ocpp::v2::SignedMeterValue>& value) {
                                                        return value.has_value() &&
                                                               value->signedMeterData.get() == "START-OCMF-DATA";
                                                    })));
    EXPECT_CALL(m_callbacks, transaction_reset(EVSE_ID));

    m_chargepoint.on_event_transaction_finished(EVSE_ID, CONNECTOR_ID, session_event);
}

// RequestStartTransaction forwards the group id token to the token sink
TEST_F(ChargePointV2Test, remoteStartForwardsGroupIdToken) {
    auto callbacks = m_chargepoint.configure_callbacks();

    ocpp::v2::RequestStartTransactionRequest request;
    request.idToken.idToken = "TOKEN123";
    request.idToken.type = "ISO14443";
    request.groupIdToken = ocpp::v2::IdToken{};
    request.groupIdToken->idToken = "PARENT456";
    request.groupIdToken->type = "ISO14443";
    request.evseId = EVSE_ID;
    request.remoteStartId = 42;

    GenericChargePointCallbacks::IdToken provided;
    EXPECT_CALL(m_callbacks, cb_provide_token(_)).WillOnce(SaveArg<0>(&provided));

    const auto status = callbacks.remote_start_transaction_callback(request, true);

    EXPECT_EQ(status, ocpp::v2::RequestStartStopStatusEnum::Accepted);
    EXPECT_EQ(provided.token.idToken.get(), "TOKEN123");
    EXPECT_FALSE(provided.prevalidated);
    ASSERT_TRUE(provided.group_id_token.has_value());
    EXPECT_EQ(provided.group_id_token->idToken.get(), "PARENT456");
    EXPECT_EQ(provided.evse_id, EVSE_ID);
    EXPECT_EQ(provided.request_id, 42);
}

// SessionStarted(Authorized) creates transaction data with token, group token, reservation and remote start id
TEST_F(ChargePointV2Test, sessionStartedCreatesTransactionData) {
    types::evse_manager::SessionEvent session_event;
    session_event.uuid = SESSION_ID;
    session_event.timestamp = TIMESTAMP;
    session_event.event = types::evse_manager::SessionEventEnum::SessionStarted;
    session_event.connector_id = CONNECTOR_ID;
    types::evse_manager::SessionStarted session_started;
    session_started.reason = types::evse_manager::StartSessionReason::Authorized;
    session_started.meter_value = meter_value_wh(0.0F);
    session_started.id_tag = provided_id_token("TOKEN123", "PARENT456", 77);
    session_started.reservation_id = 5;
    session_event.session_started = session_started;

    std::shared_ptr<module::TransactionData> transaction_data;
    EXPECT_CALL(m_callbacks, transaction_add(EVSE_ID, _)).WillOnce(SaveArg<1>(&transaction_data));
    EXPECT_CALL(m_callbacks, transaction_event(EVSE_ID, module::TxEvent::AUTHORIZED))
        .WillOnce(Return(module::TxEventEffect::NONE));

    m_chargepoint.on_event_session_started(EVSE_ID, CONNECTOR_ID, session_event);

    ASSERT_NE(transaction_data, nullptr);
    EXPECT_EQ(transaction_data->session_id, SESSION_ID);
    EXPECT_EQ(transaction_data->connector_id, CONNECTOR_ID);
    ASSERT_TRUE(transaction_data->id_token.has_value());
    EXPECT_EQ(transaction_data->id_token->idToken.get(), "TOKEN123");
    ASSERT_TRUE(transaction_data->group_id_token.has_value());
    EXPECT_EQ(transaction_data->group_id_token->idToken.get(), "PARENT456");
    EXPECT_EQ(transaction_data->reservation_id, 5);
    EXPECT_EQ(transaction_data->remote_start_id, 77);
}

// a registered variable listener receives changes reported by libocpp
TEST_F(ChargePointV2Test, variableListenerForwardsChanges) {
    stubs::Ocpp2ChargePointMock::variable_listener_t libocpp_listener;
    EXPECT_CALL(*m_libocpp, register_variable_listener(_))
        .WillOnce([&libocpp_listener](stubs::Ocpp2ChargePointMock::variable_listener_t&& listener) {
            libocpp_listener = std::move(listener);
        });

    std::optional<std::string> reported_value;
    m_chargepoint.register_variable_listener({"TxCtrlr"}, {"EVConnectionTimeOut"},
                                             [&reported_value](const ocpp::v2::Component&, const ocpp::v2::Variable&,
                                                               const std::string& value) { reported_value = value; });

    ASSERT_TRUE(libocpp_listener);
    libocpp_listener({}, {"TxCtrlr"}, {"EVConnectionTimeOut"}, {}, {}, "30", "60");

    ASSERT_TRUE(reported_value.has_value());
    EXPECT_EQ(reported_value.value(), "60");
}

// key-only (empty component) get request is passed through unchanged; rejection comes from libocpp
TEST_F(ChargePointV2Test, getVariablesEmptyComponentIsPassedThrough) {
    ocpp::v2::GetVariableData data;
    data.variable.name = "HeartbeatInterval";

    ocpp::v2::GetVariableResult rejected;
    rejected.attributeStatus = ocpp::v2::GetVariableStatusEnum::UnknownComponent;
    rejected.component = data.component;
    rejected.variable = data.variable;

    EXPECT_CALL(*m_libocpp, get_variables(Truly([](const std::vector<ocpp::v2::GetVariableData>& requests) {
        return requests.size() == 1 && requests[0].component.name.get().empty() &&
               requests[0].variable.name.get() == "HeartbeatInterval";
    }))).WillOnce(Return(std::vector<ocpp::v2::GetVariableResult>{rejected}));

    const auto results = m_chargepoint.get_variables({data});

    ASSERT_EQ(results.size(), 1);
    EXPECT_EQ(results[0].attributeStatus, ocpp::v2::GetVariableStatusEnum::UnknownComponent);
    EXPECT_EQ(results[0].component.name.get(), "");
    EXPECT_EQ(results[0].variable.name.get(), "HeartbeatInterval");
}

// key-only (empty component) set request is passed through unchanged; rejection comes from libocpp
TEST_F(ChargePointV2Test, setVariablesEmptyComponentIsPassedThrough) {
    ocpp::v2::SetVariableData data;
    data.attributeValue = "42";
    data.variable.name = "HeartbeatInterval";

    ocpp::v2::SetVariableResult rejected;
    rejected.attributeStatus = ocpp::v2::SetVariableStatusEnum::Rejected;
    rejected.component = data.component;
    rejected.variable = data.variable;

    std::map<ocpp::v2::SetVariableData, ocpp::v2::SetVariableResult> libocpp_result;
    libocpp_result.emplace(data, rejected);
    EXPECT_CALL(*m_libocpp, set_variables(Truly([](const std::vector<ocpp::v2::SetVariableData>& requests) {
                                              return requests.size() == 1 && requests[0].component.name.get().empty() &&
                                                     requests[0].variable.name.get() == "HeartbeatInterval";
                                          }),
                                          "test"))
        .WillOnce(Return(libocpp_result));

    const auto results = m_chargepoint.set_variables({data}, "test");

    ASSERT_EQ(results.size(), 1);
    EXPECT_EQ(results[0].result.attributeStatus, ocpp::v2::SetVariableStatusEnum::Rejected);
    EXPECT_EQ(results[0].result.component.name.get(), "");
    EXPECT_EQ(results[0].result.variable.name.get(), "HeartbeatInterval");
    EXPECT_FALSE(results[0].monitor_value.has_value()); // libocpp owns monitors on the v2 path
}

} // namespace
