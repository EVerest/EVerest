// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <ocpp/v2/functional_blocks/remote_transaction_control.hpp>

#include <ocpp/v2/connector.hpp>
#include <ocpp/v2/ctrlr_component_variables.hpp>
#include <ocpp/v2/device_model.hpp>
#include <ocpp/v2/functional_blocks/availability.hpp>
#include <ocpp/v2/functional_blocks/firmware_update.hpp>
#include <ocpp/v2/functional_blocks/functional_block_context.hpp>
#include <ocpp/v2/functional_blocks/provisioning.hpp>
#include <ocpp/v2/functional_blocks/security.hpp>
#include <ocpp/v2/functional_blocks/transaction.hpp>

#include <ocpp/v2/messages/Get15118EVCertificate.hpp>
#include <ocpp/v2/messages/RequestStartTransaction.hpp>
#include <ocpp/v2/messages/UnlockConnector.hpp>

#include "component_state_manager_mock.hpp"
#include "connectivity_manager_mock.hpp"
#include "device_model_test_helper.hpp"
#include "evse_manager_fake.hpp"
#include "evse_mock.hpp"
#include "evse_security_mock.hpp"
#include "message_dispatcher_mock.hpp"
#include "meter_values_mock.hpp"
#include "mocks/database_handler_mock.hpp"
#include "smart_charging_mock.hpp"

using namespace ocpp::v2;
using testing::_;
using testing::Invoke;
using testing::MockFunction;
using testing::NiceMock;
using testing::Return;

namespace {
class AvailabilityMock : public AvailabilityInterface {
public:
    MOCK_METHOD(void, handle_message, (const ocpp::EnhancedMessage<MessageType>&), (override));
    MOCK_METHOD(void, status_notification_req, (std::int32_t, std::int32_t, ConnectorStatusEnum, bool), (override));
    MOCK_METHOD(void, heartbeat_req, (bool), (override));
    MOCK_METHOD(void, handle_scheduled_change_availability_requests, (std::int32_t), (override));
    MOCK_METHOD(void, set_scheduled_change_availability_requests, (std::int32_t, AvailabilityChange), (override));
    MOCK_METHOD(void, set_heartbeat_timer_interval, (const std::chrono::seconds&), (override));
    MOCK_METHOD(void, stop_heartbeat_timer, (), (override));
    MOCK_METHOD(ChangeAvailabilityResponse, change_availability_req, (bool&, const ChangeAvailabilityRequest&),
                (override));
    MOCK_METHOD(void, action_change_availability_req,
                (bool, const ChangeAvailabilityRequest&, const ChangeAvailabilityResponse&), (override));
};

class SecurityMock : public SecurityInterface {
public:
    MOCK_METHOD(void, handle_message, (const ocpp::EnhancedMessage<MessageType>&), (override));
    MOCK_METHOD(void, security_event_notification_req,
                (const ocpp::CiString<50>&, const std::optional<ocpp::CiString<255>>&, bool, bool,
                 const std::optional<ocpp::DateTime>&),
                (override));
    MOCK_METHOD(void, sign_certificate_req, (const ocpp::CertificateSigningUseEnum&, bool), (override));
    MOCK_METHOD(void, stop_certificate_signed_timer, (), (override));
    MOCK_METHOD(void, init_certificate_expiration_check_timers, (), (override));
    MOCK_METHOD(void, stop_certificate_expiration_check_timers, (), (override));
    MOCK_METHOD(Get15118EVCertificateResponse, on_get_15118_ev_certificate_request,
                (const Get15118EVCertificateRequest&), (override));
};

class TransactionBlockMock : public TransactionInterface {
public:
    MOCK_METHOD(void, handle_message, (const ocpp::EnhancedMessage<MessageType>&), (override));
    MOCK_METHOD(void, on_transaction_started,
                (std::int32_t, std::int32_t, const std::string&, const ocpp::DateTime&, TriggerReasonEnum,
                 const MeterValue&, const std::optional<IdToken>&, const std::optional<IdToken>&,
                 const std::optional<std::int32_t>&, const std::optional<std::int32_t>&, ChargingStateEnum),
                (override));
    MOCK_METHOD(void, on_transaction_finished,
                (std::int32_t, const ocpp::DateTime&, const MeterValue&, ReasonEnum, TriggerReasonEnum,
                 const std::optional<IdToken>&, const std::optional<std::string>&, ChargingStateEnum),
                (override));
    MOCK_METHOD(void, transaction_event_req,
                (const TransactionEventEnum&, const ocpp::DateTime&, const Transaction&, const TriggerReasonEnum&,
                 std::int32_t, const std::optional<std::int32_t>&, const std::optional<EVSE>&,
                 const std::optional<IdToken>&, const std::optional<std::vector<MeterValue>>&,
                 const std::optional<std::int32_t>&, bool, const std::optional<std::int32_t>&, bool),
                (override));
    MOCK_METHOD(void, set_remote_start_id_for_evse, (std::int32_t, IdToken, std::int32_t), (override));
    MOCK_METHOD(void, schedule_reset, (std::optional<std::int32_t>), (override));
};

class FirmwareUpdateMock : public FirmwareUpdateInterface {
public:
    MOCK_METHOD(void, handle_message, (const ocpp::EnhancedMessage<MessageType>&), (override));
    MOCK_METHOD(void, on_firmware_update_status_notification, (std::int32_t, const FirmwareStatusEnum&, const bool),
                (override));
    MOCK_METHOD(void, on_firmware_status_notification_request, (), (override));
};

class ProvisioningMock : public ProvisioningInterface {
public:
    MOCK_METHOD(void, handle_message, (const ocpp::EnhancedMessage<MessageType>&), (override));
    MOCK_METHOD(void, boot_notification_req, (const BootReasonEnum&, bool), (override));
    MOCK_METHOD(void, stop_bootnotification_timer, (), (override));
    MOCK_METHOD(void, on_variable_changed, (const SetVariableData&), (override));
    MOCK_METHOD(std::vector<GetVariableResult>, get_variables, (const std::vector<GetVariableData>&), (override));
    MOCK_METHOD((std::map<SetVariableData, SetVariableResult>), set_variables,
                (const std::vector<SetVariableData>&, const std::string&), (override));
};
} // namespace

class RemoteTransactionControlTest : public ::testing::Test {
protected: // Members
    DeviceModelTestHelper device_model_test_helper;
    NiceMock<MockMessageDispatcher> mock_dispatcher;
    DeviceModel* device_model;
    NiceMock<ConnectivityManagerMock> connectivity_manager;
    NiceMock<DatabaseHandlerMock> database_handler_mock;
    ocpp::EvseSecurityMock evse_security;
    EvseManagerFake evse_manager;
    std::shared_ptr<NiceMock<ComponentStateManagerMock>> component_state_manager;
    std::atomic<ocpp::OcppProtocolVersion> ocpp_version;
    FunctionalBlockContext functional_block_context;

    NiceMock<TransactionBlockMock> transaction;
    NiceMock<SmartChargingMock> smart_charging;
    NiceMock<MeterValuesMock> meter_values;
    NiceMock<AvailabilityMock> availability;
    NiceMock<FirmwareUpdateMock> firmware_update;
    NiceMock<SecurityMock> security;
    NiceMock<ProvisioningMock> provisioning;

    MockFunction<UnlockConnectorResponse(const std::int32_t evse_id, const std::int32_t connector_id)>
        unlock_connector_callback;
    MockFunction<RequestStartStopStatusEnum(const RequestStartTransactionRequest& request,
                                            const bool authorize_remote_start)>
        remote_start_transaction_callback;
    MockFunction<RequestStartStopStatusEnum(const std::int32_t evse_id, const ReasonEnum& stop_reason)>
        stop_transaction_callback;

    std::atomic<RegistrationStatusEnum> registration_status;
    std::atomic<UploadLogStatusEnum> upload_log_status;
    std::atomic<std::int32_t> upload_log_status_id;

    // One connector per evse, owned here because EvseMock::get_connector returns a raw pointer.
    Connector connector_evse_1;
    Connector connector_evse_2;

    std::unique_ptr<RemoteTransactionControl> remote_transaction_control;

protected: // Functions
    RemoteTransactionControlTest() :
        device_model_test_helper(),
        mock_dispatcher(),
        device_model(device_model_test_helper.get_device_model()),
        connectivity_manager(),
        database_handler_mock(),
        evse_security(),
        evse_manager(2),
        component_state_manager(std::make_shared<NiceMock<ComponentStateManagerMock>>()),
        ocpp_version(ocpp::OcppProtocolVersion::v201),
        functional_block_context{
            this->mock_dispatcher,       *this->device_model, this->connectivity_manager,     this->evse_manager,
            this->database_handler_mock, this->evse_security, *this->component_state_manager, this->ocpp_version},
        registration_status(RegistrationStatusEnum::Accepted),
        upload_log_status(UploadLogStatusEnum::Idle),
        upload_log_status_id(0),
        connector_evse_1(1, 1, component_state_manager),
        connector_evse_2(2, 1, component_state_manager),
        remote_transaction_control(std::make_unique<RemoteTransactionControl>(
            functional_block_context, transaction, smart_charging, meter_values, availability, firmware_update,
            security, nullptr, provisioning, unlock_connector_callback.AsStdFunction(),
            remote_start_transaction_callback.AsStdFunction(), stop_transaction_callback.AsStdFunction(),
            registration_status, upload_log_status, upload_log_status_id)) {
        set_evse_connector_status(1, ConnectorStatusEnum::Available);
        set_evse_connector_status(2, ConnectorStatusEnum::Available);
    }

    void set_evse_connector_status(const std::int32_t evse_id, const ConnectorStatusEnum status) {
        auto& evse_mock = evse_manager.get_mock(evse_id);
        ON_CALL(evse_mock, get_number_of_connectors()).WillByDefault(Return(1));
        ON_CALL(evse_mock, get_connector(1))
            .WillByDefault(Return(evse_id == 1 ? &this->connector_evse_1 : &this->connector_evse_2));
        ON_CALL(*component_state_manager, get_connector_effective_status(evse_id, 1)).WillByDefault(Return(status));
    }

    void set_evse_occupied(const std::int32_t evse_id) {
        ON_CALL(evse_manager.get_mock(evse_id), has_active_transaction()).WillByDefault(Return(true));
    }

    void set_smart_charging_enabled(const bool enabled) {
        const auto& cv = ControllerComponentVariables::SmartChargingCtrlrEnabled;
        this->device_model->set_value(cv.component, cv.variable.value(), AttributeEnum::Actual,
                                      enabled ? "true" : "false", "test", true);
    }

    ocpp::EnhancedMessage<MessageType>
    create_remote_start_request(const std::optional<std::int32_t> evse_id,
                                const std::optional<ChargingProfile>& charging_profile = std::nullopt) {
        RequestStartTransactionRequest request;
        request.evseId = evse_id;
        request.remoteStartId = 123;
        request.idToken.idToken = "REMOTE_TOKEN";
        request.idToken.type = IdTokenEnumStringType::Central;
        request.chargingProfile = charging_profile;

        ocpp::Call<RequestStartTransactionRequest> call(request);
        ocpp::EnhancedMessage<MessageType> enhanced_message;
        enhanced_message.messageType = MessageType::RequestStartTransaction;
        enhanced_message.message = call;
        return enhanced_message;
    }

    ChargingProfile create_tx_profile() {
        ChargingSchedulePeriod period;
        period.startPeriod = 0;
        period.limit = 16.0f;

        ChargingSchedule schedule;
        schedule.id = 1;
        schedule.chargingRateUnit = ChargingRateUnitEnum::A;
        schedule.chargingSchedulePeriod = {period};

        ChargingProfile profile;
        profile.id = 1;
        profile.stackLevel = 1;
        profile.chargingProfilePurpose = ChargingProfilePurposeEnum::TxProfile;
        profile.chargingProfileKind = ChargingProfileKindEnum::Relative;
        profile.chargingSchedule = {schedule};
        return profile;
    }

    void expect_response_status(const RequestStartStopStatusEnum status) {
        EXPECT_CALL(mock_dispatcher, dispatch_call_result(_)).WillOnce(Invoke([status](const json& call_result) {
            const auto response = call_result[ocpp::CALLRESULT_PAYLOAD].get<RequestStartTransactionResponse>();
            EXPECT_EQ(response.status, status);
        }));
    }
};

TEST_F(RemoteTransactionControlTest, RemoteStartWithoutEvseIdAcceptedWhenEvseAvailable) {
    // Evse 1 is occupied, evse 2 is free: the request must be accepted and the remote start id must
    // be registered for evse id 0, meaning 'all evse ids'.
    set_evse_occupied(1);

    EXPECT_CALL(transaction, set_remote_start_id_for_evse(0, _, 123));
    EXPECT_CALL(remote_start_transaction_callback, Call(_, _))
        .WillOnce(Invoke([](const RequestStartTransactionRequest& request, bool /*authorize_remote_start*/) {
            EXPECT_FALSE(request.evseId.has_value());
            EXPECT_EQ(request.idToken.idToken.get(), "REMOTE_TOKEN");
            return RequestStartStopStatusEnum::Accepted;
        }));
    expect_response_status(RequestStartStopStatusEnum::Accepted);

    remote_transaction_control->handle_message(create_remote_start_request(std::nullopt));
}

TEST_F(RemoteTransactionControlTest, RemoteStartWithoutEvseIdRejectedWhenAllEvsesOccupied) {
    set_evse_occupied(1);
    set_evse_occupied(2);

    EXPECT_CALL(transaction, set_remote_start_id_for_evse(_, _, _)).Times(0);
    EXPECT_CALL(remote_start_transaction_callback, Call(_, _)).Times(0);
    expect_response_status(RequestStartStopStatusEnum::Rejected);

    remote_transaction_control->handle_message(create_remote_start_request(std::nullopt));
}

TEST_F(RemoteTransactionControlTest, RemoteStartWithoutEvseIdRejectedWhenAllConnectorsUnavailable) {
    set_evse_connector_status(1, ConnectorStatusEnum::Unavailable);
    set_evse_connector_status(2, ConnectorStatusEnum::Faulted);

    EXPECT_CALL(transaction, set_remote_start_id_for_evse(_, _, _)).Times(0);
    EXPECT_CALL(remote_start_transaction_callback, Call(_, _)).Times(0);
    expect_response_status(RequestStartStopStatusEnum::Rejected);

    remote_transaction_control->handle_message(create_remote_start_request(std::nullopt));
}

TEST_F(RemoteTransactionControlTest, RemoteStartWithoutEvseIdRejectedWithProfileWhenSmartChargingEnabled) {
    // A charging profile can not be validated and applied without knowing the evse.
    set_smart_charging_enabled(true);

    EXPECT_CALL(transaction, set_remote_start_id_for_evse(_, _, _)).Times(0);
    EXPECT_CALL(remote_start_transaction_callback, Call(_, _)).Times(0);
    expect_response_status(RequestStartStopStatusEnum::Rejected);

    remote_transaction_control->handle_message(create_remote_start_request(std::nullopt, create_tx_profile()));
}

TEST_F(RemoteTransactionControlTest, RemoteStartWithoutEvseIdAcceptedWithProfileWhenSmartChargingDisabled) {
    // With smart charging disabled the profile is ignored, like in the branch with an evse id.
    set_smart_charging_enabled(false);

    EXPECT_CALL(transaction, set_remote_start_id_for_evse(0, _, 123));
    EXPECT_CALL(remote_start_transaction_callback, Call(_, _)).WillOnce(Return(RequestStartStopStatusEnum::Accepted));
    expect_response_status(RequestStartStopStatusEnum::Accepted);

    remote_transaction_control->handle_message(create_remote_start_request(std::nullopt, create_tx_profile()));
}

TEST_F(RemoteTransactionControlTest, RemoteStartWithEvseIdAcceptedForSpecificEvse) {
    // Regression test for the existing path: with an evse id, the remote start id is registered for
    // that specific evse.
    EXPECT_CALL(transaction, set_remote_start_id_for_evse(2, _, 123));
    EXPECT_CALL(remote_start_transaction_callback, Call(_, _))
        .WillOnce(Invoke([](const RequestStartTransactionRequest& request, bool /*authorize_remote_start*/) {
            EXPECT_EQ(request.evseId, 2);
            return RequestStartStopStatusEnum::Accepted;
        }));
    expect_response_status(RequestStartStopStatusEnum::Accepted);

    remote_transaction_control->handle_message(create_remote_start_request(2));
}
