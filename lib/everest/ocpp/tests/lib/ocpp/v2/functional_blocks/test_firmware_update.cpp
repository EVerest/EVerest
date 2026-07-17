// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <atomic>
#include <future>
#include <optional>
#include <vector>

#include <component_state_manager_mock.hpp>
#include <connectivity_manager_mock.hpp>
#include <device_model_test_helper.hpp>
#include <evse_manager_fake.hpp>
#include <evse_security_mock.hpp>
#include <message_dispatcher_mock.hpp>
#include <mocks/database_handler_mock.hpp>

#include <ocpp/v2/ctrlr_component_variables.hpp>
#include <ocpp/v2/device_model.hpp>
#include <ocpp/v2/functional_blocks/availability.hpp>
#include <ocpp/v2/functional_blocks/firmware_update.hpp>
#include <ocpp/v2/functional_blocks/functional_block_context.hpp>
#include <ocpp/v2/functional_blocks/security.hpp>
#include <ocpp/v2/messages/Get15118EVCertificate.hpp>
#include <ocpp/v2/messages/UpdateFirmware.hpp>

using namespace ocpp;
using namespace ocpp::v2;
using ::testing::_;
using ::testing::Invoke;
using ::testing::NiceMock;
using ::testing::Return;

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
                (const CiString<50>&, const std::optional<CiString<255>>&, bool, bool, const std::optional<DateTime>&),
                (override));
    MOCK_METHOD(void, sign_certificate_req, (const ocpp::CertificateSigningUseEnum&, bool), (override));
    MOCK_METHOD(bool, is_sign_certificate_possible, (const ocpp::CertificateSigningUseEnum&), (const, override));
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

TEST_F(FirmwareUpdateDeferredDownloadTest, SupersedingRequestDropsDeferredSilently) {
    enable_defer_download_gate();
    start_transaction();
    firmware_update->handle_message(make_update_firmware_message(150));
    firmware_update->handle_message(make_update_firmware_message(151));

    end_transaction();
    firmware_update->on_transaction_finished();

    // Only the superseding request proceeds; the old one is dropped without any FSN.
    ASSERT_EQ(callback_invocations.size(), 1);
    EXPECT_EQ(callback_invocations.at(0).requestId, 151);

    std::size_t old_request_notifications = 0;
    for (const auto& notification : firmware_status_notifications()) {
        if (notification.contains("requestId") and notification.at("requestId") == 150) {
            old_request_notifications++;
        }
    }
    // Only the initial DownloadScheduled for the old request, nothing after the supersede.
    EXPECT_EQ(old_request_notifications, 1);
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
