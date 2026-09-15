// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <atomic>
#include <future>
#include <optional>
#include <vector>

#include <ocpp/v2/ctrlr_component_variables.hpp>
#include <ocpp/v2/device_model.hpp>
#include <ocpp/v2/functional_blocks/availability.hpp>
#include <ocpp/v2/functional_blocks/functional_block_context.hpp>
#include <ocpp/v2/functional_blocks/security.hpp>
#include <ocpp/v2/messages/FirmwareStatusNotification.hpp>
#include <ocpp/v2/messages/Get15118EVCertificate.hpp>
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

#define private public // Make firmware_status(_id) / firmware_status_before_installing accessible for test setup.
#include <ocpp/v2/functional_blocks/firmware_update.hpp>
#undef private

using namespace ocpp;
using namespace ocpp::v2;
using ::testing::_;
using ::testing::Invoke;
using ::testing::MockFunction;
using ::testing::NiceMock;
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
        // Guarded and handed to both Availability and FirmwareUpdate, the way ChargePoint::initialize does it
        const AllConnectorsUnavailableCallback guarded_callback = [this]() {
            if (!this->all_connectors_unavailable_notified.exchange(true)) {
                this->all_connectors_unavailable_callback_mock.Call();
            }
        };
        availability = std::make_unique<Availability>(functional_block_context, std::nullopt, guarded_callback);
        firmware_update =
            std::make_unique<FirmwareUpdate>(functional_block_context, *availability, security,
                                             update_firmware_request_callback_mock.AsStdFunction(), guarded_callback);

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

    /// \brief Build an incoming UpdateFirmware.req for \p request_id.
    ///
    /// With \p signed_firmware it carries a signature but no signing certificate, which is enough to make it a
    /// signed update without going through certificate verification.
    static ocpp::EnhancedMessage<MessageType> make_update_firmware_message(const std::int32_t request_id,
                                                                           const bool signed_firmware = false) {
        UpdateFirmwareRequest req;
        req.requestId = request_id;
        req.firmware.location = "ftp://example.com/firmware.bin";
        req.firmware.retrieveDateTime = ocpp::DateTime();
        if (signed_firmware) {
            req.firmware.signature = "c2lnbmF0dXJl";
        }

        ocpp::Call<UpdateFirmwareRequest> call(req);
        call.uniqueId = ocpp::create_message_id();

        ocpp::EnhancedMessage<MessageType> message;
        message.uniqueId = call.uniqueId;
        message.messageType = MessageType::UpdateFirmware;
        message.messageTypeId = ocpp::MessageTypeId::CALL;
        message.message = json::array();
        message.message.push_back(ocpp::MessageTypeId::CALL);
        message.message.push_back(call.uniqueId.get());
        message.message.push_back(call.msg.get_type());
        message.message.push_back(json(call.msg));
        return message;
    }

    /// \brief Let \p firmware_update handle an UpdateFirmware.req that the application answers with \p status.
    void handle_update_firmware_request(const std::int32_t request_id, const UpdateFirmwareStatusEnum status,
                                        const bool signed_firmware = false) {
        UpdateFirmwareResponse response;
        response.status = status;
        EXPECT_CALL(update_firmware_request_callback_mock, Call(_)).WillOnce(Return(response));
        EXPECT_CALL(mock_dispatcher, dispatch_call_result(_)).Times(::testing::AnyNumber());

        firmware_update->handle_message(make_update_firmware_message(request_id, signed_firmware));

        ::testing::Mock::VerifyAndClearExpectations(&update_firmware_request_callback_mock);
    }

    /// \brief A ChangeAvailability.req that makes EVSE \p evse_id inoperative.
    static ChangeAvailabilityRequest inoperative_request(const std::int32_t evse_id) {
        ChangeAvailabilityRequest request;
        request.operationalStatus = OperationalStatusEnum::Inoperative;
        EVSE evse;
        evse.id = evse_id;
        request.evse = evse;
        return request;
    }
};

TEST_F(FirmwareUpdateTest, InstallScheduled_ExplicitTrue_NoActiveTransaction_TriggersDisableOnce) {
    // The first InstallScheduled fires the callback
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

    // The second, identical call may not fire it again
    EXPECT_CALL(mock_dispatcher, dispatch_call_async(_, _)).Times(0);
    EXPECT_CALL(all_connectors_unavailable_callback_mock, Call()).Times(0);

    firmware_update->on_firmware_update_status_notification(1, FirmwareStatusEnum::InstallScheduled, true);
}

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

TEST_F(FirmwareUpdateTest, InstallScheduled_ExplicitTrue_ActiveTransaction_DefersBusyEvse) {
    // EVSE 1 is idle, EVSE 2 has an active transaction
    ON_CALL(evse_manager, any_transaction_active(_)).WillByDefault(Return(true));
    ON_CALL(evse_1, has_active_transaction()).WillByDefault(Return(false));
    ON_CALL(evse_2, has_active_transaction()).WillByDefault(Return(true));

    EXPECT_CALL(mock_dispatcher, dispatch_call_async(_, _)).WillOnce(Invoke([](const json& call, bool) {
        auto request = call[ocpp::CALL_PAYLOAD].get<FirmwareStatusNotificationRequest>();
        EXPECT_EQ(request.status, FirmwareStatusEnum::InstallScheduled);
        return deferred_empty_response();
    }));
    // The idle EVSE becomes unavailable immediately
    EXPECT_CALL(evse_1, set_connector_operative_status(1, OperationalStatusEnum::Inoperative, false));
    // The busy one is scheduled instead
    EXPECT_CALL(evse_2, set_connector_operative_status(_, _, _)).Times(0);
    EXPECT_CALL(evse_2, set_evse_operative_status(_, _)).Times(0);
    // Evse 2 is still operative, so the callback must not fire yet
    EXPECT_CALL(all_connectors_unavailable_callback_mock, Call()).Times(0);

    firmware_update->on_firmware_update_status_notification(1, FirmwareStatusEnum::InstallScheduled, true);

    ::testing::Mock::VerifyAndClearExpectations(&mock_dispatcher);
    ::testing::Mock::VerifyAndClearExpectations(&evse_1);
    ::testing::Mock::VerifyAndClearExpectations(&evse_2);
    ::testing::Mock::VerifyAndClearExpectations(&all_connectors_unavailable_callback_mock);

    // The transaction on evse 2 ends, so the scheduled change executes and all connectors are then effectively
    // inoperative, which fires the callback
    ON_CALL(evse_manager, any_transaction_active(_)).WillByDefault(Return(false));
    ON_CALL(evse_2, has_active_transaction()).WillByDefault(Return(false));
    EXPECT_CALL(evse_2, set_evse_operative_status(OperationalStatusEnum::Inoperative, false));
    EXPECT_CALL(all_connectors_unavailable_callback_mock, Call()).Times(1);

    availability->handle_scheduled_change_availability_requests(2);
}

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

TEST_F(FirmwareUpdateTest, SignatureVerified_ExplicitFalse_DoesNotTriggerDisable) {
    EXPECT_CALL(mock_dispatcher, dispatch_call_async(_, _)).WillOnce(Invoke([](const json& call, bool) {
        return deferred_empty_response();
    }));
    EXPECT_CALL(evse_1, set_connector_operative_status(_, _, _)).Times(0);
    EXPECT_CALL(evse_2, set_connector_operative_status(_, _, _)).Times(0);
    EXPECT_CALL(all_connectors_unavailable_callback_mock, Call()).Times(0);

    firmware_update->on_firmware_update_status_notification(1, FirmwareStatusEnum::SignatureVerified, false);
}

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

TEST_F(FirmwareUpdateTest, EchoedInstallScheduled_ThenRealInstallScheduled_OptInStillRuns) {
    ON_CALL(evse_manager, any_transaction_active(_)).WillByDefault(Return(true));
    ON_CALL(evse_1, has_active_transaction()).WillByDefault(Return(false));
    ON_CALL(evse_2, has_active_transaction()).WillByDefault(Return(true));

    {
        ::testing::InSequence seq;
        // The original SignatureVerified notification
        EXPECT_CALL(mock_dispatcher, dispatch_call_async(_, _)).WillOnce(Invoke([](const json& call, bool) {
            auto request = call[ocpp::CALL_PAYLOAD].get<FirmwareStatusNotificationRequest>();
            EXPECT_EQ(request.status, FirmwareStatusEnum::SignatureVerified);
            EXPECT_EQ(request.requestId, std::optional<std::int32_t>(5));
            return deferred_empty_response();
        }));
        // The internally echoed InstallScheduled notification, with the same request_id
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

    // The same request_id again can still disable the connectors, but is not forwarded to the CSMS
    EXPECT_CALL(mock_dispatcher, dispatch_call_async(_, _)).Times(0);
    EXPECT_CALL(evse_1, set_connector_operative_status(1, OperationalStatusEnum::Inoperative, false));

    firmware_update->on_firmware_update_status_notification(5, FirmwareStatusEnum::InstallScheduled, true);
}

// Without dropping the non-persisted availability changes the update queued behind running transactions, the
// entry the dead cycle parked for EVSE 2 survives and makes the EVSE inoperative for an update that is gone
TEST_F(FirmwareUpdateTest, AbortedUpdate_NewRequest_DropsScheduledAvailabilityChange) {
    // EVSE 1 is idle, EVSE 2 has an active transaction, so the update schedules a non-persisted change for it
    ON_CALL(evse_manager, any_transaction_active(_)).WillByDefault(Return(true));
    ON_CALL(evse_1, has_active_transaction()).WillByDefault(Return(false));
    ON_CALL(evse_2, has_active_transaction()).WillByDefault(Return(true));

    EXPECT_CALL(mock_dispatcher, dispatch_call_async(_, _)).WillRepeatedly(Invoke([](const json&, bool) {
        return deferred_empty_response();
    }));
    EXPECT_CALL(evse_1, set_connector_operative_status(1, OperationalStatusEnum::Inoperative, false));
    // EVSE 2 is busy, so not all connectors are inoperative at any point in this test
    EXPECT_CALL(all_connectors_unavailable_callback_mock, Call()).Times(0);

    firmware_update->on_firmware_update_status_notification(1, FirmwareStatusEnum::InstallScheduled, true);

    ::testing::Mock::VerifyAndClearExpectations(&evse_1);
    ::testing::Mock::VerifyAndClearExpectations(&evse_2);

    // The update dies without a terminal status, then the CSMS starts a new one, which is accepted
    handle_update_firmware_request(2, UpdateFirmwareStatusEnum::Accepted);

    // The transaction on EVSE 2 ends, and the stale entry of the dead cycle must be gone by then
    ON_CALL(evse_manager, any_transaction_active(_)).WillByDefault(Return(false));
    ON_CALL(evse_2, has_active_transaction()).WillByDefault(Return(false));
    EXPECT_CALL(evse_2, set_evse_operative_status(_, _)).Times(0);
    EXPECT_CALL(evse_2, set_connector_operative_status(_, _, _)).Times(0);

    availability->handle_scheduled_change_availability_requests(2);
}

// A request answered with Rejected, InvalidCertificate or RevokedCertificate starts no new update cycle, so it
// must not disturb the running update's guard or the availability changes it queued
TEST_F(FirmwareUpdateTest, RejectedRequest_LeavesRunningUpdateIntact) {
    ON_CALL(evse_manager, any_transaction_active(_)).WillByDefault(Return(true));
    ON_CALL(evse_1, has_active_transaction()).WillByDefault(Return(false));
    ON_CALL(evse_2, has_active_transaction()).WillByDefault(Return(true));

    EXPECT_CALL(mock_dispatcher, dispatch_call_async(_, _)).WillRepeatedly(Invoke([](const json&, bool) {
        return deferred_empty_response();
    }));
    EXPECT_CALL(evse_1, set_connector_operative_status(1, OperationalStatusEnum::Inoperative, false));
    EXPECT_CALL(all_connectors_unavailable_callback_mock, Call()).Times(0);

    firmware_update->on_firmware_update_status_notification(1, FirmwareStatusEnum::InstallScheduled, true);

    ::testing::Mock::VerifyAndClearExpectations(&evse_1);
    ::testing::Mock::VerifyAndClearExpectations(&evse_2);
    ::testing::Mock::VerifyAndClearExpectations(&all_connectors_unavailable_callback_mock);

    handle_update_firmware_request(2, UpdateFirmwareStatusEnum::Rejected);

    // The transaction ends, and the running update's scheduled change must still be there and still execute
    ON_CALL(evse_manager, any_transaction_active(_)).WillByDefault(Return(false));
    ON_CALL(evse_2, has_active_transaction()).WillByDefault(Return(false));
    EXPECT_CALL(evse_2, set_evse_operative_status(OperationalStatusEnum::Inoperative, false));
    EXPECT_CALL(all_connectors_unavailable_callback_mock, Call()).Times(1);

    availability->handle_scheduled_change_availability_requests(2);
}

// The duplicate suppression at the top of on_firmware_update_status_notification compares against the status the
// previous cycle last reported, so without a reset a new cycle opening with that same status is silently dropped
TEST_F(FirmwareUpdateTest, AbortedUpdate_NewRequest_ResetsReportedFirmwareStatus) {
    EXPECT_CALL(mock_dispatcher, dispatch_call_async(_, _)).WillOnce(Invoke([](const json& call, bool) {
        auto request = call[ocpp::CALL_PAYLOAD].get<FirmwareStatusNotificationRequest>();
        EXPECT_EQ(request.status, FirmwareStatusEnum::Installing);
        return deferred_empty_response();
    }));

    firmware_update->on_firmware_update_status_notification(-1, FirmwareStatusEnum::Installing, std::nullopt);
    ::testing::Mock::VerifyAndClearExpectations(&mock_dispatcher);

    // The update dies while Installing, then the CSMS starts a new one
    handle_update_firmware_request(7, UpdateFirmwareStatusEnum::Accepted);

    // The new cycle reaches Installing too, which is a fresh notification and must reach the CSMS
    EXPECT_CALL(mock_dispatcher, dispatch_call_async(_, _)).WillOnce(Invoke([](const json& call, bool) {
        auto request = call[ocpp::CALL_PAYLOAD].get<FirmwareStatusNotificationRequest>();
        EXPECT_EQ(request.status, FirmwareStatusEnum::Installing);
        return deferred_empty_response();
    }));

    firmware_update->on_firmware_update_status_notification(-1, FirmwareStatusEnum::Installing, std::nullopt);
}

// A rejected request must not touch the running update's idea of which status comes right before installing. A
// signed update whose expected SignatureVerified is rewritten to Downloaded never reaches its disable stage, so
// its connectors stay enabled and the installation waits forever
TEST_F(FirmwareUpdateTest, RejectedRequest_KeepsRunningUpdatePreInstallStatus) {
    EXPECT_CALL(mock_dispatcher, dispatch_call_async(_, _)).WillRepeatedly(Invoke([](const json&, bool) {
        return deferred_empty_response();
    }));

    // A signed update is accepted and starts running, so it reports SignatureVerified before installing
    handle_update_firmware_request(1, UpdateFirmwareStatusEnum::Accepted, true);
    EXPECT_EQ(firmware_update->firmware_status_before_installing, FirmwareStatusEnum::SignatureVerified);

    // The CSMS retries with an unsigned request while that update is still running. It is rejected, so nothing
    // about the running update may change
    handle_update_firmware_request(2, UpdateFirmwareStatusEnum::Rejected);
    EXPECT_EQ(firmware_update->firmware_status_before_installing, FirmwareStatusEnum::SignatureVerified);

    // The running update reaches the status it reports right before installing, which still has to disable the
    // connectors and report them all unavailable
    EXPECT_CALL(evse_1, set_connector_operative_status(1, OperationalStatusEnum::Inoperative, false));
    EXPECT_CALL(evse_2, set_connector_operative_status(1, OperationalStatusEnum::Inoperative, false));
    EXPECT_CALL(all_connectors_unavailable_callback_mock, Call()).Times(1);

    firmware_update->on_firmware_update_status_notification(1, FirmwareStatusEnum::SignatureVerified, std::nullopt);
}

// An update that dies or an OCPP restart can re-announce Idle without ever reaching an end state, which would
// leave the connectors disabled for the install stuck Unavailable. Checked separately from
// is_firmware_status_end_state(), because that also resets the all-connectors-unavailable guard, which Idle must
// not do
TEST_F(FirmwareUpdateTest, IdleStatus_RestoresConnectors_WithoutRearmingGuard) {
    EXPECT_CALL(mock_dispatcher, dispatch_call_async(_, _)).WillRepeatedly(Invoke([](const json&, bool) {
        return deferred_empty_response();
    }));

    // InstallScheduled disables both connectors and fires the callback once
    EXPECT_CALL(evse_1, set_connector_operative_status(1, OperationalStatusEnum::Inoperative, false));
    EXPECT_CALL(evse_2, set_connector_operative_status(1, OperationalStatusEnum::Inoperative, false));
    EXPECT_CALL(all_connectors_unavailable_callback_mock, Call()).Times(1);

    firmware_update->on_firmware_update_status_notification(1, FirmwareStatusEnum::InstallScheduled, true);

    ::testing::Mock::VerifyAndClearExpectations(&evse_1);
    ::testing::Mock::VerifyAndClearExpectations(&evse_2);
    ::testing::Mock::VerifyAndClearExpectations(&all_connectors_unavailable_callback_mock);

    // The updater gives up and falls back to Idle, which has to restore the connectors it disabled for the
    // install without firing the callback again
    EXPECT_CALL(evse_1, restore_connector_operative_status(1));
    EXPECT_CALL(evse_2, restore_connector_operative_status(1));
    EXPECT_CALL(all_connectors_unavailable_callback_mock, Call()).Times(0);

    firmware_update->on_firmware_update_status_notification(-1, FirmwareStatusEnum::Idle, std::nullopt);

    ::testing::Mock::VerifyAndClearExpectations(&evse_1);
    ::testing::Mock::VerifyAndClearExpectations(&evse_2);
    ::testing::Mock::VerifyAndClearExpectations(&all_connectors_unavailable_callback_mock);

    // Idle did not re-arm the guard, so it is still latched from the InstallScheduled notification
    EXPECT_TRUE(this->all_connectors_unavailable_notified.load())
        << "Idle must not reset the all-connectors-unavailable guard - the update cycle is still the old one";
}

// An unrelated ChangeAvailability cannot latch the guard, because it only ever goes Waiting->Notified and the
// CAS is a no-op while it is idle. The other direction does happen, and the guard has to swallow it: reporting
// all connectors unavailable twice would double-fire OCPP201.cpp call_allow_firmware_installation(), which is a
// one-shot permission gate
TEST_F(FirmwareUpdateTest, FirmwareUpdateGuard_SuppressesRedundantAvailabilityDrivenNotification) {
    EXPECT_CALL(mock_dispatcher, dispatch_call_async(_, _)).WillRepeatedly(Invoke([](const json&, bool) {
        return deferred_empty_response();
    }));

    // A firmware update reaches InstallScheduled and reports all connectors unavailable
    EXPECT_CALL(all_connectors_unavailable_callback_mock, Call()).Times(1);
    firmware_update->on_firmware_update_status_notification(1, FirmwareStatusEnum::InstallScheduled, true);
    ::testing::Mock::VerifyAndClearExpectations(&all_connectors_unavailable_callback_mock);

    // The CSMS separately sent ChangeAvailability(Inoperative) for EVSE 2 while a transaction was running, so it
    // was scheduled. That transaction now ends and the change executes, which is not new information because all
    // connectors were already reported unavailable above
    availability->set_scheduled_change_availability_requests(2, {inoperative_request(2), true});

    EXPECT_CALL(evse_2, set_evse_operative_status(OperationalStatusEnum::Inoperative, true));
    EXPECT_CALL(all_connectors_unavailable_callback_mock, Call()).Times(0);

    availability->handle_scheduled_change_availability_requests(2);
}

namespace {

class AvailabilityMock : public AvailabilityInterface {
public:
    MOCK_METHOD(void, handle_message, (const ocpp::EnhancedMessage<MessageType>&), (override));
    MOCK_METHOD(void, status_notification_req, (std::int32_t, std::int32_t, ConnectorStatusEnum, bool), (override));
    MOCK_METHOD(void, heartbeat_req, (bool), (override));
    MOCK_METHOD(void, handle_scheduled_change_availability_requests, (std::int32_t), (override));
    MOCK_METHOD(void, set_scheduled_change_availability_requests, (std::int32_t, AvailabilityChange), (override));
    MOCK_METHOD(void, drop_non_persistent_scheduled_changes, (), (override));
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
                (const CiString<50>&, const std::optional<CiString<255>>&, bool, bool, const std::optional<DateTime>&),
                (override));
    MOCK_METHOD(void, sign_certificate_req, (const ocpp::CertificateSigningUseEnum&, bool), (override));
    MOCK_METHOD(std::optional<StatusInfo>, is_sign_certificate_possible, (const ocpp::CertificateSigningUseEnum&),
                (const, override));
    MOCK_METHOD(void, stop_certificate_signed_timer, (), (override));
    MOCK_METHOD(void, init_certificate_expiration_check_timers, (), (override));
    MOCK_METHOD(void, stop_certificate_expiration_check_timers, (), (override));
    MOCK_METHOD(Get15118EVCertificateResponse, on_get_15118_ev_certificate_request,
                (const Get15118EVCertificateRequest&), (override));
};

// L01.FR.13: with the DeferFirmwareDownloadDuringTransaction gate enabled and a transaction active,
// the download itself must be deferred (DownloadScheduled) until the last transaction ends. With the
// gate off (default) the download starts immediately, keeping TC_L_14/TC_L_15 behavior unchanged.
class FirmwareUpdateDeferredDownloadTest : public ::testing::Test {
protected:
    DeviceModelTestHelper dm_helper;
    DeviceModel* dm{nullptr};

    NiceMock<MockMessageDispatcher> mock_dispatcher;
    NiceMock<ocpp::ConnectivityManagerMock> connectivity_manager;
    std::unique_ptr<EvseManagerFake> evse_manager;
    NiceMock<DatabaseHandlerMock> db_handler;
    ocpp::EvseSecurityMock evse_security;
    NiceMock<ComponentStateManagerMock> component_state_manager;
    std::atomic<OcppProtocolVersion> ocpp_version{OcppProtocolVersion::v201};

    NiceMock<AvailabilityMock> availability;
    NiceMock<SecurityMock> security;

    std::unique_ptr<FunctionalBlockContext> fb_context;
    std::unique_ptr<FirmwareUpdate> firmware_update;

    std::vector<UpdateFirmwareRequest> callback_invocations;
    std::vector<json> dispatched_calls;
    std::vector<json> dispatched_call_results;
    UpdateFirmwareStatusEnum callback_response_status{UpdateFirmwareStatusEnum::Accepted};

    FirmwareUpdateDeferredDownloadTest() : dm_helper() {
        dm = dm_helper.get_device_model();
        evse_manager = std::make_unique<EvseManagerFake>(1);

        fb_context =
            std::make_unique<FunctionalBlockContext>(mock_dispatcher, *dm, connectivity_manager, *evse_manager,
                                                     db_handler, evse_security, component_state_manager, ocpp_version);

        ON_CALL(mock_dispatcher, dispatch_call(_, _)).WillByDefault(Invoke([this](const json& call, bool) {
            this->dispatched_calls.push_back(call);
        }));
        ON_CALL(mock_dispatcher, dispatch_call_async(_, _)).WillByDefault(Invoke([this](const json& call, bool) {
            this->dispatched_calls.push_back(call);
            return std::promise<ocpp::EnhancedMessage<MessageType>>().get_future();
        }));
        ON_CALL(mock_dispatcher, dispatch_call_result(_)).WillByDefault(Invoke([this](const json& call_result) {
            this->dispatched_call_results.push_back(call_result);
        }));

        firmware_update = std::make_unique<FirmwareUpdate>(
            *fb_context, availability, security,
            [this](const UpdateFirmwareRequest& request) {
                this->callback_invocations.push_back(request);
                UpdateFirmwareResponse response;
                response.status = this->callback_response_status;
                return response;
            },
            std::nullopt);
    }

    void enable_defer_download_gate() {
        const auto& cv = ControllerComponentVariables::DeferFirmwareDownloadDuringTransaction;
        ASSERT_EQ(dm->set_value(cv.component, cv.variable.value(), AttributeEnum::Actual, "true", "test", true),
                  SetVariableStatusEnum::Accepted);
    }

    void disable_defer_download_gate() {
        const auto& cv = ControllerComponentVariables::DeferFirmwareDownloadDuringTransaction;
        ASSERT_EQ(dm->set_value(cv.component, cv.variable.value(), AttributeEnum::Actual, "false", "test", true),
                  SetVariableStatusEnum::Accepted);
    }

    void start_transaction() {
        evse_manager->open_transaction(1, "test-transaction");
        ON_CALL(*evse_manager, any_transaction_active(_)).WillByDefault(Return(true));
    }

    void end_transaction() {
        auto& mock = evse_manager->get_mock(1);
        EXPECT_CALL(mock, has_active_transaction()).WillRepeatedly(Return(false));
        ON_CALL(*evse_manager, any_transaction_active(_)).WillByDefault(Return(false));
    }

    ocpp::EnhancedMessage<MessageType> make_update_firmware_message(std::int32_t request_id) {
        UpdateFirmwareRequest request;
        request.requestId = request_id;
        request.firmware.location = "https://firmware.example.com/firmware.bin";
        request.firmware.retrieveDateTime = ocpp::DateTime("2024-01-01T00:00:00Z");
        ocpp::Call<UpdateFirmwareRequest> call(request);
        ocpp::EnhancedMessage<MessageType> enhanced_message;
        enhanced_message.messageType = MessageType::UpdateFirmware;
        enhanced_message.message = call;
        return enhanced_message;
    }

    ocpp::EnhancedMessage<MessageType> make_update_firmware_message_with_cert(std::int32_t request_id) {
        UpdateFirmwareRequest request;
        request.requestId = request_id;
        request.firmware.location = "https://firmware.example.com/firmware.bin";
        request.firmware.retrieveDateTime = ocpp::DateTime("2024-01-01T00:00:00Z");
        request.firmware.signingCertificate = "invalid-cert";
        ocpp::Call<UpdateFirmwareRequest> call(request);
        ocpp::EnhancedMessage<MessageType> enhanced_message;
        enhanced_message.messageType = MessageType::UpdateFirmware;
        enhanced_message.message = call;
        return enhanced_message;
    }

    std::vector<json> firmware_status_notifications() const {
        std::vector<json> result;
        for (const auto& call : dispatched_calls) {
            if (call.at(CALL_ACTION) == "FirmwareStatusNotification") {
                result.push_back(call.at(CALL_PAYLOAD));
            }
        }
        return result;
    }

    std::vector<UpdateFirmwareStatusEnum> update_firmware_response_statuses() const {
        std::vector<UpdateFirmwareStatusEnum> result;
        for (const auto& call_result : dispatched_call_results) {
            result.push_back(call_result.at(CALLRESULT_PAYLOAD).get<UpdateFirmwareResponse>().status);
        }
        return result;
    }
};

TEST_F(FirmwareUpdateDeferredDownloadTest, GateOnTransactionActiveDefersDownload) {
    enable_defer_download_gate();
    start_transaction();

    EXPECT_CALL(availability, set_scheduled_change_availability_requests(1, _)).Times(1);
    firmware_update->handle_message(make_update_firmware_message(150));

    EXPECT_TRUE(callback_invocations.empty());
    const auto notifications = firmware_status_notifications();
    ASSERT_EQ(notifications.size(), 1);
    EXPECT_EQ(notifications.at(0).at("status"), "DownloadScheduled");
    EXPECT_EQ(notifications.at(0).at("requestId"), 150);
}

TEST_F(FirmwareUpdateDeferredDownloadTest, DeferredDownloadStartsWhenLastTransactionEnds) {
    enable_defer_download_gate();
    start_transaction();
    firmware_update->handle_message(make_update_firmware_message(150));
    ASSERT_TRUE(callback_invocations.empty());

    end_transaction();
    firmware_update->on_transaction_finished();

    ASSERT_EQ(callback_invocations.size(), 1);
    EXPECT_EQ(callback_invocations.at(0).requestId, 150);

    // Deferred request must fire exactly once.
    firmware_update->on_transaction_finished();
    EXPECT_EQ(callback_invocations.size(), 1);
}

TEST_F(FirmwareUpdateDeferredDownloadTest, DeferredDownloadWaitsWhileTransactionStillActive) {
    enable_defer_download_gate();
    start_transaction();
    firmware_update->handle_message(make_update_firmware_message(150));

    // Another transaction is still active, so the download must stay deferred.
    firmware_update->on_transaction_finished();
    EXPECT_TRUE(callback_invocations.empty());
}

TEST_F(FirmwareUpdateDeferredDownloadTest, GateOffTransactionActiveDownloadsImmediately) {
    disable_defer_download_gate();
    start_transaction();
    firmware_update->handle_message(make_update_firmware_message(150));

    ASSERT_EQ(callback_invocations.size(), 1);
    EXPECT_EQ(callback_invocations.at(0).requestId, 150);
    EXPECT_TRUE(firmware_status_notifications().empty());
}

TEST_F(FirmwareUpdateDeferredDownloadTest, GateOnNoTransactionDownloadsImmediately) {
    enable_defer_download_gate();
    firmware_update->handle_message(make_update_firmware_message(150));

    ASSERT_EQ(callback_invocations.size(), 1);
    EXPECT_EQ(callback_invocations.at(0).requestId, 150);
    EXPECT_TRUE(firmware_status_notifications().empty());
}

TEST_F(FirmwareUpdateDeferredDownloadTest, SupersedingRequestCancelsDeferredWhileStillCharging) {
    // L01.FR.24: the second request cancels the deferred one, so it is answered AcceptedCanceled rather than
    // Accepted. It is itself deferred, because a transaction is still active.
    enable_defer_download_gate();
    start_transaction();
    firmware_update->handle_message(make_update_firmware_message(150));
    firmware_update->handle_message(make_update_firmware_message(151));

    ASSERT_EQ(update_firmware_response_statuses().size(), 2);
    EXPECT_EQ(update_firmware_response_statuses().at(0), UpdateFirmwareStatusEnum::Accepted);
    EXPECT_EQ(update_firmware_response_statuses().at(1), UpdateFirmwareStatusEnum::AcceptedCanceled);

    end_transaction();
    firmware_update->on_transaction_finished();

    // Only the superseding request proceeds.
    ASSERT_EQ(callback_invocations.size(), 1);
    EXPECT_EQ(callback_invocations.at(0).requestId, 151);

    std::size_t old_request_notifications = 0;
    for (const auto& notification : firmware_status_notifications()) {
        if (notification.contains("requestId") and notification.at("requestId") == 150) {
            old_request_notifications++;
        }
    }
    // No DownloadFailed is sent for the canceled request: the spec makes it optional, and it would restore
    // connectors that the re-deferral immediately disables again.
    EXPECT_EQ(old_request_notifications, 1);
}

TEST_F(FirmwareUpdateDeferredDownloadTest, SupersedingRequestCancelsDeferredAfterTransactionEnded) {
    // Same cancellation, but the superseding request arrives once charging has stopped, so it is not deferred and
    // goes straight to the callback. The response must still report the cancellation.
    enable_defer_download_gate();
    start_transaction();
    firmware_update->handle_message(make_update_firmware_message(150));

    end_transaction();
    firmware_update->handle_message(make_update_firmware_message(151));

    ASSERT_EQ(callback_invocations.size(), 1);
    EXPECT_EQ(callback_invocations.at(0).requestId, 151);
    ASSERT_EQ(update_firmware_response_statuses().size(), 2);
    EXPECT_EQ(update_firmware_response_statuses().at(1), UpdateFirmwareStatusEnum::AcceptedCanceled);
}

TEST_F(FirmwareUpdateDeferredDownloadTest, FirstRequestIsAcceptedNotCanceled) {
    // Guard against reporting a cancellation when there was nothing to cancel.
    enable_defer_download_gate();
    start_transaction();
    firmware_update->handle_message(make_update_firmware_message(150));

    ASSERT_EQ(update_firmware_response_statuses().size(), 1);
    EXPECT_EQ(update_firmware_response_statuses().at(0), UpdateFirmwareStatusEnum::Accepted);
}

TEST_F(FirmwareUpdateDeferredDownloadTest, SupersedingRequestRejectedByCallbackKeepsRejection) {
    // A cancellation must not upgrade a rejection into AcceptedCanceled.
    enable_defer_download_gate();
    start_transaction();
    firmware_update->handle_message(make_update_firmware_message(150));

    end_transaction();
    callback_response_status = UpdateFirmwareStatusEnum::Rejected;
    firmware_update->handle_message(make_update_firmware_message(151));

    ASSERT_EQ(update_firmware_response_statuses().size(), 2);
    EXPECT_EQ(update_firmware_response_statuses().at(1), UpdateFirmwareStatusEnum::Rejected);
}

TEST_F(FirmwareUpdateDeferredDownloadTest, DeferredRejectedOnFinishSendsDownloadFailed) {
    enable_defer_download_gate();
    start_transaction();
    firmware_update->handle_message(make_update_firmware_message(150));
    ASSERT_TRUE(callback_invocations.empty());

    // The callback rejects the deferred request when it is finally released.
    callback_response_status = UpdateFirmwareStatusEnum::Rejected;
    end_transaction();
    firmware_update->on_transaction_finished();

    ASSERT_EQ(callback_invocations.size(), 1);
    bool download_failed_seen = false;
    for (const auto& notification : firmware_status_notifications()) {
        if (notification.at("status") == "DownloadFailed" and notification.contains("requestId") and
            notification.at("requestId") == 150) {
            download_failed_seen = true;
        }
    }
    EXPECT_TRUE(download_failed_seen);
}

TEST_F(FirmwareUpdateDeferredDownloadTest, InvalidCertSupersedingRequestLeavesDeferredIntact) {
    ON_CALL(evse_security, verify_certificate(_, testing::An<const ocpp::LeafCertificateType&>()))
        .WillByDefault(Return(ocpp::CertificateValidationResult::InvalidSignature));

    enable_defer_download_gate();
    start_transaction();
    firmware_update->handle_message(make_update_firmware_message(150));

    // A superseding request with an invalid signing certificate is rejected and must not disturb
    // the already deferred request.
    firmware_update->handle_message(make_update_firmware_message_with_cert(151));

    end_transaction();
    firmware_update->on_transaction_finished();

    ASSERT_EQ(callback_invocations.size(), 1);
    EXPECT_EQ(callback_invocations.at(0).requestId, 150);
}

} // namespace
