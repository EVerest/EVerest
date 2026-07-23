// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <ocpp/v2/ctrlr_component_variables.hpp>
#include <ocpp/v2/device_model.hpp>
#include <ocpp/v2/functional_blocks/availability.hpp>
#include <ocpp/v2/functional_blocks/functional_block_context.hpp>
#include <ocpp/v2/functional_blocks/security.hpp>

#define private public // Make firmware_status(_id) / firmware_status_before_installing accessible for test setup.
#include <ocpp/v2/functional_blocks/firmware_update.hpp>
#undef private

#include <ocpp/v2/messages/FirmwareStatusNotification.hpp>
#include <ocpp/v2/messages/UpdateFirmware.hpp>

#include "component_state_manager_mock.hpp"
#include "connectivity_manager_mock.hpp"
#include "device_model_test_helper.hpp"
#include "evse_manager_fake.hpp"
#include "evse_mock.hpp"
#include "evse_security_mock.hpp"
#include "message_dispatcher_mock.hpp"
#include "mocks/database_handler_mock.hpp"
#include "ocsp_updater_mock.hpp"

using namespace ocpp;
using namespace ocpp::v2;
using ::testing::_;
using ::testing::Invoke;
using ::testing::MockFunction;
using ::testing::Return;

class FirmwareUpdateTest : public ::testing::Test {
protected: // Members
    DeviceModelTestHelper device_model_test_helper;
    DeviceModel* device_model;
    MockMessageDispatcher mock_dispatcher;
    ocpp::MessageLogging logging;
    ocpp::EvseSecurityMock evse_security;
    ConnectivityManagerMock connectivity_manager;
    EvseManagerFake evse_manager;
    ComponentStateManagerMock component_state_manager;
    ::testing::NiceMock<ocpp::v2::DatabaseHandlerMock> database_handler_mock;
    OcspUpdaterMock ocsp_updater;
    std::atomic<ocpp::OcppProtocolVersion> ocpp_version;
    FunctionalBlockContext functional_block_context;

    MockFunction<void(const ocpp::CiString<50>& event_type, const std::optional<ocpp::CiString<255>>& tech_info)>
        security_event_callback_mock;
    Security security;

    MockFunction<UpdateFirmwareResponse(const UpdateFirmwareRequest& request)> update_firmware_request_callback_mock;
    MockFunction<void()> all_connectors_unavailable_callback_mock;
    std::atomic_bool all_connectors_unavailable_notified{false};

    EvseMock& evse_1;
    EvseMock& evse_2;

    std::unique_ptr<Availability> availability;
    std::unique_ptr<FirmwareUpdate> firmware_update;

protected: // Functions
    FirmwareUpdateTest() :
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
        evse_1(evse_manager.get_mock(1)),
        evse_2(evse_manager.get_mock(2)) {
        const AllConnectorsUnavailableCallback guarded_callback = [this]() {
            if (!this->all_connectors_unavailable_notified.exchange(true)) {
                this->all_connectors_unavailable_callback_mock.Call();
            }
        };
        availability = std::make_unique<Availability>(functional_block_context, std::nullopt, guarded_callback);
        firmware_update = std::make_unique<FirmwareUpdate>(functional_block_context, *availability, security,
                                                           update_firmware_request_callback_mock.AsStdFunction(),
                                                           guarded_callback);

        ON_CALL(evse_manager, any_transaction_active(_)).WillByDefault(Return(false));
        ON_CALL(evse_1, get_number_of_connectors()).WillByDefault(Return(1));
        ON_CALL(evse_2, get_number_of_connectors()).WillByDefault(Return(1));
        ON_CALL(evse_1, get_connector_effective_operational_status(_))
            .WillByDefault(Return(OperationalStatusEnum::Inoperative));
        ON_CALL(evse_2, get_connector_effective_operational_status(_))
            .WillByDefault(Return(OperationalStatusEnum::Inoperative));
    }

    static std::future<ocpp::EnhancedMessage<MessageType>> deferred_empty_response() {
        return std::async(std::launch::deferred, []() { return ocpp::EnhancedMessage<MessageType>{}; });
    }
};

// Test that all_connectors_unavailable_callback is only triggered once
TEST_F(FirmwareUpdateTest, InstallScheduled_ExplicitTrue_NoActiveTransaction_TriggersDisableOnce) {
    // First InstallScheduled message: Should trigger all_connectors_unavailable_callback_mock
    EXPECT_CALL(mock_dispatcher, dispatch_call_async(_, _)).WillOnce(Invoke([](const json& call, bool /*triggered*/) {
        auto request = call[ocpp::CALL_PAYLOAD].get<FirmwareStatusNotificationRequest>();
        EXPECT_EQ(request.status, FirmwareStatusEnum::InstallScheduled);
        EXPECT_EQ(request.requestId, std::optional<std::int32_t>(1));
        return deferred_empty_response();
    }));
    EXPECT_CALL(evse_1, set_connector_operative_status(1, OperationalStatusEnum::Inoperative, false));
    EXPECT_CALL(evse_2, set_connector_operative_status(1, OperationalStatusEnum::Inoperative, false));
    EXPECT_CALL(all_connectors_unavailable_callback_mock, Call()).Times(1);

    firmware_update->on_firmware_update_status_notification(1, FirmwareStatusEnum::InstallScheduled, true);

    ::testing::Mock::VerifyAndClearExpectations(&mock_dispatcher);
    ::testing::Mock::VerifyAndClearExpectations(&evse_1);
    ::testing::Mock::VerifyAndClearExpectations(&evse_2);
    ::testing::Mock::VerifyAndClearExpectations(&all_connectors_unavailable_callback_mock);

    // Second, identical call: all_connectors_unavailable_callback_mock may not be triggered again
    EXPECT_CALL(mock_dispatcher, dispatch_call_async(_, _)).Times(0);
    EXPECT_CALL(all_connectors_unavailable_callback_mock, Call()).Times(0);

    firmware_update->on_firmware_update_status_notification(1, FirmwareStatusEnum::InstallScheduled, true);
}

// Test that an InstallScheduled message without disable_connectors_during_install will not disable connectors
TEST_F(FirmwareUpdateTest, InstallScheduled_Nullopt_OnlyForwardsToCsms) {
    EXPECT_CALL(mock_dispatcher, dispatch_call_async(_, _)).WillOnce(Invoke([](const json& call, bool) {
        auto request = call[ocpp::CALL_PAYLOAD].get<FirmwareStatusNotificationRequest>();
        EXPECT_EQ(request.status, FirmwareStatusEnum::InstallScheduled);
        return deferred_empty_response();
    }));
    EXPECT_CALL(evse_1, set_connector_operative_status(_, _, _)).Times(0);
    EXPECT_CALL(evse_2, set_connector_operative_status(_, _, _)).Times(0);
    EXPECT_CALL(all_connectors_unavailable_callback_mock, Call()).Times(0);

    firmware_update->on_firmware_update_status_notification(1, FirmwareStatusEnum::InstallScheduled, std::nullopt);
}

// Test that disable_connectors_during_install = false on an InstallScheduled message has the same behavior as an unset value
TEST_F(FirmwareUpdateTest, InstallScheduled_ExplicitFalse_OnlyForwardsToCsms) {
    EXPECT_CALL(mock_dispatcher, dispatch_call_async(_, _)).WillOnce(Invoke([](const json& call, bool) {
        auto request = call[ocpp::CALL_PAYLOAD].get<FirmwareStatusNotificationRequest>();
        EXPECT_EQ(request.status, FirmwareStatusEnum::InstallScheduled);
        return deferred_empty_response();
    }));
    EXPECT_CALL(evse_1, set_connector_operative_status(_, _, _)).Times(0);
    EXPECT_CALL(evse_2, set_connector_operative_status(_, _, _)).Times(0);
    EXPECT_CALL(all_connectors_unavailable_callback_mock, Call()).Times(0);

    firmware_update->on_firmware_update_status_notification(1, FirmwareStatusEnum::InstallScheduled, false);
}

// Test that the firmware update process actually waits for all connectors to become unavailable
TEST_F(FirmwareUpdateTest, InstallScheduled_ExplicitTrue_ActiveTransaction_DefersBusyEvse) {
    // EVSE 1 is idle, EVSE 2 has an active transaction.
    ON_CALL(evse_manager, any_transaction_active(_)).WillByDefault(Return(true));
    ON_CALL(evse_1, has_active_transaction()).WillByDefault(Return(false));
    ON_CALL(evse_2, has_active_transaction()).WillByDefault(Return(true));

    EXPECT_CALL(mock_dispatcher, dispatch_call_async(_, _)).WillOnce(Invoke([](const json& call, bool) {
        auto request = call[ocpp::CALL_PAYLOAD].get<FirmwareStatusNotificationRequest>();
        EXPECT_EQ(request.status, FirmwareStatusEnum::InstallScheduled);
        return deferred_empty_response();
    }));
    // Idle EVSE becomes unavailable immediately.
    EXPECT_CALL(evse_1, set_connector_operative_status(1, OperationalStatusEnum::Inoperative, false));
    // Busy EVSE is not touched immediately - it is scheduled instead.
    EXPECT_CALL(evse_2, set_connector_operative_status(_, _, _)).Times(0);
    EXPECT_CALL(evse_2, set_evse_operative_status(_, _)).Times(0);
    // Not all connectors are unavailable yet (evse 2 still operative) - callback must not fire yet.
    EXPECT_CALL(all_connectors_unavailable_callback_mock, Call()).Times(0);

    firmware_update->on_firmware_update_status_notification(1, FirmwareStatusEnum::InstallScheduled, true);

    ::testing::Mock::VerifyAndClearExpectations(&mock_dispatcher);
    ::testing::Mock::VerifyAndClearExpectations(&evse_1);
    ::testing::Mock::VerifyAndClearExpectations(&evse_2);
    ::testing::Mock::VerifyAndClearExpectations(&all_connectors_unavailable_callback_mock);

    // The transaction on evse 2 ends: the scheduled availability change now executes, and since all connectors
    // are then effectively inoperative, the (guarded) callback fires.
    ON_CALL(evse_manager, any_transaction_active(_)).WillByDefault(Return(false));
    ON_CALL(evse_2, has_active_transaction()).WillByDefault(Return(false));
    EXPECT_CALL(evse_2, set_evse_operative_status(OperationalStatusEnum::Inoperative, false));
    EXPECT_CALL(all_connectors_unavailable_callback_mock, Call()).Times(1);

    availability->handle_scheduled_change_availability_requests(2);
}

// Test that a SignatureVerified message with unset disable_connectors_during_install will disable connectors
TEST_F(FirmwareUpdateTest, SignatureVerified_Nullopt_TriggersDisable_DefaultTruePreserved) {
    EXPECT_CALL(mock_dispatcher, dispatch_call_async(_, _)).WillOnce(Invoke([](const json& call, bool) {
        auto request = call[ocpp::CALL_PAYLOAD].get<FirmwareStatusNotificationRequest>();
        EXPECT_EQ(request.status, FirmwareStatusEnum::SignatureVerified);
        return deferred_empty_response();
    }));
    EXPECT_CALL(evse_1, set_connector_operative_status(1, OperationalStatusEnum::Inoperative, false));
    EXPECT_CALL(evse_2, set_connector_operative_status(1, OperationalStatusEnum::Inoperative, false));
    EXPECT_CALL(all_connectors_unavailable_callback_mock, Call()).Times(1);

    firmware_update->on_firmware_update_status_notification(1, FirmwareStatusEnum::SignatureVerified, std::nullopt);
}

// Test that a SignatureVerified message with disable_connectors_during_install = false does not disable the connectors
TEST_F(FirmwareUpdateTest, SignatureVerified_ExplicitFalse_DoesNotTriggerDisable) {
    EXPECT_CALL(mock_dispatcher, dispatch_call_async(_, _)).WillOnce(Invoke([](const json& call, bool) {
        return deferred_empty_response();
    }));
    EXPECT_CALL(evse_1, set_connector_operative_status(_, _, _)).Times(0);
    EXPECT_CALL(evse_2, set_connector_operative_status(_, _, _)).Times(0);
    EXPECT_CALL(all_connectors_unavailable_callback_mock, Call()).Times(0);

    firmware_update->on_firmware_update_status_notification(1, FirmwareStatusEnum::SignatureVerified, false);
}

// Test that a Downloaded message with unset disable_connectors_during_install will disable connectors
TEST_F(FirmwareUpdateTest, Downloaded_Nullopt_TriggersDisable_DefaultTruePreserved) {
    firmware_update->firmware_status_before_installing = FirmwareStatusEnum::Downloaded;

    EXPECT_CALL(mock_dispatcher, dispatch_call_async(_, _)).WillOnce(Invoke([](const json& call, bool) {
        auto request = call[ocpp::CALL_PAYLOAD].get<FirmwareStatusNotificationRequest>();
        EXPECT_EQ(request.status, FirmwareStatusEnum::Downloaded);
        return deferred_empty_response();
    }));
    EXPECT_CALL(evse_1, set_connector_operative_status(1, OperationalStatusEnum::Inoperative, false));
    EXPECT_CALL(evse_2, set_connector_operative_status(1, OperationalStatusEnum::Inoperative, false));
    EXPECT_CALL(all_connectors_unavailable_callback_mock, Call()).Times(1);

    firmware_update->on_firmware_update_status_notification(1, FirmwareStatusEnum::Downloaded, std::nullopt);
}

// Test that a Downloaded message with disable_connectors_during_install = false does not disable the connectors
TEST_F(FirmwareUpdateTest, Downloaded_ExplicitFalse_DoesNotTriggerDisable) {
    firmware_update->firmware_status_before_installing = FirmwareStatusEnum::Downloaded;

    EXPECT_CALL(mock_dispatcher, dispatch_call_async(_, _)).WillOnce(Invoke([](const json& call, bool) {
        return deferred_empty_response();
    }));
    EXPECT_CALL(evse_1, set_connector_operative_status(_, _, _)).Times(0);
    EXPECT_CALL(evse_2, set_connector_operative_status(_, _, _)).Times(0);
    EXPECT_CALL(all_connectors_unavailable_callback_mock, Call()).Times(0);

    firmware_update->on_firmware_update_status_notification(1, FirmwareStatusEnum::Downloaded, false);
}

// Test that a duplicate InstallScheduled message is not resent to the CSMS, but still disables connectors
TEST_F(FirmwareUpdateTest, EchoedInstallScheduled_ThenRealInstallScheduled_OptInStillRuns) {
    ON_CALL(evse_manager, any_transaction_active(_)).WillByDefault(Return(true));
    ON_CALL(evse_1, has_active_transaction()).WillByDefault(Return(false));
    ON_CALL(evse_2, has_active_transaction()).WillByDefault(Return(true));

    {
        ::testing::InSequence seq;
        // The original SignatureVerified notification.
        EXPECT_CALL(mock_dispatcher, dispatch_call_async(_, _)).WillOnce(Invoke([](const json& call, bool) {
            auto request = call[ocpp::CALL_PAYLOAD].get<FirmwareStatusNotificationRequest>();
            EXPECT_EQ(request.status, FirmwareStatusEnum::SignatureVerified);
            EXPECT_EQ(request.requestId, std::optional<std::int32_t>(5));
            return deferred_empty_response();
        }));
        // The internally-echoed InstallScheduled notification, with the same request_id.
        EXPECT_CALL(mock_dispatcher, dispatch_call_async(_, _)).WillOnce(Invoke([](const json& call, bool) {
            auto request = call[ocpp::CALL_PAYLOAD].get<FirmwareStatusNotificationRequest>();
            EXPECT_EQ(request.status, FirmwareStatusEnum::InstallScheduled);
            EXPECT_EQ(request.requestId, std::optional<std::int32_t>(5));
            return deferred_empty_response();
        }));
    }
    EXPECT_CALL(evse_1, set_connector_operative_status(1, OperationalStatusEnum::Inoperative, false));

    firmware_update->on_firmware_update_status_notification(5, FirmwareStatusEnum::SignatureVerified, true);

    ::testing::Mock::VerifyAndClearExpectations(&mock_dispatcher);
    ::testing::Mock::VerifyAndClearExpectations(&evse_1);
    ::testing::Mock::VerifyAndClearExpectations(&evse_2);

    // A second InstallScheduled message with the same request_id can still be used to disable the connectors but will
    // not be forwarded to the CSMS
    EXPECT_CALL(mock_dispatcher, dispatch_call_async(_, _)).Times(0);
    EXPECT_CALL(evse_1, set_connector_operative_status(1, OperationalStatusEnum::Inoperative, false));

    firmware_update->on_firmware_update_status_notification(5, FirmwareStatusEnum::InstallScheduled, true);
}
