// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <atomic>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <ocpp/v2/ctrlr_component_variables.hpp>
#include <ocpp/v2/device_model.hpp>
#include <ocpp/v2/functional_blocks/availability.hpp>
#include <ocpp/v2/functional_blocks/firmware_update.hpp>
#include <ocpp/v2/functional_blocks/functional_block_context.hpp>
#include <ocpp/v2/functional_blocks/provisioning.hpp>
#include <ocpp/v2/functional_blocks/remote_transaction_control.hpp>
#include <ocpp/v2/functional_blocks/security.hpp>
#include <ocpp/v2/functional_blocks/smart_charging.hpp>
#include <ocpp/v2/functional_blocks/transaction.hpp>

#include <ocpp/v2/messages/GetCompositeSchedule.hpp>
#include <ocpp/v2/messages/NotifyEVChargingNeeds.hpp>
#include <ocpp/v2/messages/SetChargingProfile.hpp>
#include <ocpp/v2/messages/SignCertificate.hpp>
#include <ocpp/v2/messages/TriggerMessage.hpp>

#include "component_state_manager_mock.hpp"
#include "connectivity_manager_mock.hpp"
#include "device_model_test_helper.hpp"
#include "evse_manager_fake.hpp"
#include "evse_security_mock.hpp"
#include "message_dispatcher_mock.hpp"
#include "meter_values_mock.hpp"
#include "mocks/database_handler_mock.hpp"
#include "ocsp_updater_mock.hpp"

using namespace ocpp;
using namespace ocpp::v2;
using ::testing::_;
using ::testing::Invoke;
using ::testing::MockFunction;
using ::testing::NiceMock;
using ::testing::Return;

namespace {

class TransactionMock : public TransactionInterface {
public:
    MOCK_METHOD(void, handle_message, (const ocpp::EnhancedMessage<MessageType>& message), (override));
    MOCK_METHOD(void, on_transaction_started,
                (const std::int32_t evse_id, const std::int32_t connector_id, const std::string& session_id,
                 const DateTime& timestamp, const TriggerReasonEnum trigger_reason, const MeterValue& meter_start,
                 const std::optional<IdToken>& id_token, const std::optional<IdToken>& group_id_token,
                 const std::optional<std::int32_t>& reservation_id, const std::optional<std::int32_t>& remote_start_id,
                 const ChargingStateEnum charging_state),
                (override));
    MOCK_METHOD(void, on_transaction_finished,
                (const std::int32_t evse_id, const DateTime& timestamp, const MeterValue& meter_stop,
                 const ReasonEnum reason, const TriggerReasonEnum trigger_reason,
                 const std::optional<IdToken>& id_token, const std::optional<std::string>& signed_meter_value,
                 const ChargingStateEnum charging_state,
                 const std::optional<SignedMeterValue>& start_signed_meter_value),
                (override));
    MOCK_METHOD(void, transaction_event_req,
                (const TransactionEventEnum& event_type, const DateTime& timestamp, const Transaction& transaction,
                 const TriggerReasonEnum& trigger_reason, const std::int32_t seq_no,
                 const std::optional<std::int32_t>& cable_max_current, const std::optional<EVSE>& evse,
                 const std::optional<IdToken>& id_token, const std::optional<std::vector<MeterValue>>& meter_value,
                 const std::optional<std::int32_t>& number_of_phases_used, const bool offline,
                 const std::optional<std::int32_t>& reservation_id, const bool initiated_by_trigger_message),
                (override));
    MOCK_METHOD(void, set_remote_start_id_for_evse,
                (const std::int32_t evse_id, const IdToken id_token, const std::int32_t remote_start_id), (override));
    MOCK_METHOD(bool, is_id_token_awaiting_remote_start, (const IdToken& id_token), (const, override));
    MOCK_METHOD(void, schedule_reset, (const std::optional<std::int32_t> reset_scheduled_evseid), (override));
};

class AvailabilityMock : public AvailabilityInterface {
public:
    MOCK_METHOD(void, handle_message, (const ocpp::EnhancedMessage<MessageType>& message), (override));
    MOCK_METHOD(void, status_notification_req,
                (const std::int32_t evse_id, const std::int32_t connector_id, const ConnectorStatusEnum status,
                 const bool initiated_by_trigger_message),
                (override));
    MOCK_METHOD(void, heartbeat_req, (const bool initiated_by_trigger_message), (override));
    MOCK_METHOD(void, handle_scheduled_change_availability_requests, (const std::int32_t evse_id), (override));
    MOCK_METHOD(void, set_scheduled_change_availability_requests,
                (const std::int32_t evse_id, AvailabilityChange availability_change), (override));
    MOCK_METHOD(void, set_heartbeat_timer_interval, (const std::chrono::seconds& interval), (override));
    MOCK_METHOD(void, stop_heartbeat_timer, (), (override));
    MOCK_METHOD(ChangeAvailabilityResponse, change_availability_req,
                (bool& transaction_active, const ChangeAvailabilityRequest& request), (override));
    MOCK_METHOD(void, action_change_availability_req,
                (bool transaction_active, const ChangeAvailabilityRequest& request,
                 const ChangeAvailabilityResponse& response),
                (override));
};

class FirmwareUpdateMock : public FirmwareUpdateInterface {
public:
    MOCK_METHOD(void, handle_message, (const ocpp::EnhancedMessage<MessageType>& message), (override));
    MOCK_METHOD(void, on_firmware_update_status_notification,
                (std::int32_t request_id, const FirmwareStatusEnum& firmware_update_status,
                 const bool disable_connectors_during_install),
                (override));
    MOCK_METHOD(void, on_firmware_status_notification_request, (), (override));
    MOCK_METHOD(void, on_transaction_finished, (), (override));
};

class ProvisioningMock : public ProvisioningInterface {
public:
    MOCK_METHOD(void, handle_message, (const ocpp::EnhancedMessage<MessageType>& message), (override));
    MOCK_METHOD(void, boot_notification_req, (const BootReasonEnum& reason, const bool initiated_by_trigger_message),
                (override));
    MOCK_METHOD(void, stop_bootnotification_timer, (), (override));
    MOCK_METHOD(void, on_variable_changed, (const SetVariableData& set_variable_data), (override));
    MOCK_METHOD(std::vector<GetVariableResult>, get_variables,
                (const std::vector<GetVariableData>& get_variable_data_vector), (override));
    MOCK_METHOD((std::map<SetVariableData, SetVariableResult>), set_variables,
                (const std::vector<SetVariableData>& set_variable_data_vector, const std::string& source), (override));
};

class SmartChargingBlockMock : public SmartChargingInterface {
public:
    MOCK_METHOD(void, handle_message, (const ocpp::EnhancedMessage<MessageType>& message), (override));
    MOCK_METHOD(std::vector<EnhancedCompositeSchedule>, get_all_composite_schedules,
                (const std::int32_t duration, const ChargingRateUnitEnum& unit), (override));
    MOCK_METHOD(void, delete_transaction_tx_profiles, (const std::string& transaction_id), (override));
    MOCK_METHOD(SetChargingProfileResponse, conform_validate_and_add_profile,
                (ChargingProfile & profile, std::int32_t evse_id, CiString<20> charging_limit_source,
                 AddChargingProfileSource source_of_request),
                (override));
    MOCK_METHOD(ProfileValidationResultEnum, conform_and_validate_profile,
                (ChargingProfile & profile, std::int32_t evse_id, AddChargingProfileSource source_of_request),
                (override));
    MOCK_METHOD(EnhancedCompositeScheduleResponse, get_composite_schedule, (const GetCompositeScheduleRequest& request),
                (override));
    MOCK_METHOD(std::optional<EnhancedCompositeSchedule>, get_composite_schedule,
                (std::int32_t evse_id, std::chrono::seconds duration, ChargingRateUnitEnum unit), (override));
    MOCK_METHOD(void, notify_ev_charging_needs_req, (const NotifyEVChargingNeedsRequest& req), (override));
};

} // namespace

class RemoteTransactionControlTest : public ::testing::Test {
protected:
    DeviceModelTestHelper device_model_test_helper;
    DeviceModel* device_model;
    MockMessageDispatcher mock_dispatcher;
    ocpp::MessageLogging logging;
    ocpp::EvseSecurityMock evse_security;
    ConnectivityManagerMock connectivity_manager;
    EvseManagerFake evse_manager;
    ComponentStateManagerMock component_state_manager;
    NiceMock<ocpp::v2::DatabaseHandlerMock> database_handler_mock;
    OcspUpdaterMock ocsp_updater;
    MockFunction<void(const ocpp::CiString<50>& event_type, const std::optional<ocpp::CiString<255>>& tech_info)>
        security_event_callback_mock;
    std::atomic<ocpp::OcppProtocolVersion> ocpp_version;
    FunctionalBlockContext functional_block_context;
    Security security;

    NiceMock<TransactionMock> transaction;
    NiceMock<SmartChargingBlockMock> smart_charging;
    NiceMock<MeterValuesMock> meter_values;
    NiceMock<AvailabilityMock> availability;
    NiceMock<FirmwareUpdateMock> firmware_update;
    NiceMock<ProvisioningMock> provisioning;

    std::atomic<RegistrationStatusEnum> registration_status;
    std::atomic<UploadLogStatusEnum> upload_log_status;
    std::atomic<std::int32_t> upload_log_status_id;

    RemoteTransactionControl remote_transaction_control;

    RemoteTransactionControlTest() :
        device_model_test_helper(),
        device_model(device_model_test_helper.get_device_model()),
        logging(false, "", "", false, false, false, false, false, false, false, nullptr),
        evse_security(),
        connectivity_manager(),
        evse_manager(2),
        component_state_manager(),
        ocpp_version(ocpp::OcppProtocolVersion::v201),
        functional_block_context{
            this->mock_dispatcher,       *this->device_model, this->connectivity_manager,    this->evse_manager,
            this->database_handler_mock, this->evse_security, this->component_state_manager, this->ocpp_version},
        security(functional_block_context, logging, ocsp_updater, security_event_callback_mock.AsStdFunction()),
        registration_status(RegistrationStatusEnum::Accepted),
        upload_log_status(UploadLogStatusEnum::Idle),
        upload_log_status_id(0),
        remote_transaction_control(functional_block_context, transaction, smart_charging, meter_values, availability,
                                   firmware_update, security, nullptr, provisioning, nullptr, nullptr, nullptr,
                                   registration_status, upload_log_status, upload_log_status_id) {
    }

    void set_charging_station_csr_config() {
        this->device_model->set_value(ControllerComponentVariables::ChargeBoxSerialNumber.component,
                                      ControllerComponentVariables::ChargeBoxSerialNumber.variable.value(),
                                      AttributeEnum::Actual, "testserialnumber", "test", true);
        this->device_model->set_value(ControllerComponentVariables::OrganizationName.component,
                                      ControllerComponentVariables::OrganizationName.variable.value(),
                                      AttributeEnum::Actual, "testOrganization", "test", true);
        this->device_model->set_value(ControllerComponentVariables::ISO15118CtrlrCountryName.component,
                                      ControllerComponentVariables::ISO15118CtrlrCountryName.variable.value(),
                                      AttributeEnum::Actual, "testCountry", "test", true);
        this->device_model->set_value(ControllerComponentVariables::UseTPM.component,
                                      ControllerComponentVariables::UseTPM.variable.value(), AttributeEnum::Actual,
                                      "false", "test", true);
    }

    ocpp::EnhancedMessage<MessageType> create_trigger_message(const MessageTriggerEnum requested_message) {
        TriggerMessageRequest request;
        request.requestedMessage = requested_message;
        ocpp::Call<TriggerMessageRequest> call(request);

        json message = json::array();
        message.push_back(MessageTypeId::CALL);
        message.push_back(call.uniqueId.get());
        message.push_back(call.msg.get_type());
        message.push_back(json(call.msg));

        ocpp::EnhancedMessage<MessageType> enhanced_message;
        enhanced_message.uniqueId = call.uniqueId.get();
        enhanced_message.messageType = MessageType::TriggerMessage;
        enhanced_message.messageTypeId = MessageTypeId::CALL;
        enhanced_message.message = message;
        return enhanced_message;
    }
};

TEST_F(RemoteTransactionControlTest, trigger_sign_charging_station_certificate_missing_config_rejected) {
    // No CSR device model values are configured, so the CSR cannot be generated. The TriggerMessage must be rejected
    // (F06.FR.05) and no SignCertificate.req may be sent. Remove the organization name and rebuild the stack so the
    // fresh device model observes the removal.
    device_model_test_helper.remove_variable_from_db(
        ControllerComponentVariables::OrganizationName.component.name.get(), std::nullopt, std::nullopt, std::nullopt,
        ControllerComponentVariables::OrganizationName.variable->name.get(), std::nullopt);
    device_model = device_model_test_helper.get_device_model();

    const FunctionalBlockContext b{
        this->mock_dispatcher,       *this->device_model, this->connectivity_manager,    this->evse_manager,
        this->database_handler_mock, this->evse_security, this->component_state_manager, this->ocpp_version};
    Security s(b, logging, ocsp_updater, security_event_callback_mock.AsStdFunction());
    RemoteTransactionControl rtc(b, transaction, smart_charging, meter_values, availability, firmware_update, s,
                                 nullptr, provisioning, nullptr, nullptr, nullptr, registration_status,
                                 upload_log_status, upload_log_status_id);

    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_)).WillOnce(Invoke([](const json& call_result) {
        auto response = call_result[ocpp::CALLRESULT_PAYLOAD].get<TriggerMessageResponse>();
        EXPECT_EQ(response.status, TriggerMessageStatusEnum::Rejected);
    }));

    // No SignCertificate.req (or any other call) must be dispatched.
    EXPECT_CALL(mock_dispatcher, dispatch_call(_, _)).Times(0);

    rtc.handle_message(create_trigger_message(MessageTriggerEnum::SignChargingStationCertificate));
}

TEST_F(RemoteTransactionControlTest, trigger_sign_charging_station_certificate_complete_config_accepted_and_sent) {
    // All CSR device model values are configured, so the TriggerMessage is accepted and a SignCertificate.req is sent.
    set_charging_station_csr_config();

    ocpp::GetCertificateSignRequestResult sign_request_result;
    sign_request_result.status = GetCertificateSignRequestStatus::Accepted;
    sign_request_result.csr = "csr";

    EXPECT_CALL(this->evse_security,
                generate_certificate_signing_request(ocpp::CertificateSigningUseEnum::ChargingStationCertificate,
                                                     "testCountry", "testOrganization", "testserialnumber", false))
        .WillOnce(Return(sign_request_result));

    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_)).WillOnce(Invoke([](const json& call_result) {
        auto response = call_result[ocpp::CALLRESULT_PAYLOAD].get<TriggerMessageResponse>();
        EXPECT_EQ(response.status, TriggerMessageStatusEnum::Accepted);
    }));

    // The SignCertificate.req must actually be sent, flagged as triggered.
    EXPECT_CALL(mock_dispatcher, dispatch_call(_, _)).WillOnce(Invoke([](const json& call, bool triggered) {
        auto request = call[ocpp::CALL_PAYLOAD].get<SignCertificateRequest>();
        ASSERT_TRUE(request.certificateType.has_value());
        EXPECT_EQ(request.certificateType.value(), ocpp::v2::CertificateSigningUseEnum::ChargingStationCertificate);
        EXPECT_EQ(request.csr, "csr");
        EXPECT_TRUE(triggered);
    }));

    remote_transaction_control.handle_message(
        create_trigger_message(MessageTriggerEnum::SignChargingStationCertificate));
}

TEST_F(RemoteTransactionControlTest, trigger_sign_charging_station_certificate_missing_common_name_rejected) {
    // commonName (ChargeBoxSerialNumber) is required for the ChargingStation CSR. Remove it and rebuild the stack so
    // the fresh device model observes the removal. The TriggerMessage must be rejected and nothing dispatched.
    device_model_test_helper.remove_variable_from_db(
        ControllerComponentVariables::ChargeBoxSerialNumber.component.name.get(), std::nullopt, std::nullopt,
        std::nullopt, ControllerComponentVariables::ChargeBoxSerialNumber.variable->name.get(), std::nullopt);
    device_model = device_model_test_helper.get_device_model();

    const FunctionalBlockContext b{
        this->mock_dispatcher,       *this->device_model, this->connectivity_manager,    this->evse_manager,
        this->database_handler_mock, this->evse_security, this->component_state_manager, this->ocpp_version};
    Security s(b, logging, ocsp_updater, security_event_callback_mock.AsStdFunction());
    RemoteTransactionControl rtc(b, transaction, smart_charging, meter_values, availability, firmware_update, s,
                                 nullptr, provisioning, nullptr, nullptr, nullptr, registration_status,
                                 upload_log_status, upload_log_status_id);

    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_)).WillOnce(Invoke([](const json& call_result) {
        auto response = call_result[ocpp::CALLRESULT_PAYLOAD].get<TriggerMessageResponse>();
        EXPECT_EQ(response.status, TriggerMessageStatusEnum::Rejected);
    }));
    EXPECT_CALL(mock_dispatcher, dispatch_call(_, _)).Times(0);

    rtc.handle_message(create_trigger_message(MessageTriggerEnum::SignChargingStationCertificate));
}

TEST_F(RemoteTransactionControlTest, trigger_sign_charging_station_certificate_missing_country_rejected) {
    // country (ISO15118CtrlrCountryName) is required for the ChargingStation CSR. Remove it and rebuild the stack.
    device_model_test_helper.remove_variable_from_db(
        ControllerComponentVariables::ISO15118CtrlrCountryName.component.name.get(), std::nullopt, std::nullopt,
        std::nullopt, ControllerComponentVariables::ISO15118CtrlrCountryName.variable->name.get(), std::nullopt);
    device_model = device_model_test_helper.get_device_model();

    const FunctionalBlockContext b{
        this->mock_dispatcher,       *this->device_model, this->connectivity_manager,    this->evse_manager,
        this->database_handler_mock, this->evse_security, this->component_state_manager, this->ocpp_version};
    Security s(b, logging, ocsp_updater, security_event_callback_mock.AsStdFunction());
    RemoteTransactionControl rtc(b, transaction, smart_charging, meter_values, availability, firmware_update, s,
                                 nullptr, provisioning, nullptr, nullptr, nullptr, registration_status,
                                 upload_log_status, upload_log_status_id);

    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_)).WillOnce(Invoke([](const json& call_result) {
        auto response = call_result[ocpp::CALLRESULT_PAYLOAD].get<TriggerMessageResponse>();
        EXPECT_EQ(response.status, TriggerMessageStatusEnum::Rejected);
    }));
    EXPECT_CALL(mock_dispatcher, dispatch_call(_, _)).Times(0);

    rtc.handle_message(create_trigger_message(MessageTriggerEnum::SignChargingStationCertificate));
}

TEST_F(RemoteTransactionControlTest, trigger_sign_v2g_certificate_missing_config_rejected) {
    // V2G installation is enabled but the V2G CSR inputs (SeccId, ISO15118CtrlrOrganizationName) are absent by
    // default, so the CSR cannot be generated. The TriggerMessage must be rejected (F06.FR.05), nothing dispatched.
    device_model->set_value(ControllerComponentVariables::V2GCertificateInstallationEnabled.component,
                            ControllerComponentVariables::V2GCertificateInstallationEnabled.variable.value(),
                            AttributeEnum::Actual, "true", "test", true);

    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_)).WillOnce(Invoke([](const json& call_result) {
        auto response = call_result[ocpp::CALLRESULT_PAYLOAD].get<TriggerMessageResponse>();
        EXPECT_EQ(response.status, TriggerMessageStatusEnum::Rejected);
    }));
    EXPECT_CALL(mock_dispatcher, dispatch_call(_, _)).Times(0);

    remote_transaction_control.handle_message(create_trigger_message(MessageTriggerEnum::SignV2GCertificate));
}

TEST_F(RemoteTransactionControlTest, trigger_sign_certificate_while_awaiting_supersedes_and_resends) {
    // L3: a second TriggerMessage arriving while a previous SignCertificate awaits its CertificateSigned outcome must
    // still result in a SignCertificate.req being sent (the awaited state can otherwise go stale forever), not an
    // accept-then-silent-drop.
    set_charging_station_csr_config();

    ocpp::GetCertificateSignRequestResult sign_request_result;
    sign_request_result.status = GetCertificateSignRequestStatus::Accepted;
    sign_request_result.csr = "csr";
    EXPECT_CALL(this->evse_security, generate_certificate_signing_request(_, _, _, _, _))
        .WillRepeatedly(Return(sign_request_result));

    // Both triggers are accepted ...
    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_)).Times(2).WillRepeatedly(Invoke([](const json& call_result) {
        auto response = call_result[ocpp::CALLRESULT_PAYLOAD].get<TriggerMessageResponse>();
        EXPECT_EQ(response.status, TriggerMessageStatusEnum::Accepted);
    }));
    // ... and each one actually sends a SignCertificate.req.
    EXPECT_CALL(mock_dispatcher, dispatch_call(_, _)).Times(2);

    remote_transaction_control.handle_message(
        create_trigger_message(MessageTriggerEnum::SignChargingStationCertificate));
    remote_transaction_control.handle_message(
        create_trigger_message(MessageTriggerEnum::SignChargingStationCertificate));
}
