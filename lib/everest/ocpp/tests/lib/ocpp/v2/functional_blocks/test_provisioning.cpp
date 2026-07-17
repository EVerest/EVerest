// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <atomic>
#include <optional>

#include <boost/asio/io_context.hpp>

#include <component_state_manager_mock.hpp>
#include <connectivity_manager_mock.hpp>
#include <device_model_test_helper.hpp>
#include <evse_manager_fake.hpp>
#include <evse_security_mock.hpp>
#include <message_dispatcher_mock.hpp>
#include <mocks/database_handler_mock.hpp>
#include <ocsp_updater_mock.hpp>

#include <ocpp/common/message_queue.hpp>
#include <ocpp/common/types.hpp>
#include <ocpp/v2/device_model.hpp>
#include <ocpp/v2/functional_blocks/availability.hpp>
#include <ocpp/v2/functional_blocks/diagnostics.hpp>
#include <ocpp/v2/functional_blocks/functional_block_context.hpp>
#include <ocpp/v2/functional_blocks/meter_values.hpp>
#include <ocpp/v2/functional_blocks/provisioning.hpp>
#include <ocpp/v2/functional_blocks/security.hpp>
#include <ocpp/v2/functional_blocks/tariff_and_cost.hpp>
#include <ocpp/v2/functional_blocks/transaction.hpp>
#include <ocpp/v2/messages/Get15118EVCertificate.hpp>
#include <ocpp/v2/messages/Reset.hpp>
#include <ocpp/v2/ocpp_types.hpp>

using namespace ocpp;
using namespace ocpp::v2;

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

class MeterValuesMock : public MeterValuesInterface {
public:
    MOCK_METHOD(void, handle_message, (const ocpp::EnhancedMessage<MessageType>&), (override));
    MOCK_METHOD(void, update_aligned_data_interval, (), (override));
    MOCK_METHOD(void, on_meter_value, (std::int32_t, const MeterValue&), (override));
    MOCK_METHOD(MeterValue, get_latest_meter_value_filtered,
                (const MeterValue&, ReadingContextEnum, const RequiredComponentVariable&), (override));
    MOCK_METHOD(void, meter_values_req, (std::int32_t, const std::vector<MeterValue>&, bool), (override));
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

class DiagnosticsMock : public DiagnosticsInterface {
public:
    MOCK_METHOD(void, handle_message, (const ocpp::EnhancedMessage<MessageType>&), (override));
    MOCK_METHOD(void, notify_event_req, (const std::vector<EventData>&), (override));
    MOCK_METHOD(void, stop_monitoring, (), (override));
    MOCK_METHOD(void, start_monitoring, (), (override));
    MOCK_METHOD(void, process_triggered_monitors, (), (override));
};

class TransactionMock : public TransactionInterface {
public:
    MOCK_METHOD(void, handle_message, (const ocpp::EnhancedMessage<MessageType>&), (override));
    MOCK_METHOD(void, on_transaction_started,
                (std::int32_t, std::int32_t, const std::string&, const DateTime&, TriggerReasonEnum, const MeterValue&,
                 const std::optional<IdToken>&, const std::optional<IdToken>&, const std::optional<std::int32_t>&,
                 const std::optional<std::int32_t>&, ChargingStateEnum),
                (override));
    MOCK_METHOD(void, on_transaction_finished,
                (std::int32_t, const DateTime&, const MeterValue&, ReasonEnum, TriggerReasonEnum,
                 const std::optional<IdToken>&, const std::optional<std::string>&, ChargingStateEnum,
                 const std::optional<SignedMeterValue>&),
                (override));
    MOCK_METHOD(void, transaction_event_req,
                (const TransactionEventEnum&, const DateTime&, const Transaction&, const TriggerReasonEnum&,
                 std::int32_t, const std::optional<std::int32_t>&, const std::optional<EVSE>&,
                 const std::optional<IdToken>&, const std::optional<std::vector<MeterValue>>&,
                 const std::optional<std::int32_t>&, bool, const std::optional<std::int32_t>&, bool),
                (override));
    MOCK_METHOD(void, set_remote_start_id_for_evse, (std::int32_t, IdToken, std::int32_t), (override));
    MOCK_METHOD(bool, is_id_token_awaiting_remote_start, (const IdToken&), (const, override));
    MOCK_METHOD(void, schedule_reset, (std::optional<std::int32_t>), (override));
};

// An accepted, imminent whole-station reset must suppress auto-reconnect (TC_A_10_CS) without
// closing the live websocket, so a queued TransactionEvent(Ended) can still flush before the
// reboot. Scheduled and rejected resets must leave the connection untouched.
class ProvisioningResetTest : public ::testing::Test {
protected:
    DeviceModelTestHelper dm_helper;
    DeviceModel* dm{nullptr};

    ::testing::NiceMock<MockMessageDispatcher> mock_dispatcher;
    ::testing::NiceMock<ocpp::ConnectivityManagerMock> connectivity_manager;
    std::unique_ptr<EvseManagerFake> evse_manager;
    ::testing::NiceMock<DatabaseHandlerMock> db_handler;
    ocpp::EvseSecurityMock evse_security;
    ::testing::NiceMock<ComponentStateManagerMock> component_state_manager;
    std::atomic<OcppProtocolVersion> ocpp_version{OcppProtocolVersion::v201};

    ::testing::NiceMock<OcspUpdaterMock> ocsp_updater;
    ::testing::NiceMock<AvailabilityMock> availability;
    ::testing::NiceMock<MeterValuesMock> meter_values;
    ::testing::NiceMock<SecurityMock> security;
    ::testing::NiceMock<DiagnosticsMock> diagnostics;
    ::testing::NiceMock<TransactionMock> transaction;
    std::atomic<RegistrationStatusEnum> registration_status{RegistrationStatusEnum::Accepted};

    boost::asio::io_context io_context;
    std::optional<TariffMessageCallback> tariff_message_cb;
    std::optional<SetRunningCostCallback> set_running_cost_cb;
    std::optional<DefaultPriceCallback> default_price_cb;

    std::unique_ptr<FunctionalBlockContext> fb_context;
    std::unique_ptr<MessageQueue<MessageType>> message_queue;
    std::unique_ptr<TariffAndCost> tariff_and_cost;
    std::unique_ptr<Provisioning> provisioning;

    // Controls / observations for the reset path.
    bool allow_reset{true};
    std::optional<std::optional<std::int32_t>> reset_callback_evse_id;

    ProvisioningResetTest() : dm_helper() {
        dm = dm_helper.get_device_model();
        evse_manager = std::make_unique<EvseManagerFake>(1);

        fb_context =
            std::make_unique<FunctionalBlockContext>(mock_dispatcher, *dm, connectivity_manager, *evse_manager,
                                                     db_handler, evse_security, component_state_manager, ocpp_version);

        MessageQueueConfig<MessageType> mq_config;
        message_queue = std::make_unique<MessageQueue<MessageType>>([](json) { return false; }, mq_config, nullptr);

        tariff_and_cost = std::make_unique<TariffAndCost>(*fb_context, meter_values, tariff_message_cb,
                                                          set_running_cost_cb, default_price_cb, io_context);

        provisioning = std::make_unique<Provisioning>(
            *fb_context, *message_queue, ocsp_updater, availability, meter_values, security, diagnostics, transaction,
            std::nullopt, // time_sync_callback
            std::nullopt, // boot_notification_callback
            std::nullopt, // validate_network_profile_callback
            [this](auto, auto) { return this->allow_reset; },
            [this](std::optional<std::int32_t> evse_id, auto) { this->reset_callback_evse_id = evse_id; },
            [](auto, auto) { return RequestStartStopStatusEnum::Accepted; }, // stop_transaction
            std::nullopt,                                                    // variable_changed_callback
            *tariff_and_cost, registration_status);
    }

    ocpp::EnhancedMessage<MessageType> make_reset_message(ResetEnum type,
                                                          std::optional<std::int32_t> evse_id = std::nullopt) {
        ResetRequest request;
        request.type = type;
        request.evseId = evse_id;
        ocpp::Call<ResetRequest> call(request);
        ocpp::EnhancedMessage<MessageType> enhanced_message;
        enhanced_message.messageType = MessageType::Reset;
        enhanced_message.message = call;
        return enhanced_message;
    }
};

// TC_A_10_CS: Reset(Immediate) whole-station accepted with no active transaction. Auto-reconnect must be suppressed
// (via suppress_reconnect()) so the CSMS-side close after ResetResponse is not redialed before the reboot. The live
// socket must NOT be force-closed here (that is disconnect()'s job); the reboot path closes it.
TEST_F(ProvisioningResetTest, ImmediateWholeStationResetSuppressesReconnect) {
    EXPECT_CALL(connectivity_manager, suppress_reconnect()).Times(1);
    EXPECT_CALL(connectivity_manager, disconnect()).Times(0);
    provisioning->handle_message(make_reset_message(ResetEnum::Immediate));
    EXPECT_TRUE(reset_callback_evse_id.has_value()) << "reset_callback must still fire for an accepted reset";
}

// K01 / TC_B_22_CS regression guard: Reset(Immediate) whole-station accepted WITH an ongoing transaction. The
// transaction is stopped (ImmediateReset) and reconnect is suppressed, but the websocket must stay open so the
// graceful TransactionEvent(Ended) can flush before the reboot. Therefore disconnect() (which closes the socket
// immediately) must NOT be called, only suppress_reconnect().
TEST_F(ProvisioningResetTest, ImmediateWholeStationResetWithTransactionKeepsSocketOpen) {
    evse_manager->open_transaction(1, "test-transaction");
    EXPECT_CALL(connectivity_manager, suppress_reconnect()).Times(1);
    EXPECT_CALL(connectivity_manager, disconnect()).Times(0);
    provisioning->handle_message(make_reset_message(ResetEnum::Immediate));
    EXPECT_TRUE(reset_callback_evse_id.has_value()) << "reset_callback must still fire for an accepted reset";
}

// Reset(OnIdle) with an active transaction is scheduled, not imminent: connection must stay fully up (no suppression).
TEST_F(ProvisioningResetTest, ScheduledResetKeepsConnectionAlive) {
    evse_manager->open_transaction(1, "test-transaction");
    EXPECT_CALL(connectivity_manager, suppress_reconnect()).Times(0);
    EXPECT_CALL(connectivity_manager, disconnect()).Times(0);
    EXPECT_CALL(transaction, schedule_reset(::testing::_)).Times(1);
    provisioning->handle_message(make_reset_message(ResetEnum::OnIdle));
    EXPECT_FALSE(reset_callback_evse_id.has_value()) << "scheduled reset must not fire the reset callback yet";
}

// Evse-specific reset does not reboot the whole station, so the CSMS connection must not be touched.
TEST_F(ProvisioningResetTest, EvseSpecificResetDoesNotSuppressReconnect) {
    EXPECT_CALL(connectivity_manager, suppress_reconnect()).Times(0);
    EXPECT_CALL(connectivity_manager, disconnect()).Times(0);
    provisioning->handle_message(make_reset_message(ResetEnum::Immediate, 1));
}

// Rejected reset must leave the connection untouched (regression guard for the no-reset baseline).
TEST_F(ProvisioningResetTest, RejectedResetDoesNotSuppressReconnect) {
    allow_reset = false;
    EXPECT_CALL(connectivity_manager, suppress_reconnect()).Times(0);
    EXPECT_CALL(connectivity_manager, disconnect()).Times(0);
    provisioning->handle_message(make_reset_message(ResetEnum::Immediate));
    EXPECT_FALSE(reset_callback_evse_id.has_value()) << "rejected reset must not fire the reset callback";
}

// Hot reconfiguration of the per-slot MessageTimeout: a write to the active NetworkConfiguration
// slot must be applied to the live connection immediately (no reconnect), a write to any other
// slot is only picked up on the next connect.
using ProvisioningVariableChangedTest = ProvisioningResetTest;

namespace {
SetVariableData make_slot_message_timeout(const std::string& slot, const std::string& value) {
    SetVariableData data;
    data.component.name = "NetworkConfiguration";
    data.component.instance = slot;
    data.variable.name = "MessageTimeout";
    data.attributeValue = value;
    return data;
}

void set_active_network_profile(DeviceModel& dm, const std::string& slot) {
    ASSERT_TRUE(ControllerComponentVariables::ActiveNetworkProfile.variable.has_value());
    ASSERT_EQ(dm.set_read_only_value(ControllerComponentVariables::ActiveNetworkProfile.component,
                                     ControllerComponentVariables::ActiveNetworkProfile.variable.value(),
                                     AttributeEnum::Actual, slot, "test"),
              SetVariableStatusEnum::Accepted);
}
} // namespace

TEST_F(ProvisioningVariableChangedTest, MessageTimeoutOnActiveSlotAppliesWithoutReconnect) {
    set_active_network_profile(*dm, "1");
    EXPECT_CALL(connectivity_manager, set_websocket_connection_options_without_reconnect()).Times(1);
    provisioning->on_variable_changed(make_slot_message_timeout("1", "45"));
}

TEST_F(ProvisioningVariableChangedTest, MessageTimeoutOnInactiveSlotIsDeferred) {
    set_active_network_profile(*dm, "1");
    EXPECT_CALL(connectivity_manager, set_websocket_connection_options_without_reconnect()).Times(0);
    provisioning->on_variable_changed(make_slot_message_timeout("2", "45"));
}

} // namespace
