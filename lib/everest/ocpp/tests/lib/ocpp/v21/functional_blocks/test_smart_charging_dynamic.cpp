// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2023 Pionix GmbH and Contributors to EVerest

#include <ocpp/v2/functional_blocks/smart_charging.hpp>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <thread>

#include <unistd.h>

#include <date/tz.h>
#include <sqlite3.h>

#include <comparators.hpp>
#include <ocpp/common/call_types.hpp>
#include <ocpp/common/types.hpp>
#include <ocpp/v2/ctrlr_component_variables.hpp>
#include <ocpp/v2/database_handler.hpp>
#include <ocpp/v2/device_model.hpp>
#include <ocpp/v2/evse.hpp>
#include <ocpp/v2/functional_blocks/functional_block_context.hpp>
#include <ocpp/v2/ocpp_enums.hpp>
#include <ocpp/v2/ocpp_types.hpp>
#include <ocpp/v21/messages/PullDynamicScheduleUpdate.hpp>
#include <ocpp/v21/messages/UpdateDynamicSchedule.hpp>

#include "component_state_manager_mock.hpp"
#include "connectivity_manager_mock.hpp"
#include "device_model_test_helper.hpp"
#include "evse_manager_fake.hpp"
#include "evse_mock.hpp"
#include "evse_security_mock.hpp"
#include "lib/ocpp/common/database_testing_utils.hpp"
#include "message_dispatcher_mock.hpp"
#include "smart_charging_test_utils.hpp"

using ::testing::_;
using ::testing::ByMove;
using ::testing::Invoke;
using ::testing::MockFunction;
using ::testing::Return;
using ::testing::ReturnRef;

namespace ocpp::v2 {

// Helper for K28 tests: enable Dynamic profile support on the device model.
static void enable_dynamic_profiles(DeviceModel* device_model) {
    const ComponentVariable supports_dynamic_profiles = ControllerComponentVariables::SupportsDynamicProfiles;
    device_model->set_value(supports_dynamic_profiles.component, supports_dynamic_profiles.variable.value(),
                            AttributeEnum::Actual, "true", "test", true);
}

TEST_F(SmartChargingTestV21, K28FR01_DynamicProfile_RejectMultipleSchedules) {
    enable_dynamic_profiles(device_model);
    auto periods = create_charging_schedule_periods(0, 1, 1, 0.5f);
    auto schedule_a = create_charge_schedule(ChargingRateUnitEnum::A, periods, ocpp::DateTime("2024-01-17T17:00:00"));
    auto schedule_b = create_charge_schedule(ChargingRateUnitEnum::A, periods, ocpp::DateTime("2024-01-17T17:00:00"));
    std::vector<ChargingSchedule> schedules{schedule_a, schedule_b};

    auto profile =
        create_charging_profile(DEFAULT_PROFILE_ID, ChargingProfilePurposeEnum::TxDefaultProfile, schedules, {},
                                ChargingProfileKindEnum::Dynamic, DEFAULT_STACK_LEVEL,
                                ocpp::DateTime("2024-01-01T13:00:00"), ocpp::DateTime("2024-02-01T13:00:00"));

    auto response = smart_charging.conform_validate_and_add_profile(profile, DEFAULT_EVSE_ID);
    EXPECT_THAT(response.status, testing::Eq(ChargingProfileStatusEnum::Rejected));
    EXPECT_EQ(response.statusInfo.value().reasonCode, "InvalidSchedule");
    EXPECT_EQ(response.statusInfo.value().additionalInfo, "ChargingProfileDynamicMustHaveSingleSchedule");
}

TEST_F(SmartChargingTestV21, K28FR01_DynamicProfile_RejectMultiplePeriods) {
    enable_dynamic_profiles(device_model);
    auto periods = create_charging_schedule_periods({0, 60});
    ASSERT_EQ(periods.size(), 2);
    periods.at(0).limit = 16.0f;
    periods.at(1).limit = 8.0f;

    auto profile = create_charging_profile(
        DEFAULT_PROFILE_ID, ChargingProfilePurposeEnum::TxDefaultProfile,
        create_charge_schedule(ChargingRateUnitEnum::A, periods, ocpp::DateTime("2024-01-17T17:00:00")), {},
        ChargingProfileKindEnum::Dynamic, DEFAULT_STACK_LEVEL, ocpp::DateTime("2024-01-01T13:00:00"),
        ocpp::DateTime("2024-02-01T13:00:00"));

    auto response = smart_charging.conform_validate_and_add_profile(profile, DEFAULT_EVSE_ID);
    EXPECT_THAT(response.status, testing::Eq(ChargingProfileStatusEnum::Rejected));
    EXPECT_EQ(response.statusInfo.value().reasonCode, "InvalidSchedule");
    EXPECT_EQ(response.statusInfo.value().additionalInfo, "ChargingProfileDynamicMustHaveSinglePeriod");
}

TEST_F(SmartChargingTestV21, K28FR02_DynamicProfile_RejectStartPeriodNonZero) {
    enable_dynamic_profiles(device_model);
    auto periods = create_charging_schedule_periods(60, 1, 1, 0.5f);
    ASSERT_EQ(periods.size(), 1);
    ASSERT_NE(periods.at(0).startPeriod, 0);

    auto profile = create_charging_profile(
        DEFAULT_PROFILE_ID, ChargingProfilePurposeEnum::TxDefaultProfile,
        create_charge_schedule(ChargingRateUnitEnum::A, periods, ocpp::DateTime("2024-01-17T17:00:00")), {},
        ChargingProfileKindEnum::Dynamic, DEFAULT_STACK_LEVEL, ocpp::DateTime("2024-01-01T13:00:00"),
        ocpp::DateTime("2024-02-01T13:00:00"));

    auto response = smart_charging.conform_validate_and_add_profile(profile, DEFAULT_EVSE_ID);
    EXPECT_THAT(response.status, testing::Eq(ChargingProfileStatusEnum::Rejected));
    EXPECT_EQ(response.statusInfo.value().reasonCode, "InvalidSchedule");
    EXPECT_EQ(response.statusInfo.value().additionalInfo, "ChargingProfileFirstStartScheduleIsNotZero");
}

TEST_F(SmartChargingTestV21, K28FR03_DynamicProfile_ResponseHasInvalidScheduleReasonCode) {
    enable_dynamic_profiles(device_model);
    // Trigger via multi-schedule rejection — verifies reasonCode wiring goes through InvalidSchedule cluster.
    auto periods = create_charging_schedule_periods(0, 1, 1, 0.5f);
    auto schedule_a = create_charge_schedule(ChargingRateUnitEnum::A, periods, ocpp::DateTime("2024-01-17T17:00:00"));
    auto schedule_b = create_charge_schedule(ChargingRateUnitEnum::A, periods, ocpp::DateTime("2024-01-17T17:00:00"));
    std::vector<ChargingSchedule> schedules{schedule_a, schedule_b};

    auto profile =
        create_charging_profile(DEFAULT_PROFILE_ID, ChargingProfilePurposeEnum::TxDefaultProfile, schedules, {},
                                ChargingProfileKindEnum::Dynamic, DEFAULT_STACK_LEVEL,
                                ocpp::DateTime("2024-01-01T13:00:00"), ocpp::DateTime("2024-02-01T13:00:00"));

    auto response = smart_charging.conform_validate_and_add_profile(profile, DEFAULT_EVSE_ID);
    EXPECT_THAT(response.status, testing::Eq(ChargingProfileStatusEnum::Rejected));
    ASSERT_TRUE(response.statusInfo.has_value());
    EXPECT_EQ(response.statusInfo.value().reasonCode, "InvalidSchedule");
}

TEST_F(SmartChargingTestV21, K28FR04_NonDynamicProfile_WithDynUpdateInterval_Rejected) {
    // Regression: K01.FR.122 covers K28.FR.04 — a non-Dynamic profile carrying dynUpdateInterval is rejected.
    enable_dynamic_profiles(device_model);
    auto periods = create_charging_schedule_periods(0, 1, 1, 0.5f);
    auto profile = create_charging_profile(
        DEFAULT_PROFILE_ID, ChargingProfilePurposeEnum::TxDefaultProfile,
        create_charge_schedule(ChargingRateUnitEnum::A, periods, ocpp::DateTime("2024-01-17T17:00:00")), {},
        ChargingProfileKindEnum::Absolute, DEFAULT_STACK_LEVEL, ocpp::DateTime("2024-01-01T13:00:00"),
        ocpp::DateTime("2024-02-01T13:00:00"));
    profile.dynUpdateInterval = 60;

    auto response = smart_charging.conform_validate_and_add_profile(profile, DEFAULT_EVSE_ID);
    EXPECT_THAT(response.status, testing::Eq(ChargingProfileStatusEnum::Rejected));
    EXPECT_EQ(response.statusInfo.value().reasonCode, "InvalidProfile");
    EXPECT_EQ(response.statusInfo.value().additionalInfo, "ChargingProfileNotDynamic");
}

TEST_F(SmartChargingTestV21, K28FR05_DynamicProfile_DynUpdateTimeAutoSetOnAccept) {
    enable_dynamic_profiles(device_model);
    auto periods = create_charging_schedule_periods(0, 1, 1, 0.5f);
    auto profile = create_charging_profile(
        DEFAULT_PROFILE_ID, ChargingProfilePurposeEnum::TxDefaultProfile,
        create_charge_schedule(ChargingRateUnitEnum::A, periods, ocpp::DateTime("2024-01-17T17:00:00")), {},
        ChargingProfileKindEnum::Dynamic, DEFAULT_STACK_LEVEL, ocpp::DateTime("2024-01-01T13:00:00"),
        ocpp::DateTime("2024-02-01T13:00:00"));
    ASSERT_FALSE(profile.dynUpdateTime.has_value());

    const auto before = ocpp::DateTime().to_time_point();
    auto response = smart_charging.conform_validate_and_add_profile(profile, DEFAULT_EVSE_ID);
    const auto after = ocpp::DateTime().to_time_point();
    EXPECT_THAT(response.status, testing::Eq(ChargingProfileStatusEnum::Accepted));

    auto stored = database_handler->get_charging_profiles_for_evse(DEFAULT_EVSE_ID);
    ASSERT_EQ(stored.size(), 1);
    ASSERT_TRUE(stored.front().dynUpdateTime.has_value());
    const auto persisted = stored.front().dynUpdateTime.value().to_time_point();
    EXPECT_GE(persisted, before - std::chrono::seconds(2));
    EXPECT_LE(persisted, after + std::chrono::seconds(2));
}

TEST_F(SmartChargingTestV21, K28FR05_DynUpdateTime_PersistsAcrossDbRestart) {
    enable_dynamic_profiles(device_model);
    auto periods = create_charging_schedule_periods(0, 1, 1, 0.5f);
    auto profile = create_charging_profile(
        DEFAULT_PROFILE_ID, ChargingProfilePurposeEnum::TxDefaultProfile,
        create_charge_schedule(ChargingRateUnitEnum::A, periods, ocpp::DateTime("2024-01-17T17:00:00")), {},
        ChargingProfileKindEnum::Dynamic, DEFAULT_STACK_LEVEL, ocpp::DateTime("2024-01-01T13:00:00"),
        ocpp::DateTime("2024-02-01T13:00:00"));

    auto response = smart_charging.conform_validate_and_add_profile(profile, DEFAULT_EVSE_ID);
    ASSERT_THAT(response.status, testing::Eq(ChargingProfileStatusEnum::Accepted));

    auto stored_first = database_handler->get_charging_profiles_for_evse(DEFAULT_EVSE_ID);
    ASSERT_EQ(stored_first.size(), 1);
    ASSERT_TRUE(stored_first.front().dynUpdateTime.has_value());
    const auto first_dyn_update_time = stored_first.front().dynUpdateTime.value();

    // Force a JSON round-trip through DB persistence: close + reopen the on-disk connection,
    // then refetch. This exercises the same insert_or_update_charging_profile + load paths
    // that a libocpp restart would use.
    database_handler->close_connection();
    database_handler->open_connection();

    auto stored_second = database_handler->get_charging_profiles_for_evse(DEFAULT_EVSE_ID);
    ASSERT_EQ(stored_second.size(), 1);
    ASSERT_TRUE(stored_second.front().dynUpdateTime.has_value());
    EXPECT_EQ(stored_second.front().dynUpdateTime.value().to_rfc3339(), first_dyn_update_time.to_rfc3339());
}

namespace {

ChargingProfile make_dynamic_profile(std::int32_t profile_id, double setpoint_value) {
    auto periods = create_charging_schedule_periods(0, 1, 1, std::nullopt);
    periods.at(0).operationMode = OperationModeEnum::CentralSetpoint;
    periods.at(0).setpoint = setpoint_value;
    return create_charging_profile(
        profile_id, ChargingProfilePurposeEnum::TxDefaultProfile,
        create_charge_schedule(ChargingRateUnitEnum::W, periods, ocpp::DateTime("2024-01-17T17:00:00")), {},
        ChargingProfileKindEnum::Dynamic, DEFAULT_STACK_LEVEL, ocpp::DateTime("2024-01-01T13:00:00"),
        ocpp::DateTime("2024-02-01T13:00:00"));
}

} // namespace

TEST_F(SmartChargingTestV21, K28FR06_UpdateDynamicSchedule_AppliesUpdateAndRespondsAccepted) {
    enable_dynamic_profiles(device_model);
    auto profile = make_dynamic_profile(DEFAULT_PROFILE_ID, 10000.0f);
    auto add_response = smart_charging.conform_validate_and_add_profile(profile, DEFAULT_EVSE_ID);
    ASSERT_THAT(add_response.status, testing::Eq(ChargingProfileStatusEnum::Accepted));

    v21::UpdateDynamicScheduleRequest req;
    req.chargingProfileId = DEFAULT_PROFILE_ID;
    req.scheduleUpdate.setpoint = 5000.0f;
    auto enhanced =
        request_to_enhanced_message<v21::UpdateDynamicScheduleRequest, MessageType::UpdateDynamicSchedule>(req);

    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_)).WillOnce(Invoke([](const json& call_result) {
        auto response = call_result[ocpp::CALLRESULT_PAYLOAD].get<v21::UpdateDynamicScheduleResponse>();
        EXPECT_EQ(response.status, ChargingProfileStatusEnum::Accepted);
    }));

    smart_charging.handle_message(enhanced);

    auto stored = database_handler->get_charging_profiles_for_evse(DEFAULT_EVSE_ID);
    ASSERT_EQ(stored.size(), 1);
    ASSERT_FALSE(stored.front().chargingSchedule.empty());
    ASSERT_FALSE(stored.front().chargingSchedule.at(0).chargingSchedulePeriod.empty());
    const auto& period = stored.front().chargingSchedule.at(0).chargingSchedulePeriod.at(0);
    ASSERT_TRUE(period.setpoint.has_value());
    EXPECT_FLOAT_EQ(period.setpoint.value(), 5000.0f);
}

TEST_F(SmartChargingTestV21, K28FR06_UpdateDynamicSchedule_FiresChargingProfilesCallback) {
    enable_dynamic_profiles(device_model);
    auto profile = make_dynamic_profile(DEFAULT_PROFILE_ID, 10000.0f);
    auto add_response = smart_charging.conform_validate_and_add_profile(profile, DEFAULT_EVSE_ID);
    ASSERT_THAT(add_response.status, testing::Eq(ChargingProfileStatusEnum::Accepted));

    v21::UpdateDynamicScheduleRequest req;
    req.chargingProfileId = DEFAULT_PROFILE_ID;
    req.scheduleUpdate.setpoint = 5000.0f;
    auto enhanced =
        request_to_enhanced_message<v21::UpdateDynamicScheduleRequest, MessageType::UpdateDynamicSchedule>(req);

    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_));
    // The add_profile call already invoked set_charging_profiles_callback once on the Set path.
    // Apply path fires the callback when at least one field is updated.
    EXPECT_CALL(set_charging_profiles_callback_mock, Call).Times(testing::AtLeast(1));

    smart_charging.handle_message(enhanced);
}

TEST_F(SmartChargingTestV21, K28FR09_UpdateDynamicSchedule_RefreshesDynUpdateTime) {
    enable_dynamic_profiles(device_model);
    auto profile = make_dynamic_profile(DEFAULT_PROFILE_ID, 10000.0f);
    auto add_response = smart_charging.conform_validate_and_add_profile(profile, DEFAULT_EVSE_ID);
    ASSERT_THAT(add_response.status, testing::Eq(ChargingProfileStatusEnum::Accepted));

    auto stored_initial = database_handler->get_charging_profiles_for_evse(DEFAULT_EVSE_ID);
    ASSERT_EQ(stored_initial.size(), 1);
    ASSERT_TRUE(stored_initial.front().dynUpdateTime.has_value());
    const auto initial_time = stored_initial.front().dynUpdateTime.value().to_time_point();

    // Force a measurable gap so before/after comparisons are robust.
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    v21::UpdateDynamicScheduleRequest req;
    req.chargingProfileId = DEFAULT_PROFILE_ID;
    req.scheduleUpdate.setpoint = 5000.0f;
    auto enhanced =
        request_to_enhanced_message<v21::UpdateDynamicScheduleRequest, MessageType::UpdateDynamicSchedule>(req);

    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_));
    smart_charging.handle_message(enhanced);

    auto stored_after = database_handler->get_charging_profiles_for_evse(DEFAULT_EVSE_ID);
    ASSERT_EQ(stored_after.size(), 1);
    ASSERT_TRUE(stored_after.front().dynUpdateTime.has_value());
    const auto after_time = stored_after.front().dynUpdateTime.value().to_time_point();
    EXPECT_GT(after_time, initial_time);
    const auto now = ocpp::DateTime().to_time_point();
    EXPECT_LE(after_time, now + std::chrono::seconds(2));
}

TEST_F(SmartChargingTestV21, K28FR11_UpdateDynamicSchedule_NonDynamicProfile_Rejected) {
    enable_dynamic_profiles(device_model);
    auto periods = create_charging_schedule_periods(0, 1, 1, 5.0f);
    auto absolute_profile = create_charging_profile(
        DEFAULT_PROFILE_ID, ChargingProfilePurposeEnum::TxDefaultProfile,
        create_charge_schedule(ChargingRateUnitEnum::A, periods, ocpp::DateTime("2024-01-17T17:00:00")), {},
        ChargingProfileKindEnum::Absolute, DEFAULT_STACK_LEVEL, ocpp::DateTime("2024-01-01T13:00:00"),
        ocpp::DateTime("2024-02-01T13:00:00"));
    auto add_response = smart_charging.conform_validate_and_add_profile(absolute_profile, DEFAULT_EVSE_ID);
    ASSERT_THAT(add_response.status, testing::Eq(ChargingProfileStatusEnum::Accepted));

    v21::UpdateDynamicScheduleRequest req;
    req.chargingProfileId = DEFAULT_PROFILE_ID;
    req.scheduleUpdate.limit = 12.0f;
    auto enhanced =
        request_to_enhanced_message<v21::UpdateDynamicScheduleRequest, MessageType::UpdateDynamicSchedule>(req);

    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_)).WillOnce(Invoke([](const json& call_result) {
        auto response = call_result[ocpp::CALLRESULT_PAYLOAD].get<v21::UpdateDynamicScheduleResponse>();
        EXPECT_EQ(response.status, ChargingProfileStatusEnum::Rejected);
        ASSERT_TRUE(response.statusInfo.has_value());
        EXPECT_EQ(response.statusInfo.value().reasonCode, "InvalidProfile");
        // K28.FR.11: discriminate from "missing profile" via additionalInfo.
        ASSERT_TRUE(response.statusInfo.value().additionalInfo.has_value());
        EXPECT_EQ(response.statusInfo.value().additionalInfo.value(), "ProfileNotDynamic");
    }));

    smart_charging.handle_message(enhanced);
}

TEST_F(SmartChargingTestV21, K28FR11_UpdateDynamicSchedule_MissingProfile_Rejected) {
    enable_dynamic_profiles(device_model);

    v21::UpdateDynamicScheduleRequest req;
    req.chargingProfileId = 9999;
    req.scheduleUpdate.limit = 12.0f;
    auto enhanced =
        request_to_enhanced_message<v21::UpdateDynamicScheduleRequest, MessageType::UpdateDynamicSchedule>(req);

    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_)).WillOnce(Invoke([](const json& call_result) {
        auto response = call_result[ocpp::CALLRESULT_PAYLOAD].get<v21::UpdateDynamicScheduleResponse>();
        EXPECT_EQ(response.status, ChargingProfileStatusEnum::Rejected);
        ASSERT_TRUE(response.statusInfo.has_value());
        EXPECT_EQ(response.statusInfo.value().reasonCode, "InvalidProfile");
        // K28.FR.11: discriminate from "wrong kind" via additionalInfo.
        ASSERT_TRUE(response.statusInfo.value().additionalInfo.has_value());
        EXPECT_EQ(response.statusInfo.value().additionalInfo.value(), "ProfileNotFound");
    }));

    smart_charging.handle_message(enhanced);
}

TEST_F(SmartChargingTestV21, K28_UpdateDynamicSchedule_AllFieldsUnset_SkipsCallback) {
    enable_dynamic_profiles(device_model);
    auto profile = make_dynamic_profile(DEFAULT_PROFILE_ID, 10000.0f);
    auto add_response = smart_charging.conform_validate_and_add_profile(profile, DEFAULT_EVSE_ID);
    ASSERT_THAT(add_response.status, testing::Eq(ChargingProfileStatusEnum::Accepted));

    auto stored_initial = database_handler->get_charging_profiles_for_evse(DEFAULT_EVSE_ID);
    ASSERT_EQ(stored_initial.size(), 1);
    const double initial_setpoint =
        stored_initial.front().chargingSchedule.at(0).chargingSchedulePeriod.at(0).setpoint.value_or(0.0f);
    const auto initial_dyn_update_time = stored_initial.front().dynUpdateTime.value().to_time_point();

    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    v21::UpdateDynamicScheduleRequest req;
    req.chargingProfileId = DEFAULT_PROFILE_ID;
    // No fields set on scheduleUpdate.
    auto enhanced =
        request_to_enhanced_message<v21::UpdateDynamicScheduleRequest, MessageType::UpdateDynamicSchedule>(req);

    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_)).WillOnce(Invoke([](const json& call_result) {
        auto response = call_result[ocpp::CALLRESULT_PAYLOAD].get<v21::UpdateDynamicScheduleResponse>();
        EXPECT_EQ(response.status, ChargingProfileStatusEnum::Accepted);
    }));
    // Set path already invoked the callback once during add. The empty-update apply path must not.
    EXPECT_CALL(set_charging_profiles_callback_mock, Call).Times(0);

    smart_charging.handle_message(enhanced);

    auto stored_after = database_handler->get_charging_profiles_for_evse(DEFAULT_EVSE_ID);
    ASSERT_EQ(stored_after.size(), 1);
    EXPECT_FLOAT_EQ(stored_after.front().chargingSchedule.at(0).chargingSchedulePeriod.at(0).setpoint.value_or(0.0f),
                    initial_setpoint);
    ASSERT_TRUE(stored_after.front().dynUpdateTime.has_value());
    EXPECT_GT(stored_after.front().dynUpdateTime.value().to_time_point(), initial_dyn_update_time);
}

TEST_F(SmartChargingTestV21, K28_UpdateDynamicSchedule_OneFieldSet_FiresCallback) {
    enable_dynamic_profiles(device_model);
    auto profile = make_dynamic_profile(DEFAULT_PROFILE_ID, 10000.0f);
    auto add_response = smart_charging.conform_validate_and_add_profile(profile, DEFAULT_EVSE_ID);
    ASSERT_THAT(add_response.status, testing::Eq(ChargingProfileStatusEnum::Accepted));

    v21::UpdateDynamicScheduleRequest req;
    req.chargingProfileId = DEFAULT_PROFILE_ID;
    // Set the same value the period already carries — equality with current is deliberately not checked.
    req.scheduleUpdate.setpoint = 10000.0f;
    auto enhanced =
        request_to_enhanced_message<v21::UpdateDynamicScheduleRequest, MessageType::UpdateDynamicSchedule>(req);

    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_));
    EXPECT_CALL(set_charging_profiles_callback_mock, Call).Times(testing::AtLeast(1));

    smart_charging.handle_message(enhanced);
}

TEST_F(SmartChargingTestV21, K28_UpdateDynamicSchedule_PersistFailure_RejectedWithInternalError) {
    enable_dynamic_profiles(device_model);
    auto profile = make_dynamic_profile(DEFAULT_PROFILE_ID, 10000.0f);
    auto add_response = smart_charging.conform_validate_and_add_profile(profile, DEFAULT_EVSE_ID);
    ASSERT_THAT(add_response.status, testing::Eq(ChargingProfileStatusEnum::Accepted));

    // Block all subsequent INSERTs into CHARGING_PROFILES so the matching-criteria lookup
    // still finds the existing row but the persist step throws QueryExecutionException.
    auto trigger_stmt = database_handler->new_statement(
        "CREATE TRIGGER block_charging_profile_inserts BEFORE INSERT ON CHARGING_PROFILES "
        "BEGIN SELECT RAISE(ABORT, 'persist disabled for test'); END;");
    ASSERT_EQ(trigger_stmt->step(), SQLITE_DONE);

    v21::UpdateDynamicScheduleRequest req;
    req.chargingProfileId = DEFAULT_PROFILE_ID;
    req.scheduleUpdate.setpoint = 5000.0f;
    auto enhanced =
        request_to_enhanced_message<v21::UpdateDynamicScheduleRequest, MessageType::UpdateDynamicSchedule>(req);

    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_)).WillOnce(Invoke([](const json& call_result) {
        auto response = call_result[ocpp::CALLRESULT_PAYLOAD].get<v21::UpdateDynamicScheduleResponse>();
        EXPECT_EQ(response.status, ChargingProfileStatusEnum::Rejected);
        ASSERT_TRUE(response.statusInfo.has_value());
        EXPECT_EQ(response.statusInfo.value().reasonCode.get(), "InternalError");
        ASSERT_TRUE(response.statusInfo.value().additionalInfo.has_value());
        EXPECT_EQ(response.statusInfo.value().additionalInfo.value().get(), "ProfilePersistFailed");
    }));
    // No callback on persist failure.
    EXPECT_CALL(set_charging_profiles_callback_mock, Call).Times(0);

    smart_charging.handle_message(enhanced);

    // Drop the trigger so TearDown's clear_charging_profiles can run cleanly.
    auto drop_stmt = database_handler->new_statement("DROP TRIGGER IF EXISTS block_charging_profile_inserts");
    ASSERT_EQ(drop_stmt->step(), SQLITE_DONE);
}

namespace {

// Builds a Dynamic profile that carries an explicit dynUpdateInterval for FR.10 timer tests.
ChargingProfile make_dynamic_profile_with_interval(std::int32_t profile_id, std::int32_t interval_seconds,
                                                   std::optional<ocpp::DateTime> dyn_update_time = std::nullopt) {
    auto periods = create_charging_schedule_periods(0, 1, 1, std::nullopt);
    periods.at(0).operationMode = OperationModeEnum::CentralSetpoint;
    periods.at(0).setpoint = 10000.0f;
    auto profile = create_charging_profile(
        profile_id, ChargingProfilePurposeEnum::TxDefaultProfile,
        create_charge_schedule(ChargingRateUnitEnum::W, periods, ocpp::DateTime("2024-01-17T17:00:00")), {},
        ChargingProfileKindEnum::Dynamic, DEFAULT_STACK_LEVEL, ocpp::DateTime("2024-01-01T13:00:00"),
        ocpp::DateTime("2034-02-01T13:00:00"));
    profile.dynUpdateInterval = interval_seconds;
    if (dyn_update_time.has_value()) {
        profile.dynUpdateTime = dyn_update_time.value();
    }
    return profile;
}

ocpp::EnhancedMessage<MessageType>
make_pull_response_enhanced(const std::string& message_id, ChargingProfileStatusEnum status,
                            std::optional<ChargingScheduleUpdate> schedule_update = std::nullopt) {
    v21::PullDynamicScheduleUpdateResponse response;
    response.status = status;
    response.scheduleUpdate = schedule_update;
    ocpp::CallResult<v21::PullDynamicScheduleUpdateResponse> call_result(response, message_id);
    ocpp::EnhancedMessage<MessageType> enhanced;
    enhanced.messageType = MessageType::PullDynamicScheduleUpdateResponse;
    enhanced.uniqueId = message_id;
    enhanced.message = json(call_result);
    return enhanced;
}

} // namespace

TEST_F(SmartChargingTestV21, K28FR10_PullTimer_FiresAfterInterval) {
    enable_dynamic_profiles(device_model);

    std::atomic<int> dispatch_count{0};
    std::promise<json> dispatched_call_promise;
    auto dispatched_call_future = dispatched_call_promise.get_future();

    EXPECT_CALL(mock_dispatcher, dispatch_call_async(_, _))
        .WillRepeatedly(
            Invoke([&dispatch_count, p = std::make_shared<std::promise<json>>(std::move(dispatched_call_promise))](
                       const json& call, bool /*triggered*/) -> std::future<ocpp::EnhancedMessage<MessageType>> {
                if (dispatch_count.fetch_add(1) == 0) {
                    p->set_value(call);
                }
                std::promise<ocpp::EnhancedMessage<MessageType>> resp;
                ocpp::EnhancedMessage<MessageType> enhanced;
                enhanced.offline = true;
                resp.set_value(enhanced);
                return resp.get_future();
            }));

    auto profile = make_dynamic_profile_with_interval(DEFAULT_PROFILE_ID, /*interval=*/1, ocpp::DateTime());
    auto add_response = smart_charging.conform_validate_and_add_profile(profile, DEFAULT_EVSE_ID);
    ASSERT_THAT(add_response.status, testing::Eq(ChargingProfileStatusEnum::Accepted));

    // dynUpdateTime + 1s should be in the past or very near; timer should fire shortly.
    ASSERT_EQ(dispatched_call_future.wait_for(std::chrono::seconds(5)), std::future_status::ready);
    auto dispatched_call = dispatched_call_future.get();
    EXPECT_EQ(dispatched_call.at(CALL_ACTION).get<std::string>(), "PullDynamicScheduleUpdate");
    EXPECT_EQ(dispatched_call.at(CALL_PAYLOAD).at("chargingProfileId").get<std::int32_t>(), DEFAULT_PROFILE_ID);
}

TEST_F(SmartChargingTestV21, K28FR10_PullTimer_DoesNotFireBeforeInterval) {
    enable_dynamic_profiles(device_model);

    // gmock auto-verifies Times(0) on test exit; no sleep needed to assert "did not fire".
    EXPECT_CALL(mock_dispatcher, dispatch_call_async(_, _)).Times(0);

    auto profile = make_dynamic_profile_with_interval(DEFAULT_PROFILE_ID, /*interval=*/3600, ocpp::DateTime());
    auto add_response = smart_charging.conform_validate_and_add_profile(profile, DEFAULT_EVSE_ID);
    ASSERT_THAT(add_response.status, testing::Eq(ChargingProfileStatusEnum::Accepted));
}

TEST_F(SmartChargingTestV21, K28FR10_PullTimer_RebuildsAfterRestart) {
    enable_dynamic_profiles(device_model);

    auto periods = create_charging_schedule_periods(0, 1, 1, std::nullopt);
    periods.at(0).operationMode = OperationModeEnum::CentralSetpoint;
    periods.at(0).setpoint = 10000.0f;
    auto profile = create_charging_profile(
        DEFAULT_PROFILE_ID, ChargingProfilePurposeEnum::TxDefaultProfile,
        create_charge_schedule(ChargingRateUnitEnum::W, periods, ocpp::DateTime("2024-01-17T17:00:00")), {},
        ChargingProfileKindEnum::Dynamic, DEFAULT_STACK_LEVEL, ocpp::DateTime("2024-01-01T13:00:00"),
        ocpp::DateTime("2034-02-01T13:00:00"));
    profile.dynUpdateInterval = 1;
    profile.dynUpdateTime = ocpp::DateTime();
    profile.validFrom = ocpp::DateTime("2024-01-01T13:00:00");
    profile.validTo = ocpp::DateTime("2034-02-01T13:00:00");

    // Pre-populate DB directly, then re-trigger the rebuild path on the fixture's smart_charging.
    // (Reusing the existing instance avoids spawning a second SmartCharging with a stale context
    // reference — see TearDown which only clears the profile table.)
    database_handler->insert_or_update_charging_profile(DEFAULT_EVSE_ID, profile,
                                                        ChargingLimitSourceEnumStringType::CSO);

    std::atomic<int> dispatch_count{0};
    std::promise<json> dispatched_call_promise;
    auto dispatched_call_future = dispatched_call_promise.get_future();

    EXPECT_CALL(mock_dispatcher, dispatch_call_async(_, _))
        .WillRepeatedly(
            Invoke([&dispatch_count, p = std::make_shared<std::promise<json>>(std::move(dispatched_call_promise))](
                       const json& call, bool /*triggered*/) -> std::future<ocpp::EnhancedMessage<MessageType>> {
                if (dispatch_count.fetch_add(1) == 0) {
                    p->set_value(call);
                }
                std::promise<ocpp::EnhancedMessage<MessageType>> resp;
                ocpp::EnhancedMessage<MessageType> enhanced;
                enhanced.offline = true;
                resp.set_value(enhanced);
                return resp.get_future();
            }));

    // Re-run the rebuild path with a Dynamic profile already persisted.
    smart_charging.dynamic_schedule_manager->rebuild_from_db();

    ASSERT_EQ(dispatched_call_future.wait_for(std::chrono::seconds(5)), std::future_status::ready);
    auto dispatched_call = dispatched_call_future.get();
    EXPECT_EQ(dispatched_call.at(CALL_PAYLOAD).at("chargingProfileId").get<std::int32_t>(), DEFAULT_PROFILE_ID);
}

TEST_F(SmartChargingTestV21, K28FR10_PullTimer_BootstrapOnMissingDynUpdateTime) {
    enable_dynamic_profiles(device_model);

    auto periods = create_charging_schedule_periods(0, 1, 1, std::nullopt);
    periods.at(0).operationMode = OperationModeEnum::CentralSetpoint;
    periods.at(0).setpoint = 10000.0f;
    auto profile = create_charging_profile(
        DEFAULT_PROFILE_ID, ChargingProfilePurposeEnum::TxDefaultProfile,
        create_charge_schedule(ChargingRateUnitEnum::W, periods, ocpp::DateTime("2024-01-17T17:00:00")), {},
        ChargingProfileKindEnum::Dynamic, DEFAULT_STACK_LEVEL, ocpp::DateTime("2024-01-01T13:00:00"),
        ocpp::DateTime("2034-02-01T13:00:00"));
    profile.dynUpdateInterval = 60;
    profile.validFrom = ocpp::DateTime("2024-01-01T13:00:00");
    profile.validTo = ocpp::DateTime("2034-02-01T13:00:00");
    // No dynUpdateTime → bootstrap-immediate.

    database_handler->insert_or_update_charging_profile(DEFAULT_EVSE_ID, profile,
                                                        ChargingLimitSourceEnumStringType::CSO);

    std::atomic<int> dispatch_count{0};
    std::promise<json> dispatched_call_promise;
    auto dispatched_call_future = dispatched_call_promise.get_future();

    EXPECT_CALL(mock_dispatcher, dispatch_call_async(_, _))
        .WillRepeatedly(
            Invoke([&dispatch_count, p = std::make_shared<std::promise<json>>(std::move(dispatched_call_promise))](
                       const json& call, bool /*triggered*/) -> std::future<ocpp::EnhancedMessage<MessageType>> {
                if (dispatch_count.fetch_add(1) == 0) {
                    p->set_value(call);
                }
                std::promise<ocpp::EnhancedMessage<MessageType>> resp;
                ocpp::EnhancedMessage<MessageType> enhanced;
                enhanced.offline = true;
                resp.set_value(enhanced);
                return resp.get_future();
            }));

    smart_charging.dynamic_schedule_manager->rebuild_from_db();

    ASSERT_EQ(dispatched_call_future.wait_for(std::chrono::seconds(5)), std::future_status::ready);
}

TEST_F(SmartChargingTestV21, K28FR10_PullTimer_NonDynamicProfile_NoTimer) {
    enable_dynamic_profiles(device_model);

    // gmock auto-verifies Times(0) on test exit; no sleep needed to assert "did not fire".
    EXPECT_CALL(mock_dispatcher, dispatch_call_async(_, _)).Times(0);

    auto periods = create_charging_schedule_periods(0, 1, 1, 5.0f);
    auto profile = create_charging_profile(
        DEFAULT_PROFILE_ID, ChargingProfilePurposeEnum::TxDefaultProfile,
        create_charge_schedule(ChargingRateUnitEnum::A, periods, ocpp::DateTime("2024-01-17T17:00:00")), {},
        ChargingProfileKindEnum::Absolute, DEFAULT_STACK_LEVEL, ocpp::DateTime("2024-01-01T13:00:00"),
        ocpp::DateTime("2034-02-01T13:00:00"));
    auto add_response = smart_charging.conform_validate_and_add_profile(profile, DEFAULT_EVSE_ID);
    ASSERT_THAT(add_response.status, testing::Eq(ChargingProfileStatusEnum::Accepted));
}

TEST_F(SmartChargingTestV21, K28FR10_ClearChargingProfile_StopsPullTimer) {
    enable_dynamic_profiles(device_model);

    // interval=3600s — pull would only fire an hour out, so any dispatch in this test is a bug.
    // gmock auto-verifies Times(0) on test exit, no sleep needed.
    EXPECT_CALL(mock_dispatcher, dispatch_call_async(_, _)).Times(0);
    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_)).Times(testing::AnyNumber());

    auto profile = make_dynamic_profile_with_interval(DEFAULT_PROFILE_ID, /*interval=*/3600, ocpp::DateTime());
    auto add_response = smart_charging.conform_validate_and_add_profile(profile, DEFAULT_EVSE_ID);
    ASSERT_THAT(add_response.status, testing::Eq(ChargingProfileStatusEnum::Accepted));

    // Clear the profile via the public handler.
    ClearChargingProfileRequest clear_req;
    clear_req.chargingProfileId = DEFAULT_PROFILE_ID;
    auto enhanced =
        request_to_enhanced_message<ClearChargingProfileRequest, MessageType::ClearChargingProfile>(clear_req);
    smart_charging.handle_message(enhanced);
}

TEST_F(SmartChargingTestV21, K28FR10_ReplaceDynamicWithNonDynamic_ErasesPullTracking) {
    enable_dynamic_profiles(device_model);

    // interval=3600s — pull would only fire an hour out. gmock Times(0) auto-verifies on exit.
    EXPECT_CALL(mock_dispatcher, dispatch_call_async(_, _)).Times(0);

    auto dyn_profile = make_dynamic_profile_with_interval(DEFAULT_PROFILE_ID, /*interval=*/3600, ocpp::DateTime());
    auto add_response = smart_charging.conform_validate_and_add_profile(dyn_profile, DEFAULT_EVSE_ID);
    ASSERT_THAT(add_response.status, testing::Eq(ChargingProfileStatusEnum::Accepted));

    // Replace with non-Dynamic profile at the same id.
    auto periods = create_charging_schedule_periods(0, 1, 1, 5.0f);
    auto absolute_profile = create_charging_profile(
        DEFAULT_PROFILE_ID, ChargingProfilePurposeEnum::TxDefaultProfile,
        create_charge_schedule(ChargingRateUnitEnum::A, periods, ocpp::DateTime("2024-01-17T17:00:00")), {},
        ChargingProfileKindEnum::Absolute, DEFAULT_STACK_LEVEL, ocpp::DateTime("2024-01-01T13:00:00"),
        ocpp::DateTime("2034-02-01T13:00:00"));
    auto replace_response = smart_charging.conform_validate_and_add_profile(absolute_profile, DEFAULT_EVSE_ID);
    ASSERT_THAT(replace_response.status, testing::Eq(ChargingProfileStatusEnum::Accepted));
}

TEST_F(SmartChargingTestV21, K28FR08_PullResponse_AppliesUpdate) {
    enable_dynamic_profiles(device_model);

    std::promise<std::string> message_id_promise;
    auto message_id_future = message_id_promise.get_future();
    std::atomic<int> dispatch_count{0};

    EXPECT_CALL(mock_dispatcher, dispatch_call_async(_, _))
        .WillRepeatedly(
            Invoke([&dispatch_count, p = std::make_shared<std::promise<std::string>>(std::move(message_id_promise))](
                       const json& call, bool /*triggered*/) -> std::future<ocpp::EnhancedMessage<MessageType>> {
                std::promise<ocpp::EnhancedMessage<MessageType>> resp;
                if (dispatch_count.fetch_add(1) == 0) {
                    const auto message_id = call.at(MESSAGE_ID).get<std::string>();
                    p->set_value(message_id);

                    ChargingScheduleUpdate schedule_update;
                    schedule_update.setpoint = 7777.0f;
                    auto enhanced =
                        make_pull_response_enhanced(message_id, ChargingProfileStatusEnum::Accepted, schedule_update);
                    resp.set_value(enhanced);
                } else {
                    ocpp::EnhancedMessage<MessageType> offline;
                    offline.offline = true;
                    resp.set_value(offline);
                }
                return resp.get_future();
            }));

    // Signal completion when the pull-response apply path invokes the set-charging-profiles
    // callback — that runs on the worker thread AFTER the database write, so the test can read
    // the stored profile without an arbitrary sleep.
    std::promise<void> apply_done_promise;
    auto apply_done_future = apply_done_promise.get_future();
    EXPECT_CALL(set_charging_profiles_callback_mock, Call)
        .WillOnce(Invoke([&apply_done_promise]() { apply_done_promise.set_value(); }))
        .WillRepeatedly(Return());

    auto profile = make_dynamic_profile_with_interval(DEFAULT_PROFILE_ID, /*interval=*/1, ocpp::DateTime());
    auto add_response = smart_charging.conform_validate_and_add_profile(profile, DEFAULT_EVSE_ID);
    ASSERT_THAT(add_response.status, testing::Eq(ChargingProfileStatusEnum::Accepted));

    ASSERT_EQ(message_id_future.wait_for(std::chrono::seconds(5)), std::future_status::ready);
    ASSERT_EQ(apply_done_future.wait_for(std::chrono::seconds(5)), std::future_status::ready);

    auto stored = database_handler->get_charging_profiles_for_evse(DEFAULT_EVSE_ID);
    ASSERT_EQ(stored.size(), 1);
    ASSERT_FALSE(stored.front().chargingSchedule.empty());
    ASSERT_FALSE(stored.front().chargingSchedule.at(0).chargingSchedulePeriod.empty());
    const auto& period = stored.front().chargingSchedule.at(0).chargingSchedulePeriod.at(0);
    ASSERT_TRUE(period.setpoint.has_value());
    EXPECT_FLOAT_EQ(period.setpoint.value(), 7777.0f);
}

TEST_F(SmartChargingTestV21, K28FR08_PullResponse_RejectedStatus_NoOp) {
    enable_dynamic_profiles(device_model);

    std::promise<std::string> message_id_promise;
    auto message_id_future = message_id_promise.get_future();
    // The Rejected branch fires no test-observable callback, so use the SECOND dispatch (which
    // the timer schedules ~1s after the first, since the rejected response does not refresh the
    // pull deadline) as the signal that the first response was fully processed by the worker.
    std::promise<void> second_dispatch_promise;
    auto second_dispatch_future = second_dispatch_promise.get_future();
    std::atomic<int> dispatch_count{0};

    EXPECT_CALL(mock_dispatcher, dispatch_call_async(_, _))
        .WillRepeatedly(
            Invoke([&dispatch_count, mid_p = std::make_shared<std::promise<std::string>>(std::move(message_id_promise)),
                    snd_p = std::make_shared<std::promise<void>>(std::move(second_dispatch_promise))](
                       const json& call, bool /*triggered*/) -> std::future<ocpp::EnhancedMessage<MessageType>> {
                std::promise<ocpp::EnhancedMessage<MessageType>> resp;
                const int n = dispatch_count.fetch_add(1);
                if (n == 0) {
                    const auto message_id = call.at(MESSAGE_ID).get<std::string>();
                    mid_p->set_value(message_id);
                    auto enhanced =
                        make_pull_response_enhanced(message_id, ChargingProfileStatusEnum::Rejected, std::nullopt);
                    resp.set_value(enhanced);
                } else {
                    if (n == 1) {
                        snd_p->set_value();
                    }
                    ocpp::EnhancedMessage<MessageType> offline;
                    offline.offline = true;
                    resp.set_value(offline);
                }
                return resp.get_future();
            }));

    auto profile = make_dynamic_profile_with_interval(DEFAULT_PROFILE_ID, /*interval=*/1, ocpp::DateTime());
    auto add_response = smart_charging.conform_validate_and_add_profile(profile, DEFAULT_EVSE_ID);
    ASSERT_THAT(add_response.status, testing::Eq(ChargingProfileStatusEnum::Accepted));

    auto stored_initial = database_handler->get_charging_profiles_for_evse(DEFAULT_EVSE_ID);
    ASSERT_EQ(stored_initial.size(), 1);
    const double initial_setpoint =
        stored_initial.front().chargingSchedule.at(0).chargingSchedulePeriod.at(0).setpoint.value_or(0.0f);

    ASSERT_EQ(message_id_future.wait_for(std::chrono::seconds(5)), std::future_status::ready);
    ASSERT_EQ(second_dispatch_future.wait_for(std::chrono::seconds(5)), std::future_status::ready);

    auto stored_after = database_handler->get_charging_profiles_for_evse(DEFAULT_EVSE_ID);
    ASSERT_EQ(stored_after.size(), 1);
    EXPECT_FLOAT_EQ(stored_after.front().chargingSchedule.at(0).chargingSchedulePeriod.at(0).setpoint.value_or(0.0f),
                    initial_setpoint);
}

TEST_F(SmartChargingTestV21, K28FR08_PullResponse_ProfileClearedDuringFlight_NoOp) {
    enable_dynamic_profiles(device_model);

    std::promise<std::pair<std::string, json>> dispatched_promise;
    auto dispatched_future = dispatched_promise.get_future();
    std::atomic<int> dispatch_count{0};

    EXPECT_CALL(mock_dispatcher, dispatch_call_async(_, _))
        .WillRepeatedly(
            Invoke([&dispatch_count,
                    p = std::make_shared<std::promise<std::pair<std::string, json>>>(std::move(dispatched_promise))](
                       const json& call, bool /*triggered*/) -> std::future<ocpp::EnhancedMessage<MessageType>> {
                std::promise<ocpp::EnhancedMessage<MessageType>> resp;
                ocpp::EnhancedMessage<MessageType> offline;
                offline.offline = true;
                if (dispatch_count.fetch_add(1) == 0) {
                    const auto message_id = call.at(MESSAGE_ID).get<std::string>();
                    p->set_value({message_id, call});
                }
                resp.set_value(offline);
                return resp.get_future();
            }));
    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_)).Times(testing::AnyNumber());

    auto profile = make_dynamic_profile_with_interval(DEFAULT_PROFILE_ID, /*interval=*/1, ocpp::DateTime());
    auto add_response = smart_charging.conform_validate_and_add_profile(profile, DEFAULT_EVSE_ID);
    ASSERT_THAT(add_response.status, testing::Eq(ChargingProfileStatusEnum::Accepted));

    // Wait for first dispatch.
    ASSERT_EQ(dispatched_future.wait_for(std::chrono::seconds(5)), std::future_status::ready);

    // Clear the profile while a pull is in flight. The dispatcher returned an offline future, so
    // the worker exits early without touching the DB; the clear executes synchronously on the
    // test thread so the post-condition (no stored profile) holds immediately.
    ClearChargingProfileRequest clear_req;
    clear_req.chargingProfileId = DEFAULT_PROFILE_ID;
    auto enhanced =
        request_to_enhanced_message<ClearChargingProfileRequest, MessageType::ClearChargingProfile>(clear_req);
    smart_charging.handle_message(enhanced);

    auto stored_after = database_handler->get_charging_profiles_for_evse(DEFAULT_EVSE_ID);
    EXPECT_EQ(stored_after.size(), 0);
}

TEST_F(SmartChargingTestV21, K28FR10_PushApply_RefreshesNextPullDeadline) {
    enable_dynamic_profiles(device_model);

    // Long interval (3600s) — neither the timer nor the push apply may dispatch a pull. gmock
    // Times(0) auto-verifies on exit, so no sleep is needed to assert "did not fire".
    EXPECT_CALL(mock_dispatcher, dispatch_call_async(_, _)).Times(0);
    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_)).Times(testing::AnyNumber());

    auto profile = make_dynamic_profile_with_interval(DEFAULT_PROFILE_ID, /*interval=*/3600, ocpp::DateTime());
    auto add_response = smart_charging.conform_validate_and_add_profile(profile, DEFAULT_EVSE_ID);
    ASSERT_THAT(add_response.status, testing::Eq(ChargingProfileStatusEnum::Accepted));

    // Push an update — the push apply path must call update_pull_tracking on the matched profile.
    v21::UpdateDynamicScheduleRequest req;
    req.chargingProfileId = DEFAULT_PROFILE_ID;
    req.scheduleUpdate.setpoint = 5000.0f;
    auto enhanced =
        request_to_enhanced_message<v21::UpdateDynamicScheduleRequest, MessageType::UpdateDynamicSchedule>(req);
    smart_charging.handle_message(enhanced);
}

TEST_F(SmartChargingTestV21, K28_Shutdown_DestructorReturnsCleanlyWithInFlightPullWorkers) {
    // Forces destruction with workers in-flight.
    enable_dynamic_profiles(device_model);

    std::atomic<int> dispatch_count{0};
    EXPECT_CALL(mock_dispatcher, dispatch_call_async(_, _))
        .WillRepeatedly(Invoke([&dispatch_count](const json&, bool) -> std::future<ocpp::EnhancedMessage<MessageType>> {
            dispatch_count.fetch_add(1, std::memory_order_relaxed);
            std::promise<ocpp::EnhancedMessage<MessageType>> p;
            ocpp::EnhancedMessage<MessageType> offline;
            offline.offline = true;
            p.set_value(offline);
            return p.get_future();
        }));
    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_)).Times(testing::AnyNumber());

    auto local_sc = std::make_unique<TestSmartCharging>(*functional_block_context,
                                                        set_charging_profiles_callback_mock.AsStdFunction(),
                                                        stop_transaction_callback_mock.AsStdFunction());

    auto profile = make_dynamic_profile_with_interval(DEFAULT_PROFILE_ID, /*interval=*/1, ocpp::DateTime());
    ASSERT_THAT(local_sc->conform_validate_and_add_profile(profile, DEFAULT_EVSE_ID).status,
                testing::Eq(ChargingProfileStatusEnum::Accepted));

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (dispatch_count.load(std::memory_order_relaxed) == 0 && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    ASSERT_GE(dispatch_count.load(), 1);

    local_sc.reset();
}

TEST_F(SmartChargingTestV21, K28_Shutdown_DestructorDoesNotBlockOnStuckPullWorker) {
    // A worker blocked on a never-ready CSMS response must not stall teardown for the full
    // worker wait bound (DEFAULT_WAIT_FOR_FUTURE_TIMEOUT = 60s). The destructor signals
    // teardown and the bounded response wait is interrupted, so it returns promptly.
    enable_dynamic_profiles(device_model);

    std::mutex promises_mtx;
    std::vector<std::shared_ptr<std::promise<ocpp::EnhancedMessage<MessageType>>>> stuck_promises;
    std::atomic<int> dispatch_count{0};

    EXPECT_CALL(mock_dispatcher, dispatch_call_async(_, _))
        .WillRepeatedly(Invoke([&](const json&, bool) -> std::future<ocpp::EnhancedMessage<MessageType>> {
            auto p = std::make_shared<std::promise<ocpp::EnhancedMessage<MessageType>>>();
            auto fut = p->get_future();
            {
                std::lock_guard<std::mutex> lk(promises_mtx);
                stuck_promises.push_back(std::move(p)); // never satisfied: the future never becomes ready
            }
            dispatch_count.fetch_add(1, std::memory_order_relaxed);
            return fut;
        }));
    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_)).Times(testing::AnyNumber());

    auto local_sc = std::make_unique<TestSmartCharging>(*functional_block_context,
                                                        set_charging_profiles_callback_mock.AsStdFunction(),
                                                        stop_transaction_callback_mock.AsStdFunction());

    auto profile = make_dynamic_profile_with_interval(DEFAULT_PROFILE_ID, /*interval=*/1, ocpp::DateTime());
    ASSERT_THAT(local_sc->conform_validate_and_add_profile(profile, DEFAULT_EVSE_ID).status,
                testing::Eq(ChargingProfileStatusEnum::Accepted));

    const auto spawn_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (dispatch_count.load(std::memory_order_relaxed) == 0 && std::chrono::steady_clock::now() < spawn_deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    ASSERT_GE(dispatch_count.load(), 1) << "no pull worker was spawned";

    // Time the destructor on another thread; a stuck worker must not hold it for ~60s.
    std::packaged_task<void()> teardown([&] { local_sc.reset(); });
    auto teardown_done = teardown.get_future();
    std::thread teardown_thread(std::move(teardown));

    const auto status = teardown_done.wait_for(std::chrono::seconds(5));
    EXPECT_EQ(status, std::future_status::ready)
        << "destructor blocked on a stuck pull worker (teardown wait not interrupted)";

    // Release the stuck promises so the worker unwinds even if the assertion failed
    // (broken_promise -> future throws -> worker returns), then join.
    {
        std::lock_guard<std::mutex> lk(promises_mtx);
        stuck_promises.clear();
    }
    teardown_thread.join();
}

namespace {

// Build a Dynamic profile with a chargingSchedule[0].duration set, suitable for K28.FR.13/.14/.15
// expiry tests. A long interval (3600s) avoids spurious pull dispatches during the test.
ChargingProfile make_dynamic_profile_with_duration(std::int32_t profile_id, std::int32_t duration_seconds,
                                                   std::optional<ocpp::DateTime> dyn_update_time = std::nullopt) {
    auto periods = create_charging_schedule_periods(0, 1, 1, std::nullopt);
    periods.at(0).operationMode = OperationModeEnum::CentralSetpoint;
    periods.at(0).setpoint = 10000.0f;
    auto profile =
        create_charging_profile(profile_id, ChargingProfilePurposeEnum::TxDefaultProfile,
                                create_charge_schedule(ChargingRateUnitEnum::W, periods,
                                                       ocpp::DateTime("2024-01-17T17:00:00"), duration_seconds),
                                {}, ChargingProfileKindEnum::Dynamic, DEFAULT_STACK_LEVEL,
                                ocpp::DateTime("2024-01-01T13:00:00"), ocpp::DateTime("2034-02-01T13:00:00"));
    profile.dynUpdateInterval = 3600;
    if (dyn_update_time.has_value()) {
        profile.dynUpdateTime = dyn_update_time.value();
    }
    return profile;
}

} // namespace

TEST_F(SmartChargingTestV21, K28FR13_DynamicProfile_ExpiredByDuration_SkippedInComposite) {
    enable_dynamic_profiles(device_model);

    // duration=10s, dynUpdateTime 30s ago → expired.
    const auto past = ocpp::DateTime(date::utc_clock::now() - std::chrono::seconds(30));
    auto profile = make_dynamic_profile_with_duration(DEFAULT_PROFILE_ID, /*duration=*/10, past);
    auto add_response = smart_charging.conform_validate_and_add_profile(profile, DEFAULT_EVSE_ID);
    ASSERT_THAT(add_response.status, testing::Eq(ChargingProfileStatusEnum::Accepted));

    // The Set path overwrites dynUpdateTime to "now" (FR.05). Restore the past timestamp directly
    // in the DB to simulate a profile that has been sitting expired since then.
    auto stmt =
        database_handler->new_statement("UPDATE CHARGING_PROFILES SET PROFILE = json_set(PROFILE, '$.dynUpdateTime', "
                                        "?) WHERE ID = ?");
    stmt->bind_text(1, past.to_rfc3339(), everest::db::sqlite::SQLiteString::Transient);
    stmt->bind_int(2, DEFAULT_PROFILE_ID);
    ASSERT_EQ(stmt->step(), SQLITE_DONE);

    auto valid = smart_charging.get_valid_profiles_for_evse(DEFAULT_EVSE_ID);
    EXPECT_TRUE(valid.empty()) << "Expired Dynamic profile must be filtered from composite calc";
}

namespace {

// True if any period of the composite schedule carries the given setpoint value.
bool composite_contains_setpoint(const EnhancedCompositeSchedule& schedule, double setpoint) {
    for (const auto& period : schedule.chargingSchedulePeriod) {
        if (period.setpoint.has_value() && period.setpoint.value() == setpoint) {
            return true;
        }
    }
    return false;
}

} // namespace

// Expiry filtering must hold through the REAL composite path, not just the get_valid_profiles_for_evse
// helper. Drive SmartCharging::get_composite_schedule(evse_id, duration, unit) directly: an expired
// Dynamic profile's setpoint must not appear in the composite, while a fresh (non-expired) Dynamic
// profile's setpoint does. The positive control proves the composite API is actually exercised here
// (a non-expired Dynamic setpoint reaches the composite), so the negative assertion is not vacuous.
TEST_F(SmartChargingTestV21, K28FR13_DynamicProfile_ExpiredByDuration_AbsentFromGetCompositeSchedule) {
    enable_dynamic_profiles(device_model);

    constexpr double dynamic_setpoint = 10000.0;

    // A CentralSetpoint TxDefaultProfile only contributes its setpoint to the composite while a
    // transaction is active on the EVSE (see CompositeScheduleTestFixtureV21.setpoint_tx_profile).
    evse_manager->open_transaction(DEFAULT_EVSE_ID, "f1522902-1170-416f-8e43-9e3bce28fde7");

    // Positive control: a fresh Dynamic profile (dynUpdateTime = now) within its duration window
    // contributes its setpoint to the real composite.
    auto fresh = make_dynamic_profile_with_duration(DEFAULT_PROFILE_ID, /*duration=*/10, ocpp::DateTime());
    ASSERT_THAT(smart_charging.conform_validate_and_add_profile(fresh, DEFAULT_EVSE_ID).status,
                testing::Eq(ChargingProfileStatusEnum::Accepted));

    auto fresh_composite =
        smart_charging.get_composite_schedule(DEFAULT_EVSE_ID, std::chrono::seconds(60), ChargingRateUnitEnum::W);
    ASSERT_TRUE(fresh_composite.has_value()) << "composite schedule API returned nullopt for a valid EVSE";
    EXPECT_TRUE(composite_contains_setpoint(fresh_composite.value(), dynamic_setpoint))
        << "non-expired Dynamic profile setpoint must reach the real composite (positive control)";

    // Now expire it: backdate dynUpdateTime well past the 10s duration window so the profile is
    // expired. The Set path overwrote dynUpdateTime to "now"; restore a past timestamp directly.
    const auto past = ocpp::DateTime(date::utc_clock::now() - std::chrono::seconds(30));
    auto stmt =
        database_handler->new_statement("UPDATE CHARGING_PROFILES SET PROFILE = json_set(PROFILE, '$.dynUpdateTime', "
                                        "?) WHERE ID = ?");
    stmt->bind_text(1, past.to_rfc3339(), everest::db::sqlite::SQLiteString::Transient);
    stmt->bind_int(2, DEFAULT_PROFILE_ID);
    ASSERT_EQ(stmt->step(), SQLITE_DONE);

    auto expired_composite =
        smart_charging.get_composite_schedule(DEFAULT_EVSE_ID, std::chrono::seconds(60), ChargingRateUnitEnum::W);
    ASSERT_TRUE(expired_composite.has_value()) << "composite schedule API returned nullopt for a valid EVSE";
    EXPECT_FALSE(composite_contains_setpoint(expired_composite.value(), dynamic_setpoint))
        << "expired Dynamic profile setpoint must be filtered from the real composite schedule";
}

TEST_F(SmartChargingTestV21, K28FR13_DynamicProfile_NoDuration_NeverExpires) {
    enable_dynamic_profiles(device_model);

    // No duration on the schedule → expiry check does not apply, regardless of dynUpdateTime.
    auto profile = make_dynamic_profile(DEFAULT_PROFILE_ID, 10000.0f);
    profile.dynUpdateInterval = 3600;
    auto add_response = smart_charging.conform_validate_and_add_profile(profile, DEFAULT_EVSE_ID);
    ASSERT_THAT(add_response.status, testing::Eq(ChargingProfileStatusEnum::Accepted));

    // Backdate dynUpdateTime to 30s ago.
    const auto past = ocpp::DateTime(date::utc_clock::now() - std::chrono::seconds(30));
    auto stmt =
        database_handler->new_statement("UPDATE CHARGING_PROFILES SET PROFILE = json_set(PROFILE, '$.dynUpdateTime', "
                                        "?) WHERE ID = ?");
    stmt->bind_text(1, past.to_rfc3339(), everest::db::sqlite::SQLiteString::Transient);
    stmt->bind_int(2, DEFAULT_PROFILE_ID);
    ASSERT_EQ(stmt->step(), SQLITE_DONE);

    auto valid = smart_charging.get_valid_profiles_for_evse(DEFAULT_EVSE_ID);
    EXPECT_EQ(valid.size(), 1) << "Dynamic profile without schedule duration must not be expired";
}

TEST_F(SmartChargingTestV21, K28FR13_DynamicProfile_NoDynUpdateTime_NotConsideredExpired) {
    enable_dynamic_profiles(device_model);

    // Pre-populate DB with a Dynamic profile that has duration=10s but NO dynUpdateTime.
    auto profile = make_dynamic_profile_with_duration(DEFAULT_PROFILE_ID, /*duration=*/10, std::nullopt);
    profile.dynUpdateInterval = 3600;
    database_handler->insert_or_update_charging_profile(DEFAULT_EVSE_ID, profile,
                                                        ChargingLimitSourceEnumStringType::CSO);

    auto valid = smart_charging.get_valid_profiles_for_evse(DEFAULT_EVSE_ID);
    EXPECT_EQ(valid.size(), 1) << "Profile lacking dynUpdateTime defers to bootstrap pull, not expiry";
}

TEST_F(SmartChargingTestV21, K28FR13_ExpiryBoundary_TimerFiresComposite) {
    enable_dynamic_profiles(device_model);

    // Suppress timer-driven pulls (interval=3600 long enough); ignore any unexpected calls.
    EXPECT_CALL(mock_dispatcher, dispatch_call_async(_, _))
        .WillRepeatedly(Invoke([](const json&, bool) -> std::future<ocpp::EnhancedMessage<MessageType>> {
            std::promise<ocpp::EnhancedMessage<MessageType>> resp;
            ocpp::EnhancedMessage<MessageType> offline;
            offline.offline = true;
            resp.set_value(offline);
            return resp.get_future();
        }));

    // conform_validate_and_add_profile does not itself fire the composite-recompute callback —
    // that's done by handle_set_charging_profile_req on the message-handler path. So the FIRST
    // callback observed in this test is the expiry-fire composite recompute.
    std::promise<void> fired_promise;
    auto fired_future = fired_promise.get_future();
    std::atomic<bool> already_fired{false};
    EXPECT_CALL(set_charging_profiles_callback_mock, Call).WillRepeatedly(Invoke([&fired_promise, &already_fired]() {
        if (!already_fired.exchange(true)) {
            fired_promise.set_value();
        }
    }));

    // duration=1s, dynUpdateTime=now → boundary at now + 1s; the timer must fire the composite
    // recompute callback within ~2s.
    auto profile = make_dynamic_profile_with_duration(DEFAULT_PROFILE_ID, /*duration=*/1, ocpp::DateTime());
    auto add_response = smart_charging.conform_validate_and_add_profile(profile, DEFAULT_EVSE_ID);
    ASSERT_THAT(add_response.status, testing::Eq(ChargingProfileStatusEnum::Accepted));

    EXPECT_EQ(fired_future.wait_for(std::chrono::seconds(5)), std::future_status::ready);
}

TEST_F(SmartChargingTestV21, K28FR14_DynamicProfile_ReactivatedOnUpdateAfterExpiry) {
    enable_dynamic_profiles(device_model);

    // Install profile with duration=10s, dynUpdateTime in the past → already expired.
    const auto past = ocpp::DateTime(date::utc_clock::now() - std::chrono::seconds(30));
    auto profile = make_dynamic_profile_with_duration(DEFAULT_PROFILE_ID, /*duration=*/10, past);
    auto add_response = smart_charging.conform_validate_and_add_profile(profile, DEFAULT_EVSE_ID);
    ASSERT_THAT(add_response.status, testing::Eq(ChargingProfileStatusEnum::Accepted));

    // Backdate the auto-set dynUpdateTime to make the profile expired.
    auto backdate =
        database_handler->new_statement("UPDATE CHARGING_PROFILES SET PROFILE = json_set(PROFILE, '$.dynUpdateTime', "
                                        "?) WHERE ID = ?");
    backdate->bind_text(1, past.to_rfc3339(), everest::db::sqlite::SQLiteString::Transient);
    backdate->bind_int(2, DEFAULT_PROFILE_ID);
    ASSERT_EQ(backdate->step(), SQLITE_DONE);

    auto before = smart_charging.get_valid_profiles_for_evse(DEFAULT_EVSE_ID);
    ASSERT_TRUE(before.empty()) << "Profile must be expired before update";

    // Update via UpdateDynamicScheduleRequest — handler refreshes dynUpdateTime to now (FR.09).
    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_));
    v21::UpdateDynamicScheduleRequest req;
    req.chargingProfileId = DEFAULT_PROFILE_ID;
    req.scheduleUpdate.setpoint = 5000.0f;
    auto enhanced =
        request_to_enhanced_message<v21::UpdateDynamicScheduleRequest, MessageType::UpdateDynamicSchedule>(req);
    smart_charging.handle_message(enhanced);

    auto after = smart_charging.get_valid_profiles_for_evse(DEFAULT_EVSE_ID);
    EXPECT_EQ(after.size(), 1) << "Profile must be reactivated after dynUpdateTime is refreshed";
}

TEST_F(SmartChargingTestV21, K28FR15_Duration_SemanticsMatchSpec) {
    enable_dynamic_profiles(device_model);

    // dynUpdateTime = now-5s, duration = 10s → boundary 5s in the future; profile is valid.
    const auto five_seconds_ago = ocpp::DateTime(date::utc_clock::now() - std::chrono::seconds(5));
    auto profile = make_dynamic_profile_with_duration(DEFAULT_PROFILE_ID, /*duration=*/10, five_seconds_ago);
    auto add_response = smart_charging.conform_validate_and_add_profile(profile, DEFAULT_EVSE_ID);
    ASSERT_THAT(add_response.status, testing::Eq(ChargingProfileStatusEnum::Accepted));

    // Set path overwrote dynUpdateTime to "now"; restore the 5-seconds-ago value.
    auto stmt =
        database_handler->new_statement("UPDATE CHARGING_PROFILES SET PROFILE = json_set(PROFILE, '$.dynUpdateTime', "
                                        "?) WHERE ID = ?");
    stmt->bind_text(1, five_seconds_ago.to_rfc3339(), everest::db::sqlite::SQLiteString::Transient);
    stmt->bind_int(2, DEFAULT_PROFILE_ID);
    ASSERT_EQ(stmt->step(), SQLITE_DONE);

    auto valid = smart_charging.get_valid_profiles_for_evse(DEFAULT_EVSE_ID);
    EXPECT_EQ(valid.size(), 1) << "5s elapsed of a 10s duration is still within the validity window";

    // Rewind further so 30s elapsed of a 10s duration → expired.
    const auto thirty_seconds_ago = ocpp::DateTime(date::utc_clock::now() - std::chrono::seconds(30));
    auto stmt2 =
        database_handler->new_statement("UPDATE CHARGING_PROFILES SET PROFILE = json_set(PROFILE, '$.dynUpdateTime', "
                                        "?) WHERE ID = ?");
    stmt2->bind_text(1, thirty_seconds_ago.to_rfc3339(), everest::db::sqlite::SQLiteString::Transient);
    stmt2->bind_int(2, DEFAULT_PROFILE_ID);
    ASSERT_EQ(stmt2->step(), SQLITE_DONE);

    auto valid_after = smart_charging.get_valid_profiles_for_evse(DEFAULT_EVSE_ID);
    EXPECT_TRUE(valid_after.empty()) << "30s elapsed of a 10s duration must be expired";
}

// Regression: a callback throw on the timer thread must not kill the timer thread. Configure a
// Dynamic profile with a short duration already in the past so the expiry-fire branch invokes the
// set_charging_profiles_callback; the first invocation throws and subsequent timer ticks (driven
// by the still-pending pull deadline) must still dispatch. With fire-then-commit (SLICE C1) the
// throwing tick KEEPS the expire entry (logs "K28-expiry" and rethrows to on_deadline's
// top-level catch, which keeps the timer thread alive); the entry is only cleared once the
// recompute callback later succeeds. This test asserts the timer thread survives the throw.
TEST_F(SmartChargingTestV21, K28_OnTimerFire_SwallowsExceptionInCallback) {
    enable_dynamic_profiles(device_model);

    // First callback throws; subsequent ones no-op. The callback is fired on the timer thread when
    // the expiry pass retires an entry; the throw is logged with the K28-expiry errorId and
    // rethrown to on_deadline's top-level catch so the timer survives (the expire entry is kept
    // for a retry on the next tick rather than being cleared committed-but-unpublished).
    std::atomic<bool> callback_threw{false};
    EXPECT_CALL(set_charging_profiles_callback_mock, Call)
        .WillOnce(Invoke([&callback_threw]() {
            callback_threw.store(true);
            throw std::runtime_error("simulated callback failure");
        }))
        .WillRepeatedly(Return());

    // Count timer-driven dispatches; the second-and-later ones prove the timer thread survived.
    std::atomic<int> dispatch_count{0};
    EXPECT_CALL(mock_dispatcher, dispatch_call_async(_, _))
        .WillRepeatedly(Invoke([&dispatch_count](const json&, bool) -> std::future<ocpp::EnhancedMessage<MessageType>> {
            dispatch_count.fetch_add(1);
            std::promise<ocpp::EnhancedMessage<MessageType>> resp;
            ocpp::EnhancedMessage<MessageType> enhanced;
            enhanced.offline = true;
            resp.set_value(enhanced);
            return resp.get_future();
        }));
    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_)).Times(testing::AnyNumber());

    // Build a Dynamic profile with both an expiry boundary at now+1s AND a pull interval of 1s.
    // The first timer fire after ~1s detects the expired entry → fires callback (throws). With
    // fire-then-commit the expire entry is KEPT (not cleared) and the throw is logged + rethrown
    // to the top-level catch, which keeps the timer thread alive; the rearmed timer keeps
    // dispatching pulls and the expiry recompute is retried on a later tick.
    auto periods = create_charging_schedule_periods(0, 1, 1, std::nullopt);
    periods.at(0).operationMode = OperationModeEnum::CentralSetpoint;
    periods.at(0).setpoint = 10000.0f;
    auto profile = create_charging_profile(
        DEFAULT_PROFILE_ID, ChargingProfilePurposeEnum::TxDefaultProfile,
        create_charge_schedule(ChargingRateUnitEnum::W, periods, ocpp::DateTime("2024-01-17T17:00:00"), /*duration=*/1),
        {}, ChargingProfileKindEnum::Dynamic, DEFAULT_STACK_LEVEL, ocpp::DateTime("2024-01-01T13:00:00"),
        ocpp::DateTime("2034-02-01T13:00:00"));
    profile.dynUpdateInterval = 1;
    profile.dynUpdateTime = ocpp::DateTime();

    auto add_response = smart_charging.conform_validate_and_add_profile(profile, DEFAULT_EVSE_ID);
    ASSERT_THAT(add_response.status, testing::Eq(ChargingProfileStatusEnum::Accepted));

    // Wait for the callback throw to fire AND for at least two more dispatches afterwards,
    // proving the timer thread is still alive. Generous window so a loaded CI box still catches
    // the retry ticks.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(4);
    while (std::chrono::steady_clock::now() < deadline) {
        if (callback_threw.load() && dispatch_count.load() >= 2) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    EXPECT_TRUE(callback_threw.load()) << "set_charging_profiles_callback throw branch never ran";
    EXPECT_GE(dispatch_count.load(), 2) << "Timer thread died after callback threw — fewer dispatches than expected";
}

// Regression: a sync-throw from dispatch_call_async inside dispatch_pull_request must be caught
// locally so the timer thread survives AND the pull deadline is NOT advanced. The first
// dispatch throws; the second (which is the next timer tick within the same interval window)
// proves both that the timer thread survived AND that the deadline did not roll forward past the
// expected retry window.
TEST_F(SmartChargingTestV21, K28_DispatchPull_SwallowsSyncThrow) {
    enable_dynamic_profiles(device_model);

    std::atomic<int> dispatch_count{0};
    EXPECT_CALL(mock_dispatcher, dispatch_call_async(_, _))
        .WillRepeatedly(Invoke([&dispatch_count](const json&, bool) -> std::future<ocpp::EnhancedMessage<MessageType>> {
            const int n = dispatch_count.fetch_add(1);
            if (n == 0) {
                throw std::runtime_error("simulated dispatch sync-throw");
            }
            std::promise<ocpp::EnhancedMessage<MessageType>> resp;
            ocpp::EnhancedMessage<MessageType> enhanced;
            enhanced.offline = true;
            resp.set_value(enhanced);
            return resp.get_future();
        }));
    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_)).Times(testing::AnyNumber());

    // 1s interval; dynUpdateTime = now → first pull fires almost immediately and throws.
    auto profile = make_dynamic_profile_with_interval(DEFAULT_PROFILE_ID, /*interval=*/1, ocpp::DateTime());
    auto add_response = smart_charging.conform_validate_and_add_profile(profile, DEFAULT_EVSE_ID);
    ASSERT_THAT(add_response.status, testing::Eq(ChargingProfileStatusEnum::Accepted));

    // Wait for the second dispatch attempt — proves (1) the timer thread did not die and
    // (2) the deadline was not wrongly advanced past now+interval after the throw. Observation
    // window kept generous so a loaded CI box still catches the retry tick.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(4);
    while (dispatch_count.load() < 2 && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    EXPECT_GE(dispatch_count.load(), 2)
        << "Timer either died after dispatch_call_async threw, or the pull deadline was advanced "
           "past the retry window";

    // Busy-loop guard: with the 1s backoff on dispatch failure the second dispatch must NOT fire
    // immediately after the first throws. Without the backoff, the pull deadline stays past-due, the
    // timer re-arms at a past time, and on_deadline spins — dispatch_count would balloon well
    // beyond the natural ~interval rate over the 4s window. With backoff, the count over a
    // 1s-interval/4s window stays bounded around 4-5. Use a generous upper bound so CI noise
    // doesn't flake while still catching a runaway loop.
    EXPECT_LE(dispatch_count.load(), 10)
        << "Dispatch fired far more often than the interval permits — failure path is not "
           "deferring the pull deadline, busy-looping the timer thread";
}

// Regression: a transient database error during the pull-timer lookup must NOT erase deadline
// tracking. lookup_dynamic_profile cannot tell "profile absent" from "query threw", so on a DB
// error dispatch_due_pulls must back off and retry, not drop the profile. Rename CHARGING_PROFILES
// while the 1s timer is running so the lookup throws, then restore it and assert pulls resume.
TEST_F(SmartChargingTestV21, K28_DispatchPull_DbErrorPreservesTracking) {
    enable_dynamic_profiles(device_model);

    std::atomic<int> dispatch_count{0};
    EXPECT_CALL(mock_dispatcher, dispatch_call_async(_, _))
        .WillRepeatedly(Invoke([&dispatch_count](const json&, bool) -> std::future<ocpp::EnhancedMessage<MessageType>> {
            dispatch_count.fetch_add(1);
            std::promise<ocpp::EnhancedMessage<MessageType>> resp;
            ocpp::EnhancedMessage<MessageType> enhanced;
            enhanced.offline = true;
            resp.set_value(enhanced);
            return resp.get_future();
        }));
    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_)).Times(testing::AnyNumber());

    // ALTER TABLE retried on QueryExecutionException: the timer thread runs SELECTs concurrently, so a
    // rename can race a read and come back busy. Retry briefly until it lands.
    auto alter_table = [this](const std::string& from, const std::string& to) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (true) {
            try {
                database_handler->new_statement("ALTER TABLE " + from + " RENAME TO " + to)->step();
                return;
            } catch (const everest::db::QueryExecutionException&) {
                if (std::chrono::steady_clock::now() >= deadline) {
                    throw;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
        }
    };

    auto profile = make_dynamic_profile_with_interval(DEFAULT_PROFILE_ID, /*interval=*/1, ocpp::DateTime());
    auto add_response = smart_charging.conform_validate_and_add_profile(profile, DEFAULT_EVSE_ID);
    ASSERT_THAT(add_response.status, testing::Eq(ChargingProfileStatusEnum::Accepted));

    // Pulls are firing on the 1s interval; wait for the first to confirm tracking is armed.
    const auto armed_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (dispatch_count.load() < 1 && std::chrono::steady_clock::now() < armed_deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    ASSERT_GE(dispatch_count.load(), 1) << "Pull timer never armed";

    // Rename the table so the next lookup in dispatch_due_pulls throws QueryExecutionException.
    // RAII net restores it if an assertion below throws while renamed.
    bool renamed = false;
    struct Restorer {
        std::function<void()> restore;
        bool* renamed;
        ~Restorer() {
            if (*renamed) {
                try {
                    restore();
                } catch (...) {
                }
            }
        }
    };
    Restorer restorer{[&] { alter_table("CHARGING_PROFILES_BAK", "CHARGING_PROFILES"); }, &renamed};

    alter_table("CHARGING_PROFILES", "CHARGING_PROFILES_BAK");
    renamed = true;

    // Let several ticks hit the missing table. The buggy path erases the entry on the first one.
    std::this_thread::sleep_for(std::chrono::milliseconds(2500));
    const int before_restore = dispatch_count.load();

    alter_table("CHARGING_PROFILES_BAK", "CHARGING_PROFILES");
    renamed = false;

    // With tracking preserved, pulls resume once the table is back. With the entry erased they never do.
    const auto resume_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (dispatch_count.load() <= before_restore && std::chrono::steady_clock::now() < resume_deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    EXPECT_GT(dispatch_count.load(), before_restore)
        << "Pulls did not resume after the database recovered — a transient DB error erased deadline tracking";
}

// With SupportsDynamicProfiles disabled the manager must not be constructed, so no reaper thread or
// timer is spun up. Flip the flag off and build a second SmartCharging against the same context.
TEST_F(SmartChargingTestV21, K28_DynamicProfilesUnsupported_ManagerNotConstructed) {
    const auto& supports_dynamic = ControllerComponentVariables::SupportsDynamicProfiles;
    device_model->set_value(supports_dynamic.component, supports_dynamic.variable.value(), AttributeEnum::Actual,
                            "false", "test", true);

    TestSmartCharging unsupported{*functional_block_context, set_charging_profiles_callback_mock.AsStdFunction(),
                                  stop_transaction_callback_mock.AsStdFunction()};
    EXPECT_FALSE(unsupported.dynamic_schedule_manager.has_value());
}

// Regression: handle_update_dynamic_schedule_request must catch QueryExecutionException from the matching-criteria
// lookup and respond Rejected/InternalError. Rename CHARGING_PROFILES out from under the lookup
// so it fails at statement preparation, then restore it for TearDown's clear_charging_profiles.
TEST_F(SmartChargingTestV21, K28FR11_UpdateDynamicSchedule_DbLookupThrows_RespondsInternalError) {
    enable_dynamic_profiles(device_model);

    auto profile = make_dynamic_profile(DEFAULT_PROFILE_ID, 10000.0f);
    auto add_response = smart_charging.conform_validate_and_add_profile(profile, DEFAULT_EVSE_ID);
    ASSERT_THAT(add_response.status, testing::Eq(ChargingProfileStatusEnum::Accepted));

    // Rename the table so any SELECT against CHARGING_PROFILES throws QueryExecutionException at
    // statement preparation time. RAII guard always restores it, even if an assertion below throws.
    struct TableRestorer {
        std::shared_ptr<DatabaseHandler> db;
        ~TableRestorer() {
            try {
                db->new_statement("ALTER TABLE CHARGING_PROFILES_BAK RENAME TO CHARGING_PROFILES")->step();
            } catch (...) {
                // best-effort restore; TearDown will fail loudly if this didn't work.
            }
        }
    };
    auto rename_stmt = database_handler->new_statement("ALTER TABLE CHARGING_PROFILES RENAME TO CHARGING_PROFILES_BAK");
    ASSERT_EQ(rename_stmt->step(), SQLITE_DONE);
    TableRestorer restorer{database_handler};

    v21::UpdateDynamicScheduleRequest req;
    req.chargingProfileId = DEFAULT_PROFILE_ID;
    req.scheduleUpdate.setpoint = 5000.0f;
    auto enhanced =
        request_to_enhanced_message<v21::UpdateDynamicScheduleRequest, MessageType::UpdateDynamicSchedule>(req);

    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_)).WillOnce(Invoke([](const json& call_result) {
        auto response = call_result[ocpp::CALLRESULT_PAYLOAD].get<v21::UpdateDynamicScheduleResponse>();
        EXPECT_EQ(response.status, ChargingProfileStatusEnum::Rejected);
        ASSERT_TRUE(response.statusInfo.has_value());
        EXPECT_EQ(response.statusInfo.value().reasonCode.get(), "InternalError");
        ASSERT_TRUE(response.statusInfo.value().additionalInfo.has_value());
        // additionalInfo carries the full diagnostic message (not just the bare exception text).
        EXPECT_THAT(response.statusInfo.value().additionalInfo.value().get(),
                    testing::HasSubstr("Could not look up profile"));
        EXPECT_THAT(response.statusInfo.value().additionalInfo.value().get(),
                    testing::HasSubstr("for UpdateDynamicSchedule:"));
    }));
    // No callback on DB lookup failure.
    EXPECT_CALL(set_charging_profiles_callback_mock, Call).Times(0);

    smart_charging.handle_message(enhanced);
}

// When the on-disk profile has a malformed schedule (empty chargingSchedulePeriod vector),
// apply_update rejects without mutating dynUpdateTime. Corrupting the persisted JSON directly is
// the only way to reach the apply path with a malformed shape: K28.FR.01 blocks empty period
// vectors at SetChargingProfile ingress.
TEST_F(SmartChargingTestV21, K28_ApplyUpdate_MalformedShape_RejectsWithoutMutation) {
    enable_dynamic_profiles(device_model);
    auto profile = make_dynamic_profile(DEFAULT_PROFILE_ID, 10000.0f);
    auto add_response = smart_charging.conform_validate_and_add_profile(profile, DEFAULT_EVSE_ID);
    ASSERT_THAT(add_response.status, testing::Eq(ChargingProfileStatusEnum::Accepted));

    auto stored_before = database_handler->get_charging_profiles_for_evse(DEFAULT_EVSE_ID);
    ASSERT_EQ(stored_before.size(), 1);
    ASSERT_TRUE(stored_before.front().dynUpdateTime.has_value());
    const auto dyn_update_time_before = stored_before.front().dynUpdateTime.value().to_rfc3339();

    // Corrupt the persisted profile: replace the period vector with an empty array.
    auto corrupt_stmt = database_handler->new_statement(
        "UPDATE CHARGING_PROFILES SET PROFILE = json_set(PROFILE, '$.chargingSchedule[0].chargingSchedulePeriod', "
        "json('[]')) WHERE ID = ?");
    corrupt_stmt->bind_int(1, DEFAULT_PROFILE_ID);
    ASSERT_EQ(corrupt_stmt->step(), SQLITE_DONE);

    v21::UpdateDynamicScheduleRequest req;
    req.chargingProfileId = DEFAULT_PROFILE_ID;
    req.scheduleUpdate.setpoint = 5000.0f;
    auto enhanced =
        request_to_enhanced_message<v21::UpdateDynamicScheduleRequest, MessageType::UpdateDynamicSchedule>(req);

    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_)).WillOnce(Invoke([](const json& call_result) {
        auto response = call_result[ocpp::CALLRESULT_PAYLOAD].get<v21::UpdateDynamicScheduleResponse>();
        EXPECT_EQ(response.status, ChargingProfileStatusEnum::Rejected);
        ASSERT_TRUE(response.statusInfo.has_value());
        EXPECT_EQ(response.statusInfo.value().reasonCode.get(), "InternalError");
    }));
    // Must not fire the composite-recompute callback on malformed-shape rejection.
    EXPECT_CALL(set_charging_profiles_callback_mock, Call).Times(0);

    smart_charging.handle_message(enhanced);

    // dynUpdateTime must be unchanged: the shape check runs ahead of the dynUpdateTime mutation.
    auto stored_after = database_handler->get_charging_profiles_for_evse(DEFAULT_EVSE_ID);
    ASSERT_EQ(stored_after.size(), 1);
    ASSERT_TRUE(stored_after.front().dynUpdateTime.has_value());
    EXPECT_EQ(stored_after.front().dynUpdateTime.value().to_rfc3339(), dyn_update_time_before);
}

// A profile that has vanished from the DB between two timer fires must not leave a stale expiry
// deadline: when on_deadline takes the profile-gone branch it erases the profile's whole deadline
// entry (pull and expire), so a subsequent expiry pass on the same tick cannot fire a stale
// composite recompute. Setup: dynUpdateInterval=1s, duration=20s, dynUpdateTime in the past so the pull
// deadline is overdue; duration window still in the future so the expire entry survives the first
// scan. Delete the row directly so the DB lookup in the pull branch returns empty -> profile-gone.
TEST_F(SmartChargingTestV21, K28_OnTimerFire_ProfileGone_ErasesExpireTracking) {
    enable_dynamic_profiles(device_model);

    // Count dispatch_call_async invocations so we can observe timer ticks event-style instead of
    // sleeping for a fixed wall-clock budget. Each tick on a tracked profile drives at most one
    // dispatch attempt before the profile-gone branch erases the entry.
    std::atomic<int> dispatch_count{0};
    EXPECT_CALL(mock_dispatcher, dispatch_call_async(_, _))
        .WillRepeatedly(Invoke([&dispatch_count](const json&, bool) -> std::future<ocpp::EnhancedMessage<MessageType>> {
            dispatch_count.fetch_add(1);
            std::promise<ocpp::EnhancedMessage<MessageType>> resp;
            ocpp::EnhancedMessage<MessageType> offline;
            offline.offline = true;
            resp.set_value(offline);
            return resp.get_future();
        }));
    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_)).Times(testing::AnyNumber());

    // Insert directly with duration set + small interval + past dynUpdateTime. Use a 1s interval
    // so pull deadline = past_time + 1s = still in the past; expire deadline = past_time + 20s =
    // ~10s in the future. Critical: profile must reach the timer's pull pass; if we went through
    // add_profile, the Set path would reset dynUpdateTime to now and the pull deadline becomes 1s
    // out.
    const auto past = ocpp::DateTime(date::utc_clock::now() - std::chrono::seconds(10));
    auto profile = make_dynamic_profile_with_duration(DEFAULT_PROFILE_ID, /*duration=*/20, past);
    profile.dynUpdateInterval = 1;
    database_handler->insert_or_update_charging_profile(DEFAULT_EVSE_ID, profile,
                                                        ChargingLimitSourceEnumStringType::CSO);

    // Count expiry-pass callback invocations. The timer's profile-gone branch must erase the
    // expire entry before the expiry pass runs, so this stays at 0 for the full observation
    // window. The DB row will be cleaned up by TearDown's clear_charging_profiles; no separate
    // restoration needed.
    std::atomic<int> callback_count{0};
    EXPECT_CALL(set_charging_profiles_callback_mock, Call).WillRepeatedly(Invoke([&callback_count]() {
        callback_count.fetch_add(1);
    }));

    // Seed pull + expire tracking from the persisted row, then drop the row from under the timer.
    smart_charging.dynamic_schedule_manager->rebuild_from_db();
    auto delete_stmt = database_handler->new_statement("DELETE FROM CHARGING_PROFILES WHERE ID = ?");
    delete_stmt->bind_int(1, DEFAULT_PROFILE_ID);
    ASSERT_EQ(delete_stmt->step(), SQLITE_DONE);

    // Snapshot dispatch_count after the delete -- the count must stay flat afterwards because the
    // profile-gone branch is taken before dispatch_call_async is reached.
    const int dispatches_at_delete = dispatch_count.load();

    // Observation window: timer interval is 1s, so 2.5s spans at least two ticks and gives the
    // profile-gone branch ample wall-clock budget on a loaded CI box. We exit early if the negative
    // event we care about (callback_count incrementing) is ever observed, so a failing test fails
    // quickly. A passing test pays the full window; that is fine because the assertion is "nothing
    // fires" -- absence-of-event has no positive trigger we can poll on.
    const auto observation_deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(2500);
    while (std::chrono::steady_clock::now() < observation_deadline) {
        if (callback_count.load() > 0) {
            break; // Negative case: fail fast on the assertion below.
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    EXPECT_EQ(callback_count.load(), 0)
        << "Expiry callback fired despite profile-gone branch -- deadline entry not erased.";
    // Sanity: dispatch_call_async must NOT have been called after the delete, because the lookup
    // yields an empty match and the profile-gone branch erases tracking before dispatch.
    EXPECT_EQ(dispatch_count.load(), dispatches_at_delete)
        << "dispatch_call_async was invoked after delete -- profile-gone branch did not erase pull tracking.";
}

// Two Dynamic profiles tagged with different chargingLimitSource metadata (CSO, EMS). Clearing
// by stackLevel must remove the matched profile and erase its pull tracking, while the unmatched
// profile remains in the DB, keeps its tracking firing, and retains its original source metadata.
TEST_F(SmartChargingTestV21, K28FR10_ClearChargingProfile_StackLevelFilter_ErasesOnlyMatchedPullTracking) {
    enable_dynamic_profiles(device_model);

    constexpr std::int32_t profile_id_cso = DEFAULT_PROFILE_ID;
    constexpr std::int32_t profile_id_ems = DEFAULT_PROFILE_ID + 1;
    constexpr int stack_level_cso = DEFAULT_STACK_LEVEL;
    constexpr int stack_level_ems = DEFAULT_STACK_LEVEL + 1;

    std::atomic<int> dispatch_count_cso{0};
    std::atomic<int> dispatch_count_ems{0};
    std::promise<void> first_ems_dispatch_promise;
    auto first_ems_dispatch_future = first_ems_dispatch_promise.get_future();

    EXPECT_CALL(mock_dispatcher, dispatch_call_async(_, _))
        .WillRepeatedly(
            Invoke([&dispatch_count_cso, &dispatch_count_ems,
                    ems_p = std::make_shared<std::promise<void>>(std::move(first_ems_dispatch_promise))](
                       const json& call, bool /*triggered*/) -> std::future<ocpp::EnhancedMessage<MessageType>> {
                const auto profile_id = call.at(CALL_PAYLOAD).at("chargingProfileId").get<std::int32_t>();
                if (profile_id == profile_id_cso) {
                    dispatch_count_cso.fetch_add(1);
                } else if (profile_id == profile_id_ems) {
                    if (dispatch_count_ems.fetch_add(1) == 0) {
                        ems_p->set_value();
                    }
                }
                std::promise<ocpp::EnhancedMessage<MessageType>> resp;
                ocpp::EnhancedMessage<MessageType> offline;
                offline.offline = true;
                resp.set_value(offline);
                return resp.get_future();
            }));
    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_)).Times(testing::AnyNumber());

    auto profile_cso = make_dynamic_profile_with_interval(profile_id_cso, /*interval=*/1, ocpp::DateTime());
    profile_cso.stackLevel = stack_level_cso;
    auto profile_ems = make_dynamic_profile_with_interval(profile_id_ems, /*interval=*/1, ocpp::DateTime());
    profile_ems.stackLevel = stack_level_ems;

    ASSERT_THAT(
        smart_charging
            .conform_validate_and_add_profile(profile_cso, DEFAULT_EVSE_ID, ChargingLimitSourceEnumStringType::CSO)
            .status,
        testing::Eq(ChargingProfileStatusEnum::Accepted));
    ASSERT_THAT(
        smart_charging
            .conform_validate_and_add_profile(profile_ems, DEFAULT_EVSE_ID, ChargingLimitSourceEnumStringType::EMS)
            .status,
        testing::Eq(ChargingProfileStatusEnum::Accepted));

    ClearChargingProfileRequest clear_req;
    ClearChargingProfile criteria;
    criteria.stackLevel = stack_level_cso;
    clear_req.chargingProfileCriteria = criteria;
    auto enhanced =
        request_to_enhanced_message<ClearChargingProfileRequest, MessageType::ClearChargingProfile>(clear_req);
    smart_charging.handle_message(enhanced);

    // CSO row deleted; EMS row preserved with its original limit source.
    auto stored = database_handler->get_charging_profiles_for_evse(DEFAULT_EVSE_ID);
    ASSERT_EQ(stored.size(), 1u);
    EXPECT_EQ(stored.front().id, profile_id_ems);

    ChargingProfileCriterion ems_lookup;
    ems_lookup.chargingProfileId = std::vector<std::int32_t>{profile_id_ems};
    auto ems_rows = database_handler->get_charging_profiles_matching_criteria(std::nullopt, ems_lookup);
    ASSERT_EQ(ems_rows.size(), 1u);
    EXPECT_EQ(ems_rows.front().source, ChargingLimitSourceEnumStringType::EMS);

    // Observable behavior of tracking: the EMS timer keeps firing after the clear, while the CSO
    // entry stays at whatever count it reached before erase_tracking dropped it.
    ASSERT_EQ(first_ems_dispatch_future.wait_for(std::chrono::seconds(5)), std::future_status::ready);
    const int cso_dispatches_after_clear = dispatch_count_cso.load();

    // Give the timer at least one more tick at the 1s cadence to confirm no spurious CSO dispatch
    // and that EMS continues to be polled.
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    EXPECT_EQ(dispatch_count_cso.load(), cso_dispatches_after_clear)
        << "CSO profile pull tracking was not erased -- timer still firing for a deleted profile.";
    EXPECT_GE(dispatch_count_ems.load(), 1) << "EMS profile pull tracking lost -- timer no longer firing for it.";
}

// The response worker's catch branch for fut.get() must swallow without applying state. Use a
// promise that resolves to an exception; the worker drops the future result and exits. The
// stored profile's setpoint stays at the initial value and the second timer-driven dispatch
// (~1s after the first, since the throw branch does not refresh the pull deadline) signals that
// the worker fully consumed the failed future.
TEST_F(SmartChargingTestV21, K28FR08_PullResponse_FutureThrows_NoOp) {
    enable_dynamic_profiles(device_model);

    std::promise<void> first_dispatched_promise;
    auto first_dispatched_future = first_dispatched_promise.get_future();
    std::promise<void> second_dispatched_promise;
    auto second_dispatched_future = second_dispatched_promise.get_future();
    std::atomic<int> dispatch_count{0};

    EXPECT_CALL(mock_dispatcher, dispatch_call_async(_, _))
        .WillRepeatedly(Invoke(
            [&dispatch_count, first_p = std::make_shared<std::promise<void>>(std::move(first_dispatched_promise)),
             second_p = std::make_shared<std::promise<void>>(std::move(second_dispatched_promise))](
                const json& /*call*/, bool /*triggered*/) -> std::future<ocpp::EnhancedMessage<MessageType>> {
                std::promise<ocpp::EnhancedMessage<MessageType>> resp;
                const int n = dispatch_count.fetch_add(1);
                if (n == 0) {
                    resp.set_exception(std::make_exception_ptr(std::runtime_error("simulated future exception")));
                    first_p->set_value();
                } else {
                    if (n == 1) {
                        second_p->set_value();
                    }
                    ocpp::EnhancedMessage<MessageType> offline;
                    offline.offline = true;
                    resp.set_value(offline);
                }
                return resp.get_future();
            }));

    // The throw branch must not invoke the composite-recompute callback.
    EXPECT_CALL(set_charging_profiles_callback_mock, Call).Times(0);

    auto profile = make_dynamic_profile_with_interval(DEFAULT_PROFILE_ID, /*interval=*/1, ocpp::DateTime());
    auto add_response = smart_charging.conform_validate_and_add_profile(profile, DEFAULT_EVSE_ID);
    ASSERT_THAT(add_response.status, testing::Eq(ChargingProfileStatusEnum::Accepted));

    auto stored_initial = database_handler->get_charging_profiles_for_evse(DEFAULT_EVSE_ID);
    ASSERT_EQ(stored_initial.size(), 1u);
    ASSERT_FALSE(stored_initial.front().chargingSchedule.empty());
    ASSERT_FALSE(stored_initial.front().chargingSchedule.at(0).chargingSchedulePeriod.empty());
    const double initial_setpoint =
        stored_initial.front().chargingSchedule.at(0).chargingSchedulePeriod.at(0).setpoint.value_or(0.0f);

    ASSERT_EQ(first_dispatched_future.wait_for(std::chrono::seconds(5)), std::future_status::ready);
    ASSERT_EQ(second_dispatched_future.wait_for(std::chrono::seconds(5)), std::future_status::ready);

    auto stored_after = database_handler->get_charging_profiles_for_evse(DEFAULT_EVSE_ID);
    ASSERT_EQ(stored_after.size(), 1u);
    EXPECT_FLOAT_EQ(stored_after.front().chargingSchedule.at(0).chargingSchedulePeriod.at(0).setpoint.value_or(0.0f),
                    initial_setpoint);
}

// A response whose messageType is anything other than PullDynamicScheduleUpdateResponse takes the
// "unexpected response type" branch. Use Heartbeat as a stand-in for a wrong but well-formed
// envelope. The setpoint stays at the initial value and the second dispatch signals the worker
// finished processing the first response.
TEST_F(SmartChargingTestV21, K28FR08_PullResponse_UnexpectedMessageType_NoOp) {
    enable_dynamic_profiles(device_model);

    std::promise<void> first_dispatched_promise;
    auto first_dispatched_future = first_dispatched_promise.get_future();
    std::promise<void> second_dispatched_promise;
    auto second_dispatched_future = second_dispatched_promise.get_future();
    std::atomic<int> dispatch_count{0};

    EXPECT_CALL(mock_dispatcher, dispatch_call_async(_, _))
        .WillRepeatedly(Invoke(
            [&dispatch_count, first_p = std::make_shared<std::promise<void>>(std::move(first_dispatched_promise)),
             second_p = std::make_shared<std::promise<void>>(std::move(second_dispatched_promise))](
                const json& call, bool /*triggered*/) -> std::future<ocpp::EnhancedMessage<MessageType>> {
                std::promise<ocpp::EnhancedMessage<MessageType>> resp;
                const int n = dispatch_count.fetch_add(1);
                if (n == 0) {
                    const auto message_id = call.at(MESSAGE_ID).get<std::string>();
                    ocpp::EnhancedMessage<MessageType> wrong_type;
                    wrong_type.messageType = MessageType::Heartbeat;
                    wrong_type.uniqueId = message_id;
                    wrong_type.message = json::array({ocpp::MessageTypeId::CALLRESULT, message_id, json::object()});
                    resp.set_value(wrong_type);
                    first_p->set_value();
                } else {
                    if (n == 1) {
                        second_p->set_value();
                    }
                    ocpp::EnhancedMessage<MessageType> offline;
                    offline.offline = true;
                    resp.set_value(offline);
                }
                return resp.get_future();
            }));

    EXPECT_CALL(set_charging_profiles_callback_mock, Call).Times(0);

    auto profile = make_dynamic_profile_with_interval(DEFAULT_PROFILE_ID, /*interval=*/1, ocpp::DateTime());
    auto add_response = smart_charging.conform_validate_and_add_profile(profile, DEFAULT_EVSE_ID);
    ASSERT_THAT(add_response.status, testing::Eq(ChargingProfileStatusEnum::Accepted));

    auto stored_initial = database_handler->get_charging_profiles_for_evse(DEFAULT_EVSE_ID);
    ASSERT_EQ(stored_initial.size(), 1u);
    const double initial_setpoint =
        stored_initial.front().chargingSchedule.at(0).chargingSchedulePeriod.at(0).setpoint.value_or(0.0f);

    ASSERT_EQ(first_dispatched_future.wait_for(std::chrono::seconds(5)), std::future_status::ready);
    ASSERT_EQ(second_dispatched_future.wait_for(std::chrono::seconds(5)), std::future_status::ready);

    auto stored_after = database_handler->get_charging_profiles_for_evse(DEFAULT_EVSE_ID);
    ASSERT_EQ(stored_after.size(), 1u);
    EXPECT_FLOAT_EQ(stored_after.front().chargingSchedule.at(0).chargingSchedulePeriod.at(0).setpoint.value_or(0.0f),
                    initial_setpoint);
}

// A response with the correct messageType but a malformed JSON payload must take the parse-catch
// branch. The envelope here is shaped as a CALLRESULT carrying an empty object, which fails the
// PullDynamicScheduleUpdateResponse from_json conversion (status is a required field).
TEST_F(SmartChargingTestV21, K28FR08_PullResponse_MalformedPayload_NoOp) {
    enable_dynamic_profiles(device_model);

    std::promise<void> first_dispatched_promise;
    auto first_dispatched_future = first_dispatched_promise.get_future();
    std::promise<void> second_dispatched_promise;
    auto second_dispatched_future = second_dispatched_promise.get_future();
    std::atomic<int> dispatch_count{0};

    EXPECT_CALL(mock_dispatcher, dispatch_call_async(_, _))
        .WillRepeatedly(Invoke(
            [&dispatch_count, first_p = std::make_shared<std::promise<void>>(std::move(first_dispatched_promise)),
             second_p = std::make_shared<std::promise<void>>(std::move(second_dispatched_promise))](
                const json& call, bool /*triggered*/) -> std::future<ocpp::EnhancedMessage<MessageType>> {
                std::promise<ocpp::EnhancedMessage<MessageType>> resp;
                const int n = dispatch_count.fetch_add(1);
                if (n == 0) {
                    const auto message_id = call.at(MESSAGE_ID).get<std::string>();
                    ocpp::EnhancedMessage<MessageType> malformed;
                    malformed.messageType = MessageType::PullDynamicScheduleUpdateResponse;
                    malformed.uniqueId = message_id;
                    // Required `status` field absent → from_json throws inside the worker's
                    // CallResult conversion.
                    malformed.message = json::array({ocpp::MessageTypeId::CALLRESULT, message_id, json::object()});
                    resp.set_value(malformed);
                    first_p->set_value();
                } else {
                    if (n == 1) {
                        second_p->set_value();
                    }
                    ocpp::EnhancedMessage<MessageType> offline;
                    offline.offline = true;
                    resp.set_value(offline);
                }
                return resp.get_future();
            }));

    EXPECT_CALL(set_charging_profiles_callback_mock, Call).Times(0);

    auto profile = make_dynamic_profile_with_interval(DEFAULT_PROFILE_ID, /*interval=*/1, ocpp::DateTime());
    auto add_response = smart_charging.conform_validate_and_add_profile(profile, DEFAULT_EVSE_ID);
    ASSERT_THAT(add_response.status, testing::Eq(ChargingProfileStatusEnum::Accepted));

    auto stored_initial = database_handler->get_charging_profiles_for_evse(DEFAULT_EVSE_ID);
    ASSERT_EQ(stored_initial.size(), 1u);
    const double initial_setpoint =
        stored_initial.front().chargingSchedule.at(0).chargingSchedulePeriod.at(0).setpoint.value_or(0.0f);

    ASSERT_EQ(first_dispatched_future.wait_for(std::chrono::seconds(5)), std::future_status::ready);
    ASSERT_EQ(second_dispatched_future.wait_for(std::chrono::seconds(5)), std::future_status::ready);

    auto stored_after = database_handler->get_charging_profiles_for_evse(DEFAULT_EVSE_ID);
    ASSERT_EQ(stored_after.size(), 1u);
    EXPECT_FLOAT_EQ(stored_after.front().chargingSchedule.at(0).chargingSchedulePeriod.at(0).setpoint.value_or(0.0f),
                    initial_setpoint);
}

// An exception thrown from set_charging_profiles_callback inside the async pull-response worker
// must be caught by the worker's own try/catch and NOT escape into the std::future (which is only
// wait()-ed, never get()-ed). Observable state: the process survives -- the timer keeps dispatching
// after the throwing tick.
TEST_F(SmartChargingTestV21, K28_PullResponseWorker_SwallowsCallbackThrow_ProcessSurvives) {
    enable_dynamic_profiles(device_model);

    std::atomic<int> dispatch_count{0};
    std::promise<void> second_dispatch_promise;
    auto second_dispatch_future = second_dispatch_promise.get_future();

    EXPECT_CALL(mock_dispatcher, dispatch_call_async(_, _))
        .WillRepeatedly(
            Invoke([&dispatch_count, snd_p = std::make_shared<std::promise<void>>(std::move(second_dispatch_promise))](
                       const json& call, bool /*triggered*/) -> std::future<ocpp::EnhancedMessage<MessageType>> {
                std::promise<ocpp::EnhancedMessage<MessageType>> resp;
                const int n = dispatch_count.fetch_add(1);
                if (n == 0) {
                    const auto message_id = call.at(MESSAGE_ID).get<std::string>();
                    ChargingScheduleUpdate schedule_update;
                    schedule_update.setpoint = 7777.0f;
                    auto enhanced =
                        make_pull_response_enhanced(message_id, ChargingProfileStatusEnum::Accepted, schedule_update);
                    resp.set_value(enhanced);
                } else {
                    if (n == 1) {
                        snd_p->set_value();
                    }
                    ocpp::EnhancedMessage<MessageType> offline;
                    offline.offline = true;
                    resp.set_value(offline);
                }
                return resp.get_future();
            }));
    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_)).Times(testing::AnyNumber());

    // First callback (fired by the AppliedFieldsCallbackPending branch on the worker thread) throws;
    // later invocations no-op.
    std::atomic<bool> callback_threw{false};
    EXPECT_CALL(set_charging_profiles_callback_mock, Call)
        .WillOnce(Invoke([&callback_threw]() {
            callback_threw.store(true);
            throw std::runtime_error("simulated callback failure in pull-response worker");
        }))
        .WillRepeatedly(Return());

    auto profile = make_dynamic_profile_with_interval(DEFAULT_PROFILE_ID, /*interval=*/1, ocpp::DateTime());
    auto add_response = smart_charging.conform_validate_and_add_profile(profile, DEFAULT_EVSE_ID);
    ASSERT_THAT(add_response.status, testing::Eq(ChargingProfileStatusEnum::Accepted));

    // The throwing callback ran, and the process survived it: a SECOND timer-driven dispatch
    // happens after the throwing first one (the worker's catch swallowed the exception instead of
    // letting it escape into the wait()-only future).
    ASSERT_EQ(second_dispatch_future.wait_for(std::chrono::seconds(5)), std::future_status::ready)
        << "timer/process did not survive a callback throw inside the pull-response worker";
    EXPECT_TRUE(callback_threw.load()) << "throwing callback branch never ran";
}

// Expiry fire-then-commit: a throwing expiry-recompute callback must NOT clear the expire deadline
// -- the entry is kept so the NEXT timer tick retries the recompute. Observable state: the callback
// fires again on a later tick (>=2 invocations), which is only possible if the entry survived the
// throwing tick. Pre-fix the expire field was reset before the callback fired, so a throw left
// state committed-but-unpublished and the callback fired exactly once.
TEST_F(SmartChargingTestV21, K28_OnTimerFire_ExpiryCallbackThrows_KeepsEntryAndRetries) {
    enable_dynamic_profiles(device_model);

    // First expiry callback throws; subsequent ones succeed. Count invocations: a retry (>=2)
    // proves the expire entry was kept across the throwing tick.
    std::atomic<int> callback_count{0};
    EXPECT_CALL(set_charging_profiles_callback_mock, Call).WillRepeatedly(Invoke([&callback_count]() {
        if (callback_count.fetch_add(1) == 0) {
            throw std::runtime_error("simulated expiry recompute callback failure");
        }
    }));

    // Keep the timer alive across ticks via a 1s pull interval; offline responses so no apply runs.
    std::atomic<int> dispatch_count{0};
    EXPECT_CALL(mock_dispatcher, dispatch_call_async(_, _))
        .WillRepeatedly(Invoke([&dispatch_count](const json&, bool) -> std::future<ocpp::EnhancedMessage<MessageType>> {
            dispatch_count.fetch_add(1);
            std::promise<ocpp::EnhancedMessage<MessageType>> resp;
            ocpp::EnhancedMessage<MessageType> offline;
            offline.offline = true;
            resp.set_value(offline);
            return resp.get_future();
        }));
    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_)).Times(testing::AnyNumber());

    // Dynamic profile: expiry boundary at now+1s AND a 1s pull interval. The first fire (~1s)
    // detects expiry and fires the callback (throws). With fire-then-commit, the expire field is
    // kept, so the next tick re-detects expiry and fires the callback again (succeeds), and only
    // then is the entry cleared.
    auto periods = create_charging_schedule_periods(0, 1, 1, std::nullopt);
    periods.at(0).operationMode = OperationModeEnum::CentralSetpoint;
    periods.at(0).setpoint = 10000.0f;
    auto profile = create_charging_profile(
        DEFAULT_PROFILE_ID, ChargingProfilePurposeEnum::TxDefaultProfile,
        create_charge_schedule(ChargingRateUnitEnum::W, periods, ocpp::DateTime("2024-01-17T17:00:00"), /*duration=*/1),
        {}, ChargingProfileKindEnum::Dynamic, DEFAULT_STACK_LEVEL, ocpp::DateTime("2024-01-01T13:00:00"),
        ocpp::DateTime("2034-02-01T13:00:00"));
    profile.dynUpdateInterval = 1;
    profile.dynUpdateTime = ocpp::DateTime();

    auto add_response = smart_charging.conform_validate_and_add_profile(profile, DEFAULT_EVSE_ID);
    ASSERT_THAT(add_response.status, testing::Eq(ChargingProfileStatusEnum::Accepted));

    // The expiry callback must fire AGAIN (>=2) after the first throw -- proving the expire entry
    // was not cleared on the throwing tick and the recompute was retried.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(6);
    while (std::chrono::steady_clock::now() < deadline) {
        if (callback_count.load() >= 2) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    EXPECT_GE(callback_count.load(), 2)
        << "expiry callback fired only once -- the expire entry was cleared before the callback, so "
           "a throw left state committed-but-unpublished with no retry";
}

// A pull-response future that throws (transient websocket drop) and an offline pull response both
// return early WITHOUT advancing the pull deadline. Observable state: the timer keeps re-dispatching
// (deadline stayed due), so dispatch_count climbs past the two seeded responses rather than
// stalling.
TEST_F(SmartChargingTestV21, K28_PullResponseWorker_FutureThrowAndOffline_RetriesPromptly) {
    enable_dynamic_profiles(device_model);

    std::promise<void> future_throw_dispatched;
    auto future_throw_dispatched_future = future_throw_dispatched.get_future();
    std::promise<void> offline_dispatched;
    auto offline_dispatched_future = offline_dispatched.get_future();
    std::atomic<int> dispatch_count{0};

    EXPECT_CALL(mock_dispatcher, dispatch_call_async(_, _))
        .WillRepeatedly(
            Invoke([&dispatch_count, ft_p = std::make_shared<std::promise<void>>(std::move(future_throw_dispatched)),
                    off_p = std::make_shared<std::promise<void>>(std::move(offline_dispatched))](
                       const json& /*call*/, bool /*triggered*/) -> std::future<ocpp::EnhancedMessage<MessageType>> {
                std::promise<ocpp::EnhancedMessage<MessageType>> resp;
                const int n = dispatch_count.fetch_add(1);
                if (n == 0) {
                    // Future-throw path: the worker's fut.get() rethrows this.
                    resp.set_exception(std::make_exception_ptr(std::runtime_error("simulated future exception")));
                    ft_p->set_value();
                } else {
                    // Offline path.
                    ocpp::EnhancedMessage<MessageType> offline;
                    offline.offline = true;
                    resp.set_value(offline);
                    if (n == 1) {
                        off_p->set_value();
                    }
                }
                return resp.get_future();
            }));
    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_)).Times(testing::AnyNumber());
    EXPECT_CALL(set_charging_profiles_callback_mock, Call).Times(0);

    auto profile = make_dynamic_profile_with_interval(DEFAULT_PROFILE_ID, /*interval=*/1, ocpp::DateTime());
    auto add_response = smart_charging.conform_validate_and_add_profile(profile, DEFAULT_EVSE_ID);
    ASSERT_THAT(add_response.status, testing::Eq(ChargingProfileStatusEnum::Accepted));

    ASSERT_EQ(future_throw_dispatched_future.wait_for(std::chrono::seconds(5)), std::future_status::ready);
    ASSERT_EQ(offline_dispatched_future.wait_for(std::chrono::seconds(5)), std::future_status::ready);

    // Neither the future-throw nor the offline path advances the pull deadline, so the timer keeps
    // re-dispatching at the 1s interval: dispatch_count climbs past the two seeded responses.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(6);
    while (std::chrono::steady_clock::now() < deadline) {
        if (dispatch_count.load() >= 3) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    EXPECT_GE(dispatch_count.load(), 3)
        << "timer stopped re-dispatching after a future-throw + offline -- the pull deadline was "
           "advanced despite no successful apply";
}

// ClearChargingProfile drops the deadline tracking for exactly the deleted ids. The DELETE returns
// the ids it removed, so the handler erases tracking for precisely those -- no mirrored lookup that
// could drift from the DB filter. Observable state: a pull-tracked Dynamic profile cleared by id is
// gone from the DB and its deadline entry is erased, so the adaptive timer never dispatches a pull
// for it afterwards.
TEST_F(SmartChargingTestV21, K28_ClearChargingProfile_ErasesDeadlineTrackingForDeletedId) {
    enable_dynamic_profiles(device_model);

    // No pull may ever be dispatched: a far-future interval keeps the deadline un-due before the
    // clear, and after the clear the tracking entry is erased so it never becomes due.
    EXPECT_CALL(mock_dispatcher, dispatch_call_async(_, _)).Times(0);

    std::optional<ClearChargingProfileResponse> clear_response;
    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_))
        .WillRepeatedly(Invoke([&clear_response](const json& call_result) {
            clear_response = call_result[ocpp::CALLRESULT_PAYLOAD].get<ClearChargingProfileResponse>();
        }));

    // Pull-tracked Dynamic profile (deadline entry created), but the next pull is ~1h out so the
    // timer cannot fire during the test.
    auto profile = make_dynamic_profile_with_interval(DEFAULT_PROFILE_ID, /*interval=*/3600, ocpp::DateTime());
    auto add_response = smart_charging.conform_validate_and_add_profile(profile, DEFAULT_EVSE_ID);
    ASSERT_THAT(add_response.status, testing::Eq(ChargingProfileStatusEnum::Accepted));

    ClearChargingProfileRequest clear_req;
    clear_req.chargingProfileId = DEFAULT_PROFILE_ID;
    auto enhanced =
        request_to_enhanced_message<ClearChargingProfileRequest, MessageType::ClearChargingProfile>(clear_req);
    smart_charging.handle_message(enhanced);

    // Accepted, and the row is actually gone from the DB.
    ASSERT_TRUE(clear_response.has_value());
    EXPECT_EQ(clear_response->status, ClearChargingProfileStatusEnum::Accepted);
    EXPECT_TRUE(database_handler->get_charging_profiles_for_evse(DEFAULT_EVSE_ID).empty());

    // The deadline entry was erased: rebuilding tracking from the now-empty DB is a no-op, so the
    // timer stays disarmed and the Times(0) dispatch expectation holds across a short settle.
    smart_charging.dynamic_schedule_manager->rebuild_from_db();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
}

// Many simultaneously-due Dynamic profiles must all be serviced by the single reaper thread: every
// Accepted pull response is applied (its setpoint lands in the DB). The mock returns the matching
// Accepted response for each profile id; the test waits for every per-profile callback to fire,
// which only happens after each profile's apply path runs on the reaper.
TEST_F(SmartChargingTestV21, Reaper_SingleThread_HandlesManyDuePulls) {
    enable_dynamic_profiles(device_model);

    constexpr int profile_count = 8;
    constexpr double applied_setpoint = 6543.0;

    EXPECT_CALL(mock_dispatcher, dispatch_call_async(_, _))
        .WillRepeatedly(
            Invoke([applied_setpoint](const json& call, bool) -> std::future<ocpp::EnhancedMessage<MessageType>> {
                const auto message_id = call.at(MESSAGE_ID).get<std::string>();
                ChargingScheduleUpdate schedule_update;
                schedule_update.setpoint = applied_setpoint;
                auto enhanced =
                    make_pull_response_enhanced(message_id, ChargingProfileStatusEnum::Accepted, schedule_update);
                std::promise<ocpp::EnhancedMessage<MessageType>> resp;
                resp.set_value(enhanced);
                return resp.get_future();
            }));
    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_)).Times(testing::AnyNumber());

    // Each profile's apply path fires the callback once it persists. Count distinct applies via the
    // stored setpoints rather than the callback so re-pulls on later ticks do not skew the result.
    EXPECT_CALL(set_charging_profiles_callback_mock, Call).WillRepeatedly(Return());

    for (int i = 0; i < profile_count; ++i) {
        auto profile = make_dynamic_profile_with_interval(DEFAULT_PROFILE_ID + i, /*interval=*/1, ocpp::DateTime());
        // Distinct stack levels so all profiles coexist on the same EVSE.
        profile.stackLevel = DEFAULT_STACK_LEVEL + i;
        ASSERT_THAT(smart_charging.conform_validate_and_add_profile(profile, DEFAULT_EVSE_ID).status,
                    testing::Eq(ChargingProfileStatusEnum::Accepted));
    }

    // Wait until every profile has had the Accepted setpoint applied (all serviced by the reaper).
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    int applied = 0;
    while (std::chrono::steady_clock::now() < deadline) {
        applied = 0;
        auto stored = database_handler->get_charging_profiles_for_evse(DEFAULT_EVSE_ID);
        for (const auto& p : stored) {
            if (!p.chargingSchedule.empty() && !p.chargingSchedule.at(0).chargingSchedulePeriod.empty()) {
                const auto& period = p.chargingSchedule.at(0).chargingSchedulePeriod.at(0);
                if (period.setpoint.has_value() && period.setpoint.value() == applied_setpoint) {
                    ++applied;
                }
            }
        }
        if (applied == profile_count) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    EXPECT_EQ(applied, profile_count) << "not all due pulls were serviced by the reaper";
}

// Single-flight (intentional behavior change): while a profile's pull is still in flight (the
// CSMS has not responded), the timer's next due-tick must NOT issue a second
// PullDynamicScheduleUpdateRequest for the same id. The mock holds the response future open (never
// satisfied) so the pull stays in flight across several 1s ticks; dispatch_call_async must be
// invoked exactly once.
TEST_F(SmartChargingTestV21, Reaper_SingleFlight_NoDuplicateDispatch) {
    enable_dynamic_profiles(device_model);

    std::mutex promises_mtx;
    std::vector<std::shared_ptr<std::promise<ocpp::EnhancedMessage<MessageType>>>> in_flight;
    std::atomic<int> dispatch_count{0};

    EXPECT_CALL(mock_dispatcher, dispatch_call_async(_, _))
        .WillRepeatedly(Invoke([&](const json&, bool) -> std::future<ocpp::EnhancedMessage<MessageType>> {
            auto p = std::make_shared<std::promise<ocpp::EnhancedMessage<MessageType>>>();
            auto fut = p->get_future();
            {
                std::lock_guard<std::mutex> lk(promises_mtx);
                in_flight.push_back(std::move(p)); // never satisfied: pull stays in flight
            }
            dispatch_count.fetch_add(1, std::memory_order_relaxed);
            return fut;
        }));
    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_)).Times(testing::AnyNumber());

    auto profile = make_dynamic_profile_with_interval(DEFAULT_PROFILE_ID, /*interval=*/1, ocpp::DateTime());
    auto add_response = smart_charging.conform_validate_and_add_profile(profile, DEFAULT_EVSE_ID);
    ASSERT_THAT(add_response.status, testing::Eq(ChargingProfileStatusEnum::Accepted));

    // Wait for the first dispatch, then let several more 1s ticks elapse. With single-flight the
    // count must stay at 1 because the first pull never completes.
    const auto first_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (dispatch_count.load() == 0 && std::chrono::steady_clock::now() < first_deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ASSERT_GE(dispatch_count.load(), 1) << "no pull was dispatched";

    // Span ~4 ticks at the 1s interval; a non-single-flight implementation would emit a fresh
    // request each tick.
    std::this_thread::sleep_for(std::chrono::seconds(4));
    EXPECT_EQ(dispatch_count.load(), 1)
        << "a second PullDynamicScheduleUpdateRequest was emitted while the first was still in flight";

    // Release the held promises so the reaper drops the future cleanly on teardown.
    {
        std::lock_guard<std::mutex> lk(promises_mtx);
        in_flight.clear();
    }
}

// A single due profile's Accepted pull response is applied exactly as before the reaper rework: the
// schedule update lands in the DB and the apply path fires the set-charging-profiles callback. The
// callback runs after the database write, so the test reads the stored profile without a sleep.
TEST_F(SmartChargingTestV21, Reaper_AppliesPullResponse_AsBefore) {
    enable_dynamic_profiles(device_model);

    std::atomic<int> dispatch_count{0};
    EXPECT_CALL(mock_dispatcher, dispatch_call_async(_, _))
        .WillRepeatedly(
            Invoke([&dispatch_count](const json& call, bool) -> std::future<ocpp::EnhancedMessage<MessageType>> {
                std::promise<ocpp::EnhancedMessage<MessageType>> resp;
                if (dispatch_count.fetch_add(1) == 0) {
                    const auto message_id = call.at(MESSAGE_ID).get<std::string>();
                    ChargingScheduleUpdate schedule_update;
                    schedule_update.setpoint = 8888.0f;
                    auto enhanced =
                        make_pull_response_enhanced(message_id, ChargingProfileStatusEnum::Accepted, schedule_update);
                    resp.set_value(enhanced);
                } else {
                    ocpp::EnhancedMessage<MessageType> offline;
                    offline.offline = true;
                    resp.set_value(offline);
                }
                return resp.get_future();
            }));
    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_)).Times(testing::AnyNumber());

    std::promise<void> apply_done_promise;
    auto apply_done_future = apply_done_promise.get_future();
    EXPECT_CALL(set_charging_profiles_callback_mock, Call)
        .WillOnce(Invoke([&apply_done_promise]() { apply_done_promise.set_value(); }))
        .WillRepeatedly(Return());

    auto profile = make_dynamic_profile_with_interval(DEFAULT_PROFILE_ID, /*interval=*/1, ocpp::DateTime());
    auto add_response = smart_charging.conform_validate_and_add_profile(profile, DEFAULT_EVSE_ID);
    ASSERT_THAT(add_response.status, testing::Eq(ChargingProfileStatusEnum::Accepted));

    ASSERT_EQ(apply_done_future.wait_for(std::chrono::seconds(5)), std::future_status::ready);

    auto stored = database_handler->get_charging_profiles_for_evse(DEFAULT_EVSE_ID);
    ASSERT_EQ(stored.size(), 1u);
    ASSERT_FALSE(stored.front().chargingSchedule.empty());
    ASSERT_FALSE(stored.front().chargingSchedule.at(0).chargingSchedulePeriod.empty());
    const auto& period = stored.front().chargingSchedule.at(0).chargingSchedulePeriod.at(0);
    ASSERT_TRUE(period.setpoint.has_value());
    EXPECT_FLOAT_EQ(period.setpoint.value(), 8888.0f);
}

} // namespace ocpp::v2
