// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2025 Pionix GmbH and Contributors to EVerest

#pragma once

#include "everest/logging.hpp"
#include "ocpp/v2/functional_blocks/smart_charging.hpp"
#include "ocpp/v2/ocpp_types.hpp"
#include "ocpp/v2/profile.hpp"
#include "ocpp/v2/utils.hpp"
#include <ocpp/common/call_types.hpp>
#include <ocpp/v2/ctrlr_component_variables.hpp>
#include <ocpp/v2/database_handler.hpp>
#include <ocpp/v2/device_model.hpp>
#include <ocpp/v2/messages/ClearChargingProfile.hpp>
#include <ocpp/v2/messages/GetChargingProfiles.hpp>
#include <ocpp/v2/messages/GetCompositeSchedule.hpp>
#include <ocpp/v2/messages/SetChargingProfile.hpp>
#include <ocpp/v2/ocpp_enums.hpp>

#include "connectivity_manager_mock.hpp"
#include "database_handler_mock.hpp"
#include "device_model_test_helper.hpp"
#include "lib/ocpp/common/database_testing_utils.hpp"
#include "message_dispatcher_mock.hpp"
#include "mocks/database_handler_fake.hpp"
#include <component_state_manager_mock.hpp>
#include <evse_manager_fake.hpp>
#include <evse_mock.hpp>
#include <evse_security_mock.hpp>
#include <gmock/gmock.h>
#include <ocpp/v2/functional_blocks/functional_block_context.hpp>

#include <atomic>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <memory>
#include <openssl/evp.h>
#include <openssl/md5.h>
#include <openssl/sha.h>
#include <sqlite3.h>
#include <sstream>
#include <string>
#include <system_error>
#include <unistd.h>
#include <vector>

using ::testing::MockFunction;
namespace ocpp::v2 {

static const std::string BASE_JSON_PATH_V2 = std::string(TEST_PROFILES_LOCATION_V2) + "/json";
static const std::string BASE_JSON_PATH_V21 = std::string(TEST_PROFILES_LOCATION_V21) + "/json";

constexpr int NR_OF_EVSES = 1;
constexpr int NR_OF_TWO_EVSES = 2;
constexpr int STATION_WIDE_ID = 0;
constexpr int DEFAULT_EVSE_ID = 1;
constexpr int DEFAULT_PROFILE_ID = 1;
constexpr int DEFAULT_STACK_LEVEL = 1;
constexpr int DEFAULT_REQUEST_ID = 1;
constexpr std::int32_t DEFAULT_LIMIT_AMPERE = 57;
constexpr std::int32_t DEFAULT_LIMIT_WATT = 55612;
constexpr std::int32_t DEFAULT_NR_PHASES = 3;
static const std::string DEFAULT_TX_ID = "10c75ff7-74f5-44f5-9d01-f649f3ac7b78";

// Operator overloads
bool operator==(const V2XFreqWattPoint& a, const V2XFreqWattPoint& b);
bool operator==(const V2XSignalWattPoint& a, const V2XSignalWattPoint& b);
bool operator==(const ChargingSchedulePeriod& a, const ChargingSchedulePeriod& b);
bool operator!=(const ChargingSchedulePeriod& a, const ChargingSchedulePeriod& b);
bool operator==(const EnhancedChargingSchedulePeriod& a, const EnhancedChargingSchedulePeriod& b);
bool operator!=(const EnhancedChargingSchedulePeriod& a, const EnhancedChargingSchedulePeriod& b);
bool operator==(const EnhancedCompositeSchedule& a, const EnhancedCompositeSchedule& b);
bool operator!=(const EnhancedCompositeSchedule& a, const EnhancedCompositeSchedule& b);
bool operator==(const LimitAtSoC& a, const LimitAtSoC& b);
bool operator==(const ChargingSchedule& a, const ChargingSchedule& b);
bool operator!=(const ChargingSchedule& a, const ChargingSchedule& b);
bool operator==(const period_entry_t& a, const period_entry_t& b);
bool operator!=(const period_entry_t& a, const period_entry_t& b);
bool operator==(const std::vector<period_entry_t>& a, const std::vector<period_entry_t>& b);

std::string to_string(const period_entry_t& entry);
std::ostream& operator<<(std::ostream& os, const period_entry_t& entry);
ocpp::DateTime dt(const std::string& dt_string);

ChargingProfile
create_charging_profile(std::int32_t charging_profile_id, ChargingProfilePurposeEnum charging_profile_purpose,
                        const std::vector<ChargingSchedule>& charging_schedules,
                        std::optional<std::string> transaction_id = {},
                        ChargingProfileKindEnum charging_profile_kind = ChargingProfileKindEnum::Absolute,
                        int stack_level = DEFAULT_STACK_LEVEL, std::optional<ocpp::DateTime> validFrom = {},
                        std::optional<ocpp::DateTime> validTo = {});
ChargingProfile
create_charging_profile(std::int32_t charging_profile_id, ChargingProfilePurposeEnum charging_profile_purpose,
                        ChargingSchedule charging_schedule, std::optional<std::string> transaction_id = {},
                        ChargingProfileKindEnum charging_profile_kind = ChargingProfileKindEnum::Absolute,
                        int stack_level = DEFAULT_STACK_LEVEL, std::optional<ocpp::DateTime> validFrom = {},
                        std::optional<ocpp::DateTime> validTo = {});
ChargingSchedule create_charge_schedule(ChargingRateUnitEnum charging_rate_unit);
ChargingSchedule create_charge_schedule(ChargingRateUnitEnum charging_rate_unit,
                                        const std::vector<ChargingSchedulePeriod>& charging_schedule_period,
                                        std::optional<ocpp::DateTime> start_schedule = std::nullopt,
                                        std::optional<std::int32_t> duration = std::nullopt);
std::vector<ChargingSchedulePeriod>
create_charging_schedule_periods(std::int32_t start_period, std::optional<std::int32_t> number_phases = std::nullopt,
                                 std::optional<std::int32_t> phase_to_use = std::nullopt,
                                 std::optional<double> limit = std::nullopt);
std::vector<ChargingSchedulePeriod> create_charging_schedule_periods(const std::vector<std::int32_t>& start_periods);
std::vector<ChargingSchedulePeriod> create_charging_schedule_periods_with_phases(std::int32_t start_period,
                                                                                 std::int32_t numberPhases,
                                                                                 std::int32_t phaseToUse);
ChargingProfileCriterion
create_charging_profile_criteria(std::optional<std::vector<ocpp::CiString<20>>> sources = std::nullopt,
                                 std::optional<std::vector<std::int32_t>> ids = std::nullopt,
                                 std::optional<ChargingProfilePurposeEnum> purpose = std::nullopt,
                                 std::optional<std::int32_t> stack_level = std::nullopt);
GetChargingProfilesRequest create_get_charging_profile_request(std::int32_t request_id,
                                                               ChargingProfileCriterion criteria,
                                                               std::optional<std::int32_t> evse_id = std::nullopt);
ClearChargingProfileRequest
create_clear_charging_profile_request(std::optional<std::int32_t> id = std::nullopt,
                                      std::optional<ClearChargingProfile> criteria = std::nullopt);
ClearChargingProfile create_clear_charging_profile(std::optional<std::int32_t> evse_id = std::nullopt,
                                                   std::optional<ChargingProfilePurposeEnum> purpose = std::nullopt,
                                                   std::optional<std::int32_t> stack_level = std::nullopt);
namespace SmartChargingTestUtils {
std::vector<ChargingProfile> get_charging_profiles_from_directory(const std::string& path);
ChargingProfile get_charging_profile_from_path(const std::string& path);
ChargingProfile get_charging_profile_from_file(const std::string& filename);
std::vector<ChargingProfile> get_charging_profiles_from_file(const std::string& filename);
std::vector<ChargingProfile> get_baseline_profile_vector();
std::string to_string(std::vector<ChargingProfile>& profiles);
bool validate_profile_result(const std::vector<period_entry_t>& result);
} // namespace SmartChargingTestUtils

class TestSmartCharging : public SmartCharging {
public:
    using SmartCharging::add_profile;
    using SmartCharging::calculate_composite_schedule;
    using SmartCharging::clear_profiles;
    using SmartCharging::dynamic_schedule_manager;
    using SmartCharging::get_reported_profiles;
    using SmartCharging::get_valid_profiles;
    using SmartCharging::get_valid_profiles_for_evse;
    using SmartCharging::SmartCharging;
    using SmartCharging::validate_evse_exists;
    using SmartCharging::validate_phase_conflict;
    using SmartCharging::validate_profile_schedules;
    using SmartCharging::validate_setpoint_within_limit_range;
    using SmartCharging::validate_tx_default_profile;
    using SmartCharging::validate_tx_profile;
    using SmartCharging::verify_no_conflicting_external_constraints_id;
};

class CompositeScheduleTestFixtureV2 : public DatabaseTestingUtils {
protected:
    void SetUp() override;
    void TearDown() override;
    void load_charging_profiles_for_evse(const std::filesystem::path& path, std::int32_t evse_id);
    std::unique_ptr<TestSmartCharging>
    create_smart_charging_handler(const OcppProtocolVersion ocpp_version = OcppProtocolVersion::v201);

public:
    CompositeScheduleTestFixtureV2();
    void reconfigure_for_nr_of_evses(std::int32_t nr_of_evses);

    std::unique_ptr<EvseManagerFake> evse_manager;
    DeviceModelTestHelper device_model_test_helper;
    MockMessageDispatcher mock_dispatcher;
    DeviceModel* device_model;
    ::testing::NiceMock<ConnectivityManagerMock> connectivity_manager;
    ocpp::EvseSecurityMock evse_security;
    ComponentStateManagerMock component_state_manager;
    std::unique_ptr<FunctionalBlockContext> functional_block_context;
    std::unique_ptr<DatabaseHandlerFake> database_handler;
    MockFunction<void()> set_charging_profiles_callback_mock;
    MockFunction<RequestStartStopStatusEnum(const std::int32_t evse_id, const ReasonEnum& stop_reason)>
        stop_transaction_callback_mock;
    std::unique_ptr<TestSmartCharging> handler;
    boost::uuids::random_generator uuid_generator;
    std::atomic<OcppProtocolVersion> ocpp_version = OcppProtocolVersion::v201;
};

class CompositeScheduleTestFixtureV21 : public CompositeScheduleTestFixtureV2 {
public:
    CompositeScheduleTestFixtureV21();
};

class SmartChargingTestV21 : public DatabaseTestingUtils {
protected:
    void SetUp() override {
        const auto& charging_rate_unit_cv = ControllerComponentVariables::ChargingScheduleChargingRateUnit;
        device_model->set_value(charging_rate_unit_cv.component, charging_rate_unit_cv.variable.value(),
                                AttributeEnum::Actual, "A,W", "test", true);

        const auto& ac_phase_switching_cv = ControllerComponentVariables::ACPhaseSwitchingSupported;
        device_model->set_value(ac_phase_switching_cv.component, ac_phase_switching_cv.variable.value(),
                                AttributeEnum::Actual, "true", "test", true);
    }

    void TearDown() override {
        // Each test gets its own DB file (see create_smart_charging), so there is no shared state
        // to reset. Unlink it; if a background K28 worker/timer still holds the connection the
        // inode survives until that fd closes and the next test uses a fresh, distinct path.
        std::error_code ec;
        std::filesystem::remove(this->test_db_path, ec);
    }

    TestSmartCharging create_smart_charging() {
        // Unique DB file per fixture instance. The shared /tmp/ocpp201/cp.db was raced by the
        // DynamicScheduleManager timer/async-worker threads, corrupting migrations for the next
        // test's fixture constructor; a distinct path per test removes the cross-test contention.
        static std::atomic<unsigned> db_seq{0};
        this->test_db_path = std::filesystem::temp_directory_path() /
                             ("ocpp201_test_" + std::to_string(::getpid()) + "_" +
                              std::to_string(db_seq.fetch_add(1, std::memory_order_relaxed)) + ".db");
        std::unique_ptr<everest::db::sqlite::Connection> database_connection =
            std::make_unique<everest::db::sqlite::Connection>(this->test_db_path);
        database_handler =
            std::make_shared<DatabaseHandler>(std::move(database_connection), MIGRATION_FILES_LOCATION_V2);
        database_handler->open_connection();
        device_model = device_model_test_helper.get_device_model();
        // SupportsDynamicProfiles is read in the SmartCharging constructor to decide whether to build
        // the optional DynamicScheduleManager. Set it before constructing so the K28 tests get one.
        const auto& supports_dynamic = ControllerComponentVariables::SupportsDynamicProfiles;
        device_model->set_value(supports_dynamic.component, supports_dynamic.variable.value(), AttributeEnum::Actual,
                                "true", "test", true);
        this->functional_block_context = std::make_unique<FunctionalBlockContext>(
            this->mock_dispatcher, *this->device_model, this->connectivity_manager, *this->evse_manager,
            *this->database_handler, this->evse_security, this->component_state_manager, this->ocpp_version);
        return TestSmartCharging(*functional_block_context, set_charging_profiles_callback_mock.AsStdFunction(),
                                 stop_transaction_callback_mock.AsStdFunction());
    }

    template <class T> void call_to_json(json& j, const ocpp::Call<T>& call) {
        j = json::array();
        j.push_back(ocpp::MessageTypeId::CALL);
        j.push_back(call.uniqueId.get());
        j.push_back(call.msg.get_type());
        j.push_back(json(call.msg));
    }

    template <class T, MessageType M> ocpp::EnhancedMessage<MessageType> request_to_enhanced_message(const T& req) {
        auto message_id = ocpp::create_message_id();
        ocpp::Call<T> call(req);
        call.uniqueId = message_id;
        ocpp::EnhancedMessage<MessageType> enhanced_message;
        enhanced_message.uniqueId = message_id;
        enhanced_message.messageType = M;
        enhanced_message.messageTypeId = ocpp::MessageTypeId::CALL;
        call_to_json(enhanced_message.message, call);
        return enhanced_message;
    }

    // Default values used within the tests
    DeviceModelTestHelper device_model_test_helper;
    MockMessageDispatcher mock_dispatcher;
    std::unique_ptr<EvseManagerFake> evse_manager = std::make_unique<EvseManagerFake>(NR_OF_TWO_EVSES);
    std::shared_ptr<DatabaseHandler> database_handler;
    DeviceModel* device_model;
    ::testing::NiceMock<ConnectivityManagerMock> connectivity_manager;
    ocpp::EvseSecurityMock evse_security;
    ComponentStateManagerMock component_state_manager;
    MockFunction<void()> set_charging_profiles_callback_mock;
    MockFunction<RequestStartStopStatusEnum(const std::int32_t evse_id, const ReasonEnum& stop_reason)>
        stop_transaction_callback_mock;
    std::filesystem::path test_db_path;
    std::unique_ptr<FunctionalBlockContext> functional_block_context;
    TestSmartCharging smart_charging = create_smart_charging();
    std::atomic<OcppProtocolVersion> ocpp_version = OcppProtocolVersion::v21;
};

} // namespace ocpp::v2
