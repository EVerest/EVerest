// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <atomic>
#include <chrono>
#include <functional>
#include <optional>
#include <thread>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <boost/asio/io_context.hpp>

#include <component_state_manager_mock.hpp>
#include <connectivity_manager_mock.hpp>
#include <device_model_test_helper.hpp>
#include <evse_manager_fake.hpp>
#include <evse_security_mock.hpp>
#include <message_dispatcher_mock.hpp>
#include <meter_values_mock.hpp>
#include <mocks/database_handler_mock.hpp>

#include <ocpp/common/message_queue.hpp>
#include <ocpp/v2/ctrlr_component_variables.hpp>
#include <ocpp/v2/device_model.hpp>
#include <ocpp/v2/functional_blocks/authorization.hpp>
#include <ocpp/v2/functional_blocks/availability.hpp>
#include <ocpp/v2/functional_blocks/functional_block_context.hpp>
#include <ocpp/v2/functional_blocks/smart_charging.hpp>
#include <ocpp/v2/functional_blocks/tariff_and_cost.hpp>
#include <ocpp/v2/functional_blocks/transaction.hpp>

#include <ocpp/v2/messages/Authorize.hpp>
#include <ocpp/v2/messages/GetCompositeSchedule.hpp>
#include <ocpp/v2/messages/NotifyEVChargingNeeds.hpp>
#include <ocpp/v2/messages/SetChargingProfile.hpp>
#include <ocpp/v2/ocpp_types.hpp>

using namespace ocpp;
using namespace ocpp::v2;

namespace {

// Stores the registered remote-start predicate so tests can invoke it and observe that the TransactionBlock clears it
// on destruction (M-a4 teardown safety). Only set_remote_start_pending_check carries behavior; the rest are inert
// stubs (the transaction block under test never calls them).
class AuthorizationSpy : public AuthorizationInterface {
public:
    std::function<bool(const IdToken&)> remote_start_check;

    void set_remote_start_pending_check(std::function<bool(const IdToken&)> check) override {
        this->remote_start_check = std::move(check);
    }

    void handle_message(const ocpp::EnhancedMessage<MessageType>&) override {
    }
    void start_auth_cache_cleanup_thread() override {
    }
    AuthorizeResponse authorize_req(const IdToken, const std::optional<CiString<10000>>&,
                                    const std::optional<std::vector<ocpp::v2::OCSPRequestData>>&) override {
        return AuthorizeResponse{};
    }
    void trigger_authorization_cache_cleanup() override {
    }
    void update_authorization_cache_size() override {
    }
    bool is_auth_cache_ctrlr_enabled() override {
        return false;
    }
    void authorization_cache_insert_entry(const std::string&, const IdTokenInfo&) override {
    }
    std::optional<AuthorizationCacheEntry> authorization_cache_get_entry(const std::string&) override {
        return std::nullopt;
    }
    void authorization_cache_delete_entry(const std::string&) override {
    }
    AuthorizeResponse validate_token(const IdToken, const std::optional<CiString<10000>>&,
                                     const std::optional<std::vector<ocpp::v2::OCSPRequestData>>&) override {
        return AuthorizeResponse{};
    }
};

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

IdToken make_token(const std::string& value) {
    IdToken token;
    token.type = IdTokenEnumStringType::ISO14443;
    token.idToken = value;
    return token;
}

} // namespace

class TransactionBlockTest : public ::testing::Test {
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

    AuthorizationSpy authorization;
    ::testing::NiceMock<AvailabilityMock> availability;
    ::testing::NiceMock<SmartChargingBlockMock> smart_charging;
    ::testing::NiceMock<MeterValuesMock> meter_values;

    boost::asio::io_context io_context;
    std::optional<TariffMessageCallback> tariff_message_cb;
    std::optional<SetRunningCostCallback> set_running_cost_cb;
    std::optional<DefaultPriceCallback> default_price_cb;

    std::unique_ptr<FunctionalBlockContext> fb_context;
    std::unique_ptr<MessageQueue<MessageType>> message_queue;
    std::unique_ptr<TariffAndCost> tariff_and_cost;
    std::unique_ptr<TransactionBlock> transaction;

    TransactionBlockTest() : dm_helper() {
        dm = dm_helper.get_device_model();
        evse_manager = std::make_unique<EvseManagerFake>(1);

        fb_context =
            std::make_unique<FunctionalBlockContext>(mock_dispatcher, *dm, connectivity_manager, *evse_manager,
                                                     db_handler, evse_security, component_state_manager, ocpp_version);

        MessageQueueConfig<MessageType> mq_config;
        message_queue = std::make_unique<MessageQueue<MessageType>>([](json) { return false; }, mq_config, nullptr);

        tariff_and_cost = std::make_unique<TariffAndCost>(*fb_context, meter_values, tariff_message_cb,
                                                          set_running_cost_cb, default_price_cb, io_context);

        transaction = std::make_unique<TransactionBlock>(
            *fb_context, *message_queue, authorization, availability, smart_charging, *tariff_and_cost,
            [](std::int32_t, const ReasonEnum&) { return RequestStartStopStatusEnum::Accepted; }, // stop_transaction
            [](std::int32_t) {},                                                                  // pause_charging
            std::nullopt,                                                                         // transaction_event
            std::nullopt,                                                // transaction_event_response
            [](std::optional<const std::int32_t>, const ResetEnum&) {}); // reset
    }

    void set_ev_connection_timeout(int seconds) {
        ASSERT_EQ(dm->set_value(ControllerComponentVariables::EVConnectionTimeOut.component,
                                ControllerComponentVariables::EVConnectionTimeOut.variable.value(),
                                AttributeEnum::Actual, std::to_string(seconds), "test", true),
                  SetVariableStatusEnum::Accepted);
    }
};

// H5: a fresh accepted remote start (within EVConnectionTimeOut) makes the token await a remote start.
TEST_F(TransactionBlockTest, fresh_entry_awaits_remote_start) {
    this->set_ev_connection_timeout(120);
    const auto token = make_token("fresh_token");

    this->transaction->set_remote_start_id_for_evse(1, token, 42);

    EXPECT_TRUE(this->transaction->is_id_token_awaiting_remote_start(token));
    EXPECT_FALSE(this->transaction->is_id_token_awaiting_remote_start(make_token("other_token")));
}

// H5: an entry older than EVConnectionTimeOut must no longer trigger the bypass and must be lazily evicted.
TEST_F(TransactionBlockTest, expired_entry_does_not_await_remote_start) {
    this->set_ev_connection_timeout(0);
    const auto token = make_token("stale_token");

    this->transaction->set_remote_start_id_for_evse(1, token, 42);
    // With a 0s window any elapsed time expires the entry; make the elapsed time strictly positive.
    std::this_thread::sleep_for(std::chrono::milliseconds(5));

    EXPECT_FALSE(this->transaction->is_id_token_awaiting_remote_start(token));
    // A second lookup confirms the entry was evicted (still false, would still be false anyway).
    EXPECT_FALSE(this->transaction->is_id_token_awaiting_remote_start(token));
}

// L5(a): the real predicate registered on the authorization block (mirroring the charge_point wiring) reflects
// set_remote_start_id_for_evse.
TEST_F(TransactionBlockTest, registered_predicate_reflects_pending_remote_start) {
    this->set_ev_connection_timeout(120);
    this->authorization.set_remote_start_pending_check([&transaction = *this->transaction](const IdToken& id_token) {
        return transaction.is_id_token_awaiting_remote_start(id_token);
    });
    const auto token = make_token("wired_token");

    ASSERT_TRUE(this->authorization.remote_start_check != nullptr);
    EXPECT_FALSE(this->authorization.remote_start_check(token));

    this->transaction->set_remote_start_id_for_evse(1, token, 7);
    EXPECT_TRUE(this->authorization.remote_start_check(token));
}

// M-a4: destroying the TransactionBlock clears the predicate on the authorization block, so authorization (which
// outlives it) can never call a dangling target.
TEST_F(TransactionBlockTest, destructor_clears_registered_predicate) {
    this->authorization.set_remote_start_pending_check([&transaction = *this->transaction](const IdToken& id_token) {
        return transaction.is_id_token_awaiting_remote_start(id_token);
    });
    ASSERT_TRUE(this->authorization.remote_start_check != nullptr);

    this->transaction.reset();

    EXPECT_TRUE(this->authorization.remote_start_check == nullptr);
}
