// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#include "ocpp/common/constants.hpp"
#include "ocpp/common/types.hpp"
#include "ocpp/v2/device_model.hpp"
#include "ocpp/v2/device_model_storage_sqlite.hpp"
#include "ocpp/v2/functional_blocks/functional_block_context.hpp"
#include "ocpp/v2/functional_blocks/smart_charging.hpp"
#include "ocpp/v2/init_device_model_db.hpp"
#include "ocpp/v2/ocpp_types.hpp"
#include "ocpp/v2/utils.hpp"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <future>
#include <gtest/gtest.h>
#include <iostream>
#include <memory>
#include <sqlite3.h>
#include <string>
#include <thread>

#include <ocpp/common/call_types.hpp>
#include <ocpp/v2/evse.hpp>

#include <optional>

#include "smart_charging_test_utils.hpp"
#include <smart_charging_matchers.hpp>

#include <chrono>
#include <sstream>
#include <vector>

namespace ocpp::v2 {
static const std::string TX_ID = "f1522902-1170-416f-8e43-9e3bce28fde7";

TEST_F(CompositeScheduleTestFixtureV2, NoSchedulesPresent) {
    const DateTime start_time = ocpp::DateTime("2024-01-02T00:00:00");
    const DateTime end_time = ocpp::DateTime("2024-01-02T01:00:00");

    auto result = handler->calculate_composite_schedule(start_time, end_time, STATION_WIDE_ID, ChargingRateUnitEnum::A,
                                                        false, true);

    EXPECT_EQ(result.evseId, STATION_WIDE_ID);
    EXPECT_EQ(result.scheduleStart, start_time);
    EXPECT_EQ(result.duration, 3600);
    EXPECT_EQ(result.chargingRateUnit, ChargingRateUnitEnum::A);

    // clang-format off
    EXPECT_THAT(result.chargingSchedulePeriod,
                testing::ElementsAre(
                    PeriodEquals(0, DEFAULT_LIMIT_AMPERE)
                ));
    // clang-format on
}

TEST_F(CompositeScheduleTestFixtureV2, ExtraSeconds) {
    this->load_charging_profiles_for_evse("singles/Absolute_301.json", STATION_WIDE_ID);

    const DateTime start_time = ocpp::DateTime("2024-01-01T12:01:59");
    const DateTime end_time = ocpp::DateTime("2024-01-01T13:02:01");

    auto result = handler->calculate_composite_schedule(start_time, end_time, STATION_WIDE_ID, ChargingRateUnitEnum::A,
                                                        false, true);

    EXPECT_EQ(result.evseId, STATION_WIDE_ID);
    EXPECT_EQ(result.scheduleStart, start_time);
    EXPECT_EQ(result.duration, 3602);
    EXPECT_EQ(result.chargingRateUnit, ChargingRateUnitEnum::A);

    // clang-format off
    EXPECT_THAT(result.chargingSchedulePeriod,
                testing::ElementsAre(
                    PeriodEquals(   0, DEFAULT_LIMIT_AMPERE),
                    PeriodEquals(   1, 32.0F),
                    PeriodEquals(1801, 31.0F),
                    PeriodEquals(2701, 30.0F),
                    PeriodEquals(3601, DEFAULT_LIMIT_AMPERE)
                ));
    // clang-format on
}

TEST_F(CompositeScheduleTestFixtureV2, FoundationTest_Grid) {
    this->load_charging_profiles_for_evse(BASE_JSON_PATH_V2 + "/grid/", DEFAULT_EVSE_ID);

    evse_manager->open_transaction(DEFAULT_EVSE_ID, TX_ID);

    const DateTime start_time = ocpp::DateTime("2024-01-17T00:00:00");
    const DateTime end_time = ocpp::DateTime("2024-01-18T00:00:00");
    EnhancedChargingSchedulePeriod period1;
    period1.startPeriod = 0;
    period1.limit = 1.0;
    period1.numberPhases = 1;
    EnhancedChargingSchedulePeriod period2;
    period2.startPeriod = 3600;
    period2.limit = 2.0;
    period2.numberPhases = 1;
    EnhancedChargingSchedulePeriod period3;
    period3.startPeriod = 7200;
    period3.limit = 3.0;
    period3.numberPhases = 1;
    EnhancedChargingSchedulePeriod period4;
    period4.startPeriod = 10800;
    period4.limit = 4.0;
    period4.numberPhases = 1;
    EnhancedChargingSchedulePeriod period5;
    period5.startPeriod = 14400;
    period5.limit = 5.0;
    period5.numberPhases = 1;
    EnhancedChargingSchedulePeriod period6;
    period6.startPeriod = 18000;
    period6.limit = 6.0;
    period6.numberPhases = 1;
    EnhancedChargingSchedulePeriod period7;
    period7.startPeriod = 21600;
    period7.limit = 7.0;
    period7.numberPhases = 1;
    EnhancedChargingSchedulePeriod period8;
    period8.startPeriod = 25200;
    period8.limit = 8.0;
    period8.numberPhases = 1;
    EnhancedChargingSchedulePeriod period9;
    period9.startPeriod = 28800;
    period9.limit = 9.0;
    period9.numberPhases = 1;
    EnhancedChargingSchedulePeriod period10;
    period10.startPeriod = 32400;
    period10.limit = 10.0;
    period10.numberPhases = 1;
    EnhancedChargingSchedulePeriod period11;
    period11.startPeriod = 36000;
    period11.limit = 11.0;
    period11.numberPhases = 1;
    EnhancedChargingSchedulePeriod period12;
    period12.startPeriod = 39600;
    period12.limit = 12.0;
    period12.numberPhases = 1;
    EnhancedChargingSchedulePeriod period13;
    period13.startPeriod = 43200;
    period13.limit = 13.0;
    period13.numberPhases = 1;
    EnhancedChargingSchedulePeriod period14;
    period14.startPeriod = 46800;
    period14.limit = 14.0;
    period14.numberPhases = 1;
    EnhancedChargingSchedulePeriod period15;
    period15.startPeriod = 50400;
    period15.limit = 15.0;
    period15.numberPhases = 1;
    EnhancedChargingSchedulePeriod period16;
    period16.startPeriod = 54000;
    period16.limit = 16.0;
    period16.numberPhases = 1;
    EnhancedChargingSchedulePeriod period17;
    period17.startPeriod = 57600;
    period17.limit = 17.0;
    period17.numberPhases = 1;
    EnhancedChargingSchedulePeriod period18;
    period18.startPeriod = 61200;
    period18.limit = 18.0;
    period18.numberPhases = 1;
    EnhancedChargingSchedulePeriod period19;
    period19.startPeriod = 64800;
    period19.limit = 19.0;
    period19.numberPhases = 1;
    EnhancedChargingSchedulePeriod period20;
    period20.startPeriod = 68400;
    period20.limit = 20.0;
    period20.numberPhases = 1;
    EnhancedChargingSchedulePeriod period21;
    period21.startPeriod = 72000;
    period21.limit = 21.0;
    period21.numberPhases = 1;
    EnhancedChargingSchedulePeriod period22;
    period22.startPeriod = 75600;
    period22.limit = 22.0;
    period22.numberPhases = 1;
    EnhancedChargingSchedulePeriod period23;
    period23.startPeriod = 79200;
    period23.limit = 23.0;
    period23.numberPhases = 1;
    EnhancedChargingSchedulePeriod period24;
    period24.startPeriod = 82800;
    period24.limit = 24.0;
    period24.numberPhases = 1;
    EnhancedCompositeSchedule expected;
    expected.chargingSchedulePeriod = {period1,  period2,  period3,  period4,  period5,  period6,  period7,  period8,
                                       period9,  period10, period11, period12, period13, period14, period15, period16,
                                       period17, period18, period19, period20, period21, period22, period23, period24};
    expected.evseId = DEFAULT_EVSE_ID;
    expected.duration = 86400;
    expected.scheduleStart = start_time;
    expected.chargingRateUnit = ChargingRateUnitEnum::W;

    auto actual = handler->calculate_composite_schedule(start_time, end_time, DEFAULT_EVSE_ID, ChargingRateUnitEnum::W,
                                                        false, false);

    ASSERT_EQ(actual, expected);
}

TEST_F(CompositeScheduleTestFixtureV2, LayeredTest_SameStartTime) {
    this->load_charging_profiles_for_evse(BASE_JSON_PATH_V2 + "/layered/", DEFAULT_EVSE_ID);

    evse_manager->open_transaction(DEFAULT_EVSE_ID, TX_ID);

    // Time Window: START = Stack #1 start time || END = Stack #1 end time
    {
        const DateTime start_time = ocpp::DateTime("2024-01-18T18:04:00");
        const DateTime end_time = ocpp::DateTime("2024-01-18T18:22:00");

        EnhancedChargingSchedulePeriod period;
        period.startPeriod = 0;
        period.limit = 19.0;
        period.numberPhases = 1;
        EnhancedCompositeSchedule expected;
        expected.chargingSchedulePeriod = {period};
        expected.evseId = DEFAULT_EVSE_ID;
        expected.duration = 1080;
        expected.scheduleStart = start_time;
        expected.chargingRateUnit = ChargingRateUnitEnum::W;

        auto actual = handler->calculate_composite_schedule(start_time, end_time, DEFAULT_EVSE_ID,
                                                            ChargingRateUnitEnum::W, false, false);

        ASSERT_EQ(actual, expected);
    }

    // Time Window: START = Stack #1 start time || END = After Stack #1 end time Before next Start #0 start time
    {
        const DateTime start_time = ocpp::DateTime("2024-01-17T18:04:00");
        const DateTime end_time = ocpp::DateTime("2024-01-17T18:33:00");

        EnhancedChargingSchedulePeriod period1;
        period1.startPeriod = 0;
        period1.limit = 2000.0;
        period1.numberPhases = 1;
        period1.stackLevel = 1;
        EnhancedChargingSchedulePeriod period2;
        period2.startPeriod = 1080;
        period2.limit = 19.0;
        period2.numberPhases = 1;
        EnhancedCompositeSchedule expected;
        expected.chargingSchedulePeriod = {period1, period2};
        expected.evseId = DEFAULT_EVSE_ID;
        expected.duration = 1740;
        expected.scheduleStart = start_time;
        expected.chargingRateUnit = ChargingRateUnitEnum::W;

        auto actual = handler->calculate_composite_schedule(start_time, end_time, DEFAULT_EVSE_ID,
                                                            ChargingRateUnitEnum::W, false, false);

        ASSERT_EQ(actual, expected);
    }

    // Time Window: START = Stack #1 start time || END = After next Start #0 start time
    {
        const DateTime start_time = ocpp::DateTime("2024-01-17T18:04:00");
        const DateTime end_time = ocpp::DateTime("2024-01-17T19:04:00");

        EnhancedChargingSchedulePeriod period1;
        period1.startPeriod = 0;
        period1.limit = 2000.0;
        period1.numberPhases = 1;
        period1.stackLevel = 1;
        EnhancedChargingSchedulePeriod period2;
        period2.startPeriod = 1080;
        period2.limit = 19.0;
        period2.numberPhases = 1;
        EnhancedChargingSchedulePeriod period3;
        period3.startPeriod = 3360;
        period3.limit = 20.0;
        period3.numberPhases = 1;

        EnhancedCompositeSchedule expected;
        expected.chargingSchedulePeriod = {period1, period2, period3};
        expected.evseId = DEFAULT_EVSE_ID;
        expected.duration = 3600;
        expected.scheduleStart = start_time;
        expected.chargingRateUnit = ChargingRateUnitEnum::W;

        auto actual = handler->calculate_composite_schedule(start_time, end_time, DEFAULT_EVSE_ID,
                                                            ChargingRateUnitEnum::W, false, false);

        ASSERT_EQ(actual, expected);
    }
}

TEST_F(CompositeScheduleTestFixtureV2, LayeredRecurringTest_FutureStartTime) {
    this->load_charging_profiles_for_evse(BASE_JSON_PATH_V2 + "/layered_recurring/", DEFAULT_EVSE_ID);

    evse_manager->open_transaction(DEFAULT_EVSE_ID, TX_ID);

    const DateTime start_time = ocpp::DateTime("2024-02-17T18:04:00");
    const DateTime end_time = ocpp::DateTime("2024-02-17T18:05:00");

    EnhancedChargingSchedulePeriod period;
    period.startPeriod = 0;
    period.limit = 2000.0;
    period.numberPhases = 1;
    period.stackLevel = 1;
    EnhancedCompositeSchedule expected;
    expected.chargingSchedulePeriod = {period};
    expected.evseId = DEFAULT_EVSE_ID;
    expected.duration = 60;
    expected.scheduleStart = start_time;
    expected.chargingRateUnit = ChargingRateUnitEnum::W;

    auto actual = handler->calculate_composite_schedule(start_time, end_time, DEFAULT_EVSE_ID, ChargingRateUnitEnum::W,
                                                        false, false);

    ASSERT_EQ(actual, expected);
}

TEST_F(CompositeScheduleTestFixtureV2, LayeredTest_PreviousStartTime) {
    this->load_charging_profiles_for_evse("singles/TXProfile_Absolute_Start18-04.json", DEFAULT_EVSE_ID);

    evse_manager->open_transaction(DEFAULT_EVSE_ID, TX_ID);

    const DateTime start_time = ocpp::DateTime("2024-01-17T18:00:00");
    const DateTime end_time = ocpp::DateTime("2024-01-17T18:05:00");
    EnhancedChargingSchedulePeriod period1;
    period1.startPeriod = 0;
    period1.limit = DEFAULT_LIMIT_WATT;
    EnhancedChargingSchedulePeriod period2;
    period2.startPeriod = 240;
    period2.limit = 2000;
    period2.numberPhases = 1;
    period2.stackLevel = 1;
    EnhancedCompositeSchedule expected;
    expected.chargingSchedulePeriod = {period1, period2};
    expected.evseId = DEFAULT_EVSE_ID;
    expected.duration = 300;
    expected.scheduleStart = start_time;
    expected.chargingRateUnit = ChargingRateUnitEnum::W;

    auto actual = handler->calculate_composite_schedule(start_time, end_time, DEFAULT_EVSE_ID, ChargingRateUnitEnum::W,
                                                        false, false);

    ASSERT_EQ(actual, expected);
}

TEST_F(CompositeScheduleTestFixtureV2, LayeredRecurringTest_PreviousStartTime) {
    this->load_charging_profiles_for_evse(BASE_JSON_PATH_V2 + "/layered_recurring/", DEFAULT_EVSE_ID);

    evse_manager->open_transaction(DEFAULT_EVSE_ID, TX_ID);

    const DateTime start_time = ocpp::DateTime("2024-02-19T18:00:00");
    const DateTime end_time = ocpp::DateTime("2024-02-19T19:04:00");
    EnhancedChargingSchedulePeriod period1;
    period1.startPeriod = 0;
    period1.limit = 19.0;
    period1.numberPhases = 1;
    EnhancedChargingSchedulePeriod period2;
    period2.startPeriod = 240;
    period2.limit = 2000;
    period2.numberPhases = 1;
    period2.stackLevel = 1;
    EnhancedChargingSchedulePeriod period3;
    period3.startPeriod = 1320;
    period3.limit = 19.0;
    period3.numberPhases = 1;
    EnhancedChargingSchedulePeriod period4;
    period4.startPeriod = 3600;
    period4.limit = 20.0;
    period4.numberPhases = 1;
    EnhancedCompositeSchedule expected;
    expected.chargingSchedulePeriod = {period1, period2, period3, period4};
    expected.evseId = DEFAULT_EVSE_ID;
    expected.duration = 3840;
    expected.scheduleStart = start_time;
    expected.chargingRateUnit = ChargingRateUnitEnum::W;

    auto actual = handler->calculate_composite_schedule(start_time, end_time, DEFAULT_EVSE_ID, ChargingRateUnitEnum::W,
                                                        false, false);

    ASSERT_EQ(actual, expected);
}

/**
 * Calculate Composite Schedule
 */
TEST_F(CompositeScheduleTestFixtureV2, ValidateBaselineProfileVector) {
    const DateTime start_time = ocpp::DateTime("2024-01-17T18:01:00");
    const DateTime end_time = ocpp::DateTime("2024-01-18T06:00:00");
    std::vector<ChargingProfile> profiles = SmartChargingTestUtils::get_baseline_profile_vector();

    ON_CALL(*database_handler, get_charging_profiles_for_evse(DEFAULT_EVSE_ID))
        .WillByDefault(testing::Return(profiles));

    evse_manager->open_transaction(DEFAULT_EVSE_ID, TX_ID);

    EnhancedChargingSchedulePeriod period1;
    period1.startPeriod = 0;
    period1.limit = 2000.0;
    period1.numberPhases = 1;
    period1.stackLevel = 1;
    EnhancedChargingSchedulePeriod period2;
    period2.startPeriod = 1020;
    period2.limit = 11000.0;
    period2.numberPhases = 3;
    EnhancedChargingSchedulePeriod period3;
    period3.startPeriod = 25140;
    period3.limit = 6000.0;
    period3.numberPhases = 3;
    EnhancedCompositeSchedule expected;
    expected.chargingSchedulePeriod = {period1, period2, period3};
    expected.evseId = DEFAULT_EVSE_ID;
    expected.duration = 43140;
    expected.scheduleStart = start_time;
    expected.chargingRateUnit = ChargingRateUnitEnum::W;

    auto actual = handler->calculate_composite_schedule(start_time, end_time, DEFAULT_EVSE_ID, ChargingRateUnitEnum::W,
                                                        false, false);

    ASSERT_EQ(actual, expected);
}

TEST_F(CompositeScheduleTestFixtureV2, RelativeProfile_minutia) {
    this->load_charging_profiles_for_evse(BASE_JSON_PATH_V2 + "/relative/", DEFAULT_EVSE_ID);

    const DateTime start_time = ocpp::DateTime("2024-05-17T05:00:00");
    const DateTime end_time = ocpp::DateTime("2024-05-17T06:00:00");

    this->evse_manager->open_transaction(DEFAULT_EVSE_ID, TX_ID);
    this->evse_manager->get_evse(DEFAULT_EVSE_ID).get_transaction()->start_time = start_time;

    EnhancedChargingSchedulePeriod period;
    period.startPeriod = 0;
    period.limit = 2000.0;
    period.numberPhases = 1;
    period.stackLevel = 1;
    EnhancedCompositeSchedule expected;
    expected.chargingSchedulePeriod = {period};
    expected.evseId = DEFAULT_EVSE_ID;
    expected.duration = 3600;
    expected.scheduleStart = start_time;
    expected.chargingRateUnit = ChargingRateUnitEnum::W;

    auto actual = handler->calculate_composite_schedule(start_time, end_time, DEFAULT_EVSE_ID, ChargingRateUnitEnum::W,
                                                        false, false);

    ASSERT_EQ(actual, expected);
}

TEST_F(CompositeScheduleTestFixtureV2, RelativeProfile_e2e) {
    const DateTime start_time = ocpp::DateTime("2024-05-17T05:00:00");
    const DateTime end_time = ocpp::DateTime("2024-05-17T06:01:00");

    this->load_charging_profiles_for_evse(BASE_JSON_PATH_V2 + "/relative/", DEFAULT_EVSE_ID);

    this->evse_manager->open_transaction(DEFAULT_EVSE_ID, TX_ID);
    this->evse_manager->get_evse(DEFAULT_EVSE_ID).get_transaction()->start_time = start_time;

    EnhancedChargingSchedulePeriod period1;
    period1.startPeriod = 0;
    period1.limit = 2000.0;
    period1.numberPhases = 1;
    period1.stackLevel = 1;
    EnhancedChargingSchedulePeriod period2;
    period2.startPeriod = 3601;
    period2.limit = 7.0;
    period2.numberPhases = 1;
    EnhancedCompositeSchedule expected;
    expected.chargingSchedulePeriod = {period1, period2};
    expected.evseId = DEFAULT_EVSE_ID;
    expected.duration = 3660;
    expected.scheduleStart = start_time;
    expected.chargingRateUnit = ChargingRateUnitEnum::W;

    auto actual = handler->calculate_composite_schedule(start_time, end_time, DEFAULT_EVSE_ID, ChargingRateUnitEnum::W,
                                                        false, false);

    ASSERT_EQ(actual, expected);
}

TEST_F(CompositeScheduleTestFixtureV2, DemoCaseOne_17th) {
    this->load_charging_profiles_for_evse(BASE_JSON_PATH_V2 + "/case_one/", DEFAULT_EVSE_ID);

    evse_manager->open_transaction(DEFAULT_EVSE_ID, TX_ID);

    const DateTime start_time = ocpp::DateTime("2024-01-17T18:00:00");
    const DateTime end_time = ocpp::DateTime("2024-01-18T06:00:00");

    EnhancedChargingSchedulePeriod period1;
    period1.startPeriod = 0;
    period1.limit = 2000.0;
    period1.numberPhases = 1;
    period1.stackLevel = 1;
    EnhancedChargingSchedulePeriod period2;
    period2.startPeriod = 1080;
    period2.limit = 11000.0;
    period2.numberPhases = 1;
    EnhancedChargingSchedulePeriod period3;
    period3.startPeriod = 25200;
    period3.limit = 6000.0;
    period3.numberPhases = 1;
    EnhancedCompositeSchedule expected;
    expected.chargingSchedulePeriod = {period1, period2, period3};
    expected.evseId = DEFAULT_EVSE_ID;
    expected.duration = 43200;
    expected.scheduleStart = start_time;
    expected.chargingRateUnit = ChargingRateUnitEnum::W;

    auto actual = handler->calculate_composite_schedule(start_time, end_time, DEFAULT_EVSE_ID, ChargingRateUnitEnum::W,
                                                        false, false);

    ASSERT_EQ(actual, expected);
}

TEST_F(CompositeScheduleTestFixtureV2, DemoCaseOne_19th) {
    this->load_charging_profiles_for_evse(BASE_JSON_PATH_V2 + "/case_one/", DEFAULT_EVSE_ID);

    evse_manager->open_transaction(DEFAULT_EVSE_ID, TX_ID);

    const DateTime start_time = ocpp::DateTime("2024-01-19T18:00:00");
    const DateTime end_time = ocpp::DateTime("2024-01-20T06:00:00");

    EnhancedChargingSchedulePeriod period1;
    period1.startPeriod = 0;
    period1.limit = 11000.0;
    period1.numberPhases = 1;
    EnhancedChargingSchedulePeriod period2;
    period2.startPeriod = 25200;
    period2.limit = 6000.0;
    period2.numberPhases = 1;
    EnhancedCompositeSchedule expected;
    expected.chargingSchedulePeriod = {period1, period2};
    expected.evseId = DEFAULT_EVSE_ID;
    expected.duration = 43200;
    expected.scheduleStart = start_time;
    expected.chargingRateUnit = ChargingRateUnitEnum::W;

    auto actual = handler->calculate_composite_schedule(start_time, end_time, DEFAULT_EVSE_ID, ChargingRateUnitEnum::W,
                                                        false, false);

    ASSERT_EQ(actual, expected);
}

TEST_F(CompositeScheduleTestFixtureV2, MaxOverridesHigherLimits) {
    this->load_charging_profiles_for_evse(BASE_JSON_PATH_V2 + "/max/0/", STATION_WIDE_ID);
    this->load_charging_profiles_for_evse(BASE_JSON_PATH_V2 + "/max/1/", DEFAULT_EVSE_ID);

    evse_manager->open_transaction(DEFAULT_EVSE_ID, TX_ID);

    const DateTime start_time = ocpp::DateTime("2024-01-17T00:00:00");
    const DateTime end_time = ocpp::DateTime("2024-01-17T02:00:00");

    EnhancedChargingSchedulePeriod period1;
    period1.startPeriod = 0;
    period1.limit = 10.0;
    period1.numberPhases = 1;
    EnhancedChargingSchedulePeriod period2;
    period2.startPeriod = 3600;
    period2.limit = 20.0;
    period2.numberPhases = 1;
    EnhancedCompositeSchedule expected;
    expected.chargingSchedulePeriod = {period1, period2};
    expected.evseId = DEFAULT_EVSE_ID;
    expected.duration = 7200;
    expected.scheduleStart = start_time;
    expected.chargingRateUnit = ChargingRateUnitEnum::W;

    auto actual = handler->calculate_composite_schedule(start_time, end_time, DEFAULT_EVSE_ID, ChargingRateUnitEnum::W,
                                                        false, false);
    ASSERT_EQ(actual, expected);
}

TEST_F(CompositeScheduleTestFixtureV2, MaxOverridenByLowerLimits) {
    this->load_charging_profiles_for_evse(BASE_JSON_PATH_V2 + "/max/0/", STATION_WIDE_ID);
    this->load_charging_profiles_for_evse(BASE_JSON_PATH_V2 + "/max/1/", DEFAULT_EVSE_ID);

    evse_manager->open_transaction(DEFAULT_EVSE_ID, TX_ID);

    const DateTime start_time = ocpp::DateTime("2024-01-17T22:00:00");
    const DateTime end_time = ocpp::DateTime("2024-01-18T00:00:00");

    EnhancedChargingSchedulePeriod period1;
    period1.startPeriod = 0;
    period1.limit = 230.0;
    period1.numberPhases = 1;
    EnhancedChargingSchedulePeriod period2;
    period2.startPeriod = 3600;
    period2.limit = 10.0;
    period2.numberPhases = 1;
    period2.stackLevel = 1;
    EnhancedCompositeSchedule expected;
    expected.chargingSchedulePeriod = {period1, period2};
    expected.evseId = DEFAULT_EVSE_ID;
    expected.duration = 7200;
    expected.scheduleStart = start_time;
    expected.chargingRateUnit = ChargingRateUnitEnum::W;

    auto actual = handler->calculate_composite_schedule(start_time, end_time, DEFAULT_EVSE_ID, ChargingRateUnitEnum::W,
                                                        false, false);
    ASSERT_EQ(actual, expected);
}

TEST_F(CompositeScheduleTestFixtureV2, ExternalOverridesHigherLimits) {
    this->load_charging_profiles_for_evse(BASE_JSON_PATH_V2 + "/external/0/", STATION_WIDE_ID);
    this->load_charging_profiles_for_evse(BASE_JSON_PATH_V2 + "/external/1/", DEFAULT_EVSE_ID);

    evse_manager->open_transaction(DEFAULT_EVSE_ID, TX_ID);

    const DateTime start_time = ocpp::DateTime("2024-01-17T00:00:00");
    const DateTime end_time = ocpp::DateTime("2024-01-17T02:00:00");

    EnhancedChargingSchedulePeriod period1;
    period1.startPeriod = 0;
    period1.limit = 10.0;
    period1.numberPhases = 1;
    EnhancedChargingSchedulePeriod period2;
    period2.startPeriod = 3600;
    period2.limit = 20.0;
    period2.numberPhases = 1;
    EnhancedCompositeSchedule expected;
    expected.chargingSchedulePeriod = {period1, period2};
    expected.evseId = DEFAULT_EVSE_ID;
    expected.duration = 7200;
    expected.scheduleStart = start_time;
    expected.chargingRateUnit = ChargingRateUnitEnum::W;

    auto actual = handler->calculate_composite_schedule(start_time, end_time, DEFAULT_EVSE_ID, ChargingRateUnitEnum::W,
                                                        false, false);
    ASSERT_EQ(actual, expected);
}

TEST_F(CompositeScheduleTestFixtureV2, ExternalOverridenByLowerLimits) {
    this->load_charging_profiles_for_evse(BASE_JSON_PATH_V2 + "/external/0/", STATION_WIDE_ID);
    this->load_charging_profiles_for_evse(BASE_JSON_PATH_V2 + "/external/1/", DEFAULT_EVSE_ID);

    evse_manager->open_transaction(DEFAULT_EVSE_ID, TX_ID);

    const DateTime start_time = ocpp::DateTime("2024-01-17T22:00:00");
    const DateTime end_time = ocpp::DateTime("2024-01-18T00:00:00");

    EnhancedChargingSchedulePeriod period1;
    period1.startPeriod = 0;
    period1.limit = 230.0;
    period1.numberPhases = 1;
    EnhancedChargingSchedulePeriod period2;
    period2.startPeriod = 3600;
    period2.limit = 10.0;
    period2.numberPhases = 1;
    period2.stackLevel = 1;
    EnhancedCompositeSchedule expected;
    expected.chargingSchedulePeriod = {period1, period2};
    expected.evseId = DEFAULT_EVSE_ID;
    expected.duration = 7200;
    expected.scheduleStart = start_time;
    expected.chargingRateUnit = ChargingRateUnitEnum::W;

    auto actual = handler->calculate_composite_schedule(start_time, end_time, DEFAULT_EVSE_ID, ChargingRateUnitEnum::W,
                                                        false, false);
    ASSERT_EQ(actual, expected);
}

TEST_F(CompositeScheduleTestFixtureV2, OCTT_TC_K_41_CS) {
    this->load_charging_profiles_for_evse(BASE_JSON_PATH_V2 + "/OCCT_TC_K_41_CS/0/", STATION_WIDE_ID);
    this->load_charging_profiles_for_evse(BASE_JSON_PATH_V2 + "/OCCT_TC_K_41_CS/1/", DEFAULT_EVSE_ID);

    evse_manager->open_transaction(DEFAULT_EVSE_ID, TX_ID);

    const DateTime start_time = ocpp::DateTime("2024-08-21T12:24:40");
    const DateTime end_time = ocpp::DateTime("2024-08-21T12:31:20");

    EnhancedChargingSchedulePeriod period1;
    period1.startPeriod = 0;
    period1.limit = 8.0;
    period1.numberPhases = 3;
    EnhancedChargingSchedulePeriod period2;
    period2.startPeriod = 46;
    period2.limit = 10.0;
    period2.numberPhases = 3;
    EnhancedChargingSchedulePeriod period3;
    period3.startPeriod = 196;
    period3.limit = 6.0;
    period3.numberPhases = 3;
    EnhancedChargingSchedulePeriod period4;
    period4.startPeriod = 236;
    period4.limit = 10.0;
    period4.numberPhases = 3;
    EnhancedChargingSchedulePeriod period5;
    period5.startPeriod = 260;
    period5.limit = 8.0;
    period5.numberPhases = 3;
    EnhancedChargingSchedulePeriod period6;
    period6.startPeriod = 300;
    period6.limit = 10.0;
    period6.numberPhases = 3;
    EnhancedCompositeSchedule expected;
    expected.chargingSchedulePeriod = {period1, period2, period3, period4, period5, period6};
    expected.evseId = DEFAULT_EVSE_ID;
    expected.duration = 400;
    expected.scheduleStart = start_time;
    expected.chargingRateUnit = ChargingRateUnitEnum::A;

    auto actual = handler->calculate_composite_schedule(start_time, end_time, DEFAULT_EVSE_ID, ChargingRateUnitEnum::A,
                                                        false, false);

    ASSERT_EQ(actual, expected);
}

TEST_F(CompositeScheduleTestFixtureV2, SingleStationMaxForEvse0) {
    this->load_charging_profiles_for_evse("singles/ChargingStationMaxProfile_401.json", STATION_WIDE_ID);

    const DateTime start_time = ocpp::DateTime("2024-08-21T08:00:00");
    const DateTime end_time = ocpp::DateTime("2024-08-21T09:00:00");

    auto result = handler->calculate_composite_schedule(start_time, end_time, STATION_WIDE_ID, ChargingRateUnitEnum::A,
                                                        false, false);

    EXPECT_EQ(result.evseId, STATION_WIDE_ID);
    EXPECT_EQ(result.scheduleStart, start_time);
    EXPECT_EQ(result.duration, 3600);
    EXPECT_EQ(result.chargingRateUnit, ChargingRateUnitEnum::A);

    // clang-format off
    EXPECT_THAT(result.chargingSchedulePeriod,
                testing::ElementsAre(
                    PeriodEqualsWithPhases(    0, 24.0F, 1),
                    PeriodEqualsWithPhases(  900, 28.0F, 1),
                    PeriodEqualsWithPhases( 1800, 30.0F, 1),
                    PeriodEqualsWithPhases( 2700, 32.0F, 1)
                ));
    // clang-format on
}

TEST_F(CompositeScheduleTestFixtureV2, SingleTxDefaultProfileForEvse0WithSingleEvse) {
    this->load_charging_profiles_for_evse("singles/Relative_303.json", STATION_WIDE_ID);

    const DateTime start_time = ocpp::DateTime("2024-01-02T08:00:00");
    const DateTime end_time = ocpp::DateTime("2024-01-02T09:00:00");

    auto result = handler->calculate_composite_schedule(start_time, end_time, STATION_WIDE_ID, ChargingRateUnitEnum::A,
                                                        false, true);

    EXPECT_EQ(result.evseId, STATION_WIDE_ID);
    EXPECT_EQ(result.scheduleStart, start_time);
    EXPECT_EQ(result.duration, 3600);
    EXPECT_EQ(result.chargingRateUnit, ChargingRateUnitEnum::A);

    // clang-format off
    EXPECT_THAT(result.chargingSchedulePeriod,
                testing::ElementsAre(
                    PeriodEqualsWithPhases(    0, 16.0F, 3),
                    PeriodEqualsWithPhases( 1800, 15.0F, 3),
                    PeriodEqualsWithPhases( 2700, 14.0F, 3)
                ));
    // clang-format on
}

TEST_F(CompositeScheduleTestFixtureV2, SingleTxDefaultProfileForEvse0WithMultipleEvses_Current) {
    this->load_charging_profiles_for_evse("singles/Relative_303.json", STATION_WIDE_ID);

    const DateTime start_time = ocpp::DateTime("2024-01-02T08:00:00");
    const DateTime end_time = ocpp::DateTime("2024-01-02T09:00:00");

    constexpr std::int32_t nr_of_evses = 2;

    this->reconfigure_for_nr_of_evses(nr_of_evses);

    auto result = handler->calculate_composite_schedule(start_time, end_time, STATION_WIDE_ID, ChargingRateUnitEnum::A,
                                                        false, true);

    EXPECT_EQ(result.evseId, STATION_WIDE_ID);
    EXPECT_EQ(result.scheduleStart, start_time);
    EXPECT_EQ(result.duration, 3600);
    EXPECT_EQ(result.chargingRateUnit, ChargingRateUnitEnum::A);

    // clang-format off
    EXPECT_THAT(result.chargingSchedulePeriod,
                testing::ElementsAre(
                    PeriodEqualsWithPhases(    0, nr_of_evses * 16.0F, 3),
                    PeriodEqualsWithPhases( 1800, nr_of_evses * 15.0F, 3),
                    PeriodEqualsWithPhases( 2700, nr_of_evses * 14.0F, 3)
                ));
    // clang-format on
}

TEST_F(CompositeScheduleTestFixtureV2, SingleTxDefaultProfileForEvse0WithMultipleEvses_Power) {
    this->load_charging_profiles_for_evse("singles/TXDefaultProfile_25_Watt.json", STATION_WIDE_ID);

    constexpr std::int32_t nr_of_evses = 2;

    this->reconfigure_for_nr_of_evses(nr_of_evses);

    // Note the time is 1 minute off the whole hour
    const DateTime start_time = ocpp::DateTime("2024-01-02T08:00:00");
    const DateTime end_time = ocpp::DateTime("2024-01-02T09:00:00");

    auto result = handler->calculate_composite_schedule(start_time, end_time, STATION_WIDE_ID, ChargingRateUnitEnum::A,
                                                        false, true);

    EXPECT_EQ(result.evseId, STATION_WIDE_ID);
    EXPECT_EQ(result.scheduleStart, start_time);
    EXPECT_EQ(result.duration, 3600);
    EXPECT_EQ(result.chargingRateUnit, ChargingRateUnitEnum::A);

    // clang-format off
    EXPECT_THAT(result.chargingSchedulePeriod,
                testing::ElementsAre(
                    PeriodEqualsWithPhases(    0, 36.0F, 3),
                    PeriodEqualsWithPhases(  300, 24.0F, 3),
                    PeriodEqualsWithPhases(  600, 18.0F, 3),
                    PeriodEqualsWithPhases(  900, 12.0F, 3),
                    PeriodEqualsWithPhases( 1200,  6.0F, 3)
                ));
    // clang-format on
}

TEST_F(CompositeScheduleTestFixtureV2, SingleTxDefaultProfileWithStationMaxForEvse0WithSingleEvse) {
    this->load_charging_profiles_for_evse("singles/ChargingStationMaxProfile_401.json", STATION_WIDE_ID);
    this->load_charging_profiles_for_evse("singles/Relative_303.json", DEFAULT_EVSE_ID);

    // Note the time is 1 minute off the whole hour
    const DateTime start_time = ocpp::DateTime("2024-01-02T08:01:00");
    const DateTime end_time = ocpp::DateTime("2024-01-02T09:01:00");

    auto result = handler->calculate_composite_schedule(start_time, end_time, STATION_WIDE_ID, ChargingRateUnitEnum::A,
                                                        false, true);

    EXPECT_EQ(result.evseId, STATION_WIDE_ID);
    EXPECT_EQ(result.scheduleStart, start_time);
    EXPECT_EQ(result.duration, 3600);
    EXPECT_EQ(result.chargingRateUnit, ChargingRateUnitEnum::A);

    // clang-format off
    EXPECT_THAT(result.chargingSchedulePeriod,
                testing::ElementsAre(
                    PeriodEqualsWithPhases(    0, 16.0F, 1),
                    PeriodEqualsWithPhases( 1800, 15.0F, 1),
                    PeriodEqualsWithPhases( 2700, 14.0F, 1)
                ));
    // clang-format on
}

TEST_F(CompositeScheduleTestFixtureV2, SingleTxDefaultProfileWithStationMaxForEvse0WithMultipleEvses) {
    this->load_charging_profiles_for_evse("singles/ChargingStationMaxProfile_401.json", STATION_WIDE_ID);
    this->load_charging_profiles_for_evse("singles/Relative_303.json", DEFAULT_EVSE_ID);
    this->load_charging_profiles_for_evse("singles/Relative_303.json", 2);

    constexpr std::int32_t nr_of_evses = 2;

    this->reconfigure_for_nr_of_evses(nr_of_evses);

    // Note the time is 1 minute off the whole hour
    const DateTime start_time = ocpp::DateTime("2024-01-02T08:01:00");
    const DateTime end_time = ocpp::DateTime("2024-01-02T09:01:00");

    auto result = handler->calculate_composite_schedule(start_time, end_time, STATION_WIDE_ID, ChargingRateUnitEnum::A,
                                                        false, true);

    EXPECT_EQ(result.evseId, STATION_WIDE_ID);
    EXPECT_EQ(result.scheduleStart, start_time);
    EXPECT_EQ(result.duration, 3600);
    EXPECT_EQ(result.chargingRateUnit, ChargingRateUnitEnum::A);

    // clang-format off
    EXPECT_THAT(result.chargingSchedulePeriod,
                testing::ElementsAre(
                    PeriodEqualsWithPhases(    0, 24.0F, 1),
                    PeriodEqualsWithPhases(  840, 28.0F, 1),
                    PeriodEqualsWithPhases( 1740, 30.0F, 1),
                    PeriodEqualsWithPhases( 2700, 28.0F, 1)
                ));
    // clang-format on
}

TEST_F(CompositeScheduleTestFixtureV2, TxProfilePerEvseWithMultipleEvses) {
    this->load_charging_profiles_for_evse("singles/Recurring_Daily_301.json", DEFAULT_EVSE_ID);
    this->load_charging_profiles_for_evse("singles/Relative_303.json", 2);

    constexpr std::int32_t nr_of_evses = 2;

    this->reconfigure_for_nr_of_evses(nr_of_evses);

    // Note the time is 1 minute off the whole hour
    const DateTime start_time = ocpp::DateTime("2024-01-02T08:01:00");
    const DateTime end_time = ocpp::DateTime("2024-01-02T09:01:00");

    auto result = handler->calculate_composite_schedule(start_time, end_time, STATION_WIDE_ID, ChargingRateUnitEnum::A,
                                                        false, true);

    EXPECT_EQ(result.evseId, STATION_WIDE_ID);
    EXPECT_EQ(result.scheduleStart, start_time);
    EXPECT_EQ(result.duration, 3600);
    EXPECT_EQ(result.chargingRateUnit, ChargingRateUnitEnum::A);

    // clang-format off
    EXPECT_THAT(result.chargingSchedulePeriod,
                testing::ElementsAre(
                    PeriodEqualsWithPhases(    0, 32.0F, 3),
                    PeriodEqualsWithPhases( 1740, 31.0F, 3),
                    PeriodEqualsWithPhases( 1800, 30.0F, 3),
                    PeriodEqualsWithPhases( 2640, 29.0F, 3),
                    PeriodEqualsWithPhases( 2700, 28.0F, 3),
                    PeriodEqualsWithPhases( 3540, 14.0F + DEFAULT_LIMIT_AMPERE, 3)
                ));
    // clang-format on
}

TEST_F(CompositeScheduleTestFixtureV2, MixingCurrentAndPower_16A_1P) {
    this->load_charging_profiles_for_evse("singles/ChargingStationMaxProfile_24_Ampere.json", STATION_WIDE_ID);
    this->load_charging_profiles_for_evse("singles/TXDefaultProfile_25_Watt.json", DEFAULT_EVSE_ID);

    const DateTime start_time = ocpp::DateTime("2024-01-02T00:00:00");
    const DateTime end_time = ocpp::DateTime("2024-01-02T01:00:00");

    auto result = handler->calculate_composite_schedule(start_time, end_time, DEFAULT_EVSE_ID, ChargingRateUnitEnum::A,
                                                        false, true);

    EXPECT_EQ(result.evseId, DEFAULT_EVSE_ID);
    EXPECT_EQ(result.scheduleStart, start_time);
    EXPECT_EQ(result.duration, 3600);
    EXPECT_EQ(result.chargingRateUnit, ChargingRateUnitEnum::A);

    // clang-format off
    EXPECT_THAT(result.chargingSchedulePeriod,
                testing::ElementsAre(
                    PeriodEqualsWithPhases(    0, 16.0F, 1),
                    PeriodEqualsWithPhases( 1200,  9.0F, 1)
                ));
    // clang-format on
}

TEST_F(CompositeScheduleTestFixtureV2, MixingCurrentAndPower_16A_3P) {
    this->load_charging_profiles_for_evse("singles/ChargingStationMaxProfile_24_Ampere.json", STATION_WIDE_ID);
    this->load_charging_profiles_for_evse("singles/TXDefaultProfile_25_Watt.json", DEFAULT_EVSE_ID);

    const DateTime start_time = ocpp::DateTime("2024-01-02T01:00:00");
    const DateTime end_time = ocpp::DateTime("2024-01-02T02:00:00");

    auto result = handler->calculate_composite_schedule(start_time, end_time, DEFAULT_EVSE_ID, ChargingRateUnitEnum::A,
                                                        false, true);

    EXPECT_EQ(result.evseId, DEFAULT_EVSE_ID);
    EXPECT_EQ(result.scheduleStart, start_time);
    EXPECT_EQ(result.duration, 3600);
    EXPECT_EQ(result.chargingRateUnit, ChargingRateUnitEnum::A);

    // clang-format off
    EXPECT_THAT(result.chargingSchedulePeriod,
                testing::ElementsAre(
                    PeriodEqualsWithPhases(    0, 16.0F, 3),
                    PeriodEqualsWithPhases(  300, 12.0F, 3),
                    PeriodEqualsWithPhases(  600,  9.0F, 3),
                    PeriodEqualsWithPhases(  900,  6.0F, 3),
                    PeriodEqualsWithPhases( 1200,  3.0F, 3)
                ));
    // clang-format on
}

TEST_F(CompositeScheduleTestFixtureV2, MixingCurrentAndPower_10A_1P) {
    this->load_charging_profiles_for_evse("singles/ChargingStationMaxProfile_24_Ampere.json", STATION_WIDE_ID);
    this->load_charging_profiles_for_evse("singles/TXDefaultProfile_25_Watt.json", DEFAULT_EVSE_ID);

    const DateTime start_time = ocpp::DateTime("2024-01-02T02:00:00");
    const DateTime end_time = ocpp::DateTime("2024-01-02T03:00:00");

    auto result = handler->calculate_composite_schedule(start_time, end_time, DEFAULT_EVSE_ID, ChargingRateUnitEnum::A,
                                                        false, true);

    EXPECT_EQ(result.evseId, DEFAULT_EVSE_ID);
    EXPECT_EQ(result.scheduleStart, start_time);
    EXPECT_EQ(result.duration, 3600);
    EXPECT_EQ(result.chargingRateUnit, ChargingRateUnitEnum::A);

    // clang-format off
    EXPECT_THAT(result.chargingSchedulePeriod,
                testing::ElementsAre(
                    PeriodEqualsWithPhases(    0, 10.0F, 1),
                    PeriodEqualsWithPhases( 1200,  9.0F, 1)
                ));
    // clang-format on
}

TEST_F(CompositeScheduleTestFixtureV2, MixingCurrentAndPower_10A_3P) {
    this->load_charging_profiles_for_evse("singles/ChargingStationMaxProfile_24_Ampere.json", STATION_WIDE_ID);
    this->load_charging_profiles_for_evse("singles/TXDefaultProfile_25_Watt.json", DEFAULT_EVSE_ID);

    const DateTime start_time = ocpp::DateTime("2024-01-02T03:00:00");
    const DateTime end_time = ocpp::DateTime("2024-01-02T04:00:00");

    auto result = handler->calculate_composite_schedule(start_time, end_time, DEFAULT_EVSE_ID, ChargingRateUnitEnum::A,
                                                        false, true);

    EXPECT_EQ(result.evseId, DEFAULT_EVSE_ID);
    EXPECT_EQ(result.scheduleStart, start_time);
    EXPECT_EQ(result.duration, 3600);
    EXPECT_EQ(result.chargingRateUnit, ChargingRateUnitEnum::A);

    // clang-format off
    EXPECT_THAT(result.chargingSchedulePeriod,
                testing::ElementsAre(
                    PeriodEqualsWithPhases(    0, 10.0F, 3),
                    PeriodEqualsWithPhases(  600,  9.0F, 3),
                    PeriodEqualsWithPhases(  900,  6.0F, 3),
                    PeriodEqualsWithPhases( 1200,  3.0F, 3)
                ));
    // clang-format on
}

TEST_F(CompositeScheduleTestFixtureV2, MixingCurrentAndPower_10A_1P_RequestPower) {
    this->load_charging_profiles_for_evse("singles/ChargingStationMaxProfile_24_Ampere.json", STATION_WIDE_ID);
    this->load_charging_profiles_for_evse("singles/TXDefaultProfile_25_Watt.json", DEFAULT_EVSE_ID);

    const DateTime start_time = ocpp::DateTime("2024-01-02T02:00:00");
    const DateTime end_time = ocpp::DateTime("2024-01-02T03:00:00");

    auto result = handler->calculate_composite_schedule(start_time, end_time, DEFAULT_EVSE_ID, ChargingRateUnitEnum::W,
                                                        false, true);

    EXPECT_EQ(result.evseId, DEFAULT_EVSE_ID);
    EXPECT_EQ(result.scheduleStart, start_time);
    EXPECT_EQ(result.duration, 3600);
    EXPECT_EQ(result.chargingRateUnit, ChargingRateUnitEnum::W);

    // clang-format off
    EXPECT_THAT(result.chargingSchedulePeriod,
                testing::ElementsAre(
                    PeriodEqualsWithPhases(    0, 2300.0F, 1),
                    PeriodEqualsWithPhases( 1200, 2070.0F, 1)
                ));
    // clang-format on
}

TEST_F(CompositeScheduleTestFixtureV2, MixingCurrentAndPower_10A_3P_RequestPower) {
    this->load_charging_profiles_for_evse("singles/ChargingStationMaxProfile_24_Ampere.json", STATION_WIDE_ID);
    this->load_charging_profiles_for_evse("singles/TXDefaultProfile_25_Watt.json", DEFAULT_EVSE_ID);

    const DateTime start_time = ocpp::DateTime("2024-01-02T03:00:00");
    const DateTime end_time = ocpp::DateTime("2024-01-02T04:00:00");

    auto result = handler->calculate_composite_schedule(start_time, end_time, DEFAULT_EVSE_ID, ChargingRateUnitEnum::W,
                                                        false, true);

    EXPECT_EQ(result.evseId, DEFAULT_EVSE_ID);
    EXPECT_EQ(result.scheduleStart, start_time);
    EXPECT_EQ(result.duration, 3600);
    EXPECT_EQ(result.chargingRateUnit, ChargingRateUnitEnum::W);

    // clang-format off
    EXPECT_THAT(result.chargingSchedulePeriod,
                testing::ElementsAre(
                    PeriodEqualsWithPhases(    0, 6900.0F, 3),
                    PeriodEqualsWithPhases(  600, 6210.0F, 3),
                    PeriodEqualsWithPhases(  900, 4140.0F, 3),
                    PeriodEqualsWithPhases( 1200, 2070.0F, 3)
                ));
    // clang-format on
}

TEST_F(CompositeScheduleTestFixtureV2, TxProfilePerEvseWithMultipleEvses_DifferentNrOfPhases) {
    this->load_charging_profiles_for_evse("singles/Recurring_Daily_302_phase_limit.json", DEFAULT_EVSE_ID);
    this->load_charging_profiles_for_evse("singles/Relative_302_phase_limit.json", 2);

    constexpr std::int32_t nr_of_evses = 2;

    this->reconfigure_for_nr_of_evses(nr_of_evses);

    // Note the time is 1 minute off the whole hour
    const DateTime start_time = ocpp::DateTime("2024-01-02T08:00:00");
    const DateTime end_time = ocpp::DateTime("2024-01-02T09:00:00");

    auto result = handler->calculate_composite_schedule(start_time, end_time, STATION_WIDE_ID, ChargingRateUnitEnum::A,
                                                        false, true);

    EXPECT_EQ(result.evseId, STATION_WIDE_ID);
    EXPECT_EQ(result.scheduleStart, start_time);
    EXPECT_EQ(result.duration, 3600);
    EXPECT_EQ(result.chargingRateUnit, ChargingRateUnitEnum::A);

    // clang-format off
    EXPECT_THAT(result.chargingSchedulePeriod,
                testing::ElementsAre(
                    PeriodEqualsWithPhases(    0, 32.0F, 3),
                    PeriodEqualsWithPhases( 1800, 30.0F, 1),
                    PeriodEqualsWithPhases( 2700, 28.0F, 3)
                ));
    // clang-format on
}

TEST_F(CompositeScheduleTestFixtureV2, MixingNumberOfPhasesOnSingleEvse) {
    this->load_charging_profiles_for_evse("singles/ChargingStationMaxProfile_24_Ampere.json", STATION_WIDE_ID);
    this->load_charging_profiles_for_evse("singles/Relative_302_phase_limit.json", DEFAULT_EVSE_ID);

    // Note the time is 40 minute off the whole hour
    const DateTime start_time = ocpp::DateTime("2024-01-02T00:40:00");
    const DateTime end_time = ocpp::DateTime("2024-01-02T01:40:00");

    auto result = handler->calculate_composite_schedule(start_time, end_time, DEFAULT_EVSE_ID, ChargingRateUnitEnum::A,
                                                        false, true);

    EXPECT_EQ(result.evseId, DEFAULT_EVSE_ID);
    EXPECT_EQ(result.scheduleStart, start_time);
    EXPECT_EQ(result.duration, 3600);
    EXPECT_EQ(result.chargingRateUnit, ChargingRateUnitEnum::A);

    // clang-format off
    EXPECT_THAT(result.chargingSchedulePeriod,
                testing::ElementsAre(
                    PeriodEqualsWithPhases(    0, 16.0F, 1),
                    PeriodEqualsWithPhases( 1200, 16.0F, 3),
                    PeriodEqualsWithPhases( 1800, 15.0F, 1),
                    PeriodEqualsWithPhases( 2700, 14.0F, 3)
                ));
    // clang-format on
}

TEST_F(CompositeScheduleTestFixtureV2, NoGapsWithSequentialProfiles) {
    this->load_charging_profiles_for_evse(BASE_JSON_PATH_V2 + "/no_gap/", STATION_WIDE_ID);

    const DateTime start_time = ocpp::DateTime("2024-01-02T08:00:00");
    const DateTime end_time = ocpp::DateTime("2024-01-02T08:20:00");

    auto result = handler->calculate_composite_schedule(start_time, end_time, DEFAULT_EVSE_ID, ChargingRateUnitEnum::A,
                                                        false, true);

    EXPECT_EQ(result.evseId, DEFAULT_EVSE_ID);
    EXPECT_EQ(result.scheduleStart, start_time);
    EXPECT_EQ(result.duration, 1200);
    EXPECT_EQ(result.chargingRateUnit, ChargingRateUnitEnum::A);

    // clang-format off
    EXPECT_THAT(result.chargingSchedulePeriod,
                testing::ElementsAre(
                    PeriodEquals(   0, 16.0F),
                    PeriodEquals( 300, 12.0F),
                    PeriodEquals( 600, DEFAULT_LIMIT_AMPERE)
                ));
    // clang-format on
}

TEST_F(CompositeScheduleTestFixtureV2, TxDefaultConnector0) {
    this->load_charging_profiles_for_evse("relative/TxDefault_relative.json", STATION_WIDE_ID);

    constexpr std::int32_t nr_of_evses = 2;
    this->reconfigure_for_nr_of_evses(nr_of_evses);

    const DateTime start_time = ocpp::DateTime("2024-01-02T08:00:00");
    const DateTime end_time = ocpp::DateTime("2024-01-02T08:20:00");

    auto result_con0 =
        handler->calculate_composite_schedule(start_time, end_time, 0, ChargingRateUnitEnum::W, false, true);

    EXPECT_EQ(result_con0.evseId, STATION_WIDE_ID);
    EXPECT_EQ(result_con0.scheduleStart, start_time);
    EXPECT_EQ(result_con0.duration, 1200);
    EXPECT_EQ(result_con0.chargingRateUnit, ChargingRateUnitEnum::W);

    EXPECT_THAT(result_con0.chargingSchedulePeriod,
                testing::ElementsAre(PeriodEquals(0, 2000.0F), PeriodEquals(70, 14000.0F), PeriodEquals(140, 0.0F),
                                     PeriodEquals(210, 12000.0F), PeriodEquals(280, 3000.0F),
                                     PeriodEquals(350, 10000.0F)));

    auto result_con1 =
        handler->calculate_composite_schedule(start_time, end_time, 1, ChargingRateUnitEnum::W, false, true);

    EXPECT_EQ(result_con1.evseId, 1);
    EXPECT_EQ(result_con1.scheduleStart, start_time);
    EXPECT_EQ(result_con1.duration, 1200);
    EXPECT_EQ(result_con1.chargingRateUnit, ChargingRateUnitEnum::W);

    EXPECT_THAT(result_con1.chargingSchedulePeriod,
                testing::ElementsAre(PeriodEquals(0, 1000.0F), PeriodEquals(70, 7000.0F), PeriodEquals(140, 0.0F),
                                     PeriodEquals(210, 6000.0F), PeriodEquals(280, 1500.0F),
                                     PeriodEquals(350, 5000.0F)));

    auto result_con2 =
        handler->calculate_composite_schedule(start_time, end_time, 2, ChargingRateUnitEnum::W, false, true);

    EXPECT_EQ(result_con2.evseId, 2);
    EXPECT_EQ(result_con2.scheduleStart, start_time);
    EXPECT_EQ(result_con2.duration, 1200);
    EXPECT_EQ(result_con2.chargingRateUnit, ChargingRateUnitEnum::W);

    EXPECT_THAT(result_con2.chargingSchedulePeriod,
                testing::ElementsAre(PeriodEquals(0, 1000.0F), PeriodEquals(70, 7000.0F), PeriodEquals(140, 0.0F),
                                     PeriodEquals(210, 6000.0F), PeriodEquals(280, 1500.0F),
                                     PeriodEquals(350, 5000.0F)));

    const DateTime tx_start_time = ocpp::DateTime("2024-01-02T08:00:00");
    const DateTime request_time = ocpp::DateTime("2024-01-02T08:03:00");
    auto timer = std::unique_ptr<Everest::SteadyTimer>();

    this->evse_manager->open_transaction(1, "TX_ID_12345", tx_start_time);

    auto result_con1_tx_active =
        handler->calculate_composite_schedule(request_time, end_time, 1, ChargingRateUnitEnum::W, false, true);

    EXPECT_EQ(result_con1_tx_active.evseId, 1);
    EXPECT_EQ(result_con1_tx_active.scheduleStart, request_time);
    EXPECT_EQ(result_con1_tx_active.duration, 1020);
    EXPECT_EQ(result_con1_tx_active.chargingRateUnit, ChargingRateUnitEnum::W);

    EXPECT_THAT(result_con1_tx_active.chargingSchedulePeriod,
                testing::ElementsAre(PeriodEquals(0, 0.0F), PeriodEquals(30, 6000.0F), PeriodEquals(100, 1500.0F),
                                     PeriodEquals(170, 5000.0F)));

    result_con0 =
        handler->calculate_composite_schedule(request_time, end_time, 0, ChargingRateUnitEnum::W, false, true);

    EXPECT_EQ(result_con0.evseId, STATION_WIDE_ID);
    EXPECT_EQ(result_con0.scheduleStart, request_time);
    EXPECT_EQ(result_con0.duration, 1020);
    EXPECT_EQ(result_con0.chargingRateUnit, ChargingRateUnitEnum::W);

    EXPECT_THAT(result_con0.chargingSchedulePeriod,
                testing::ElementsAre(PeriodEquals(0, 1000.0F), PeriodEquals(30, 7000.0F), PeriodEquals(70, 13000.0F),
                                     PeriodEquals(100, 8500.0F), PeriodEquals(140, 1500.0F), PeriodEquals(170, 5000.0F),
                                     PeriodEquals(210, 11000.0F), PeriodEquals(280, 6500.0F),
                                     PeriodEquals(350, 10000.0F)));
}

TEST_F(CompositeScheduleTestFixtureV2, ZeroDuration) {
    // test relating to libocpp issue 1169
    // incorrect composite schedule when duration is zero

    // using absolute schedule with limit 32.0, starting 2024-01-01T12:02:00Z
    // default limit is 57.0 (DEFAULT_LIMIT_AMPERE)

    load_charging_profiles_for_evse("singles/Absolute_301.json", STATION_WIDE_ID);

    const DateTime start_time = ocpp::DateTime("2024-01-01T12:02:00Z");
    const DateTime end_time = ocpp::DateTime("2024-01-01T12:02:01Z");
    const auto expected_limit = 32.0F;

    // check with end time and a corresponding duration of 1 second
    auto result = handler->calculate_composite_schedule(start_time, end_time, STATION_WIDE_ID, ChargingRateUnitEnum::A,
                                                        false, true);

    EXPECT_EQ(result.evseId, STATION_WIDE_ID);
    EXPECT_EQ(result.scheduleStart, start_time);
    EXPECT_EQ(result.duration, 1);
    EXPECT_EQ(result.chargingRateUnit, ChargingRateUnitEnum::A);

    EXPECT_THAT(result.chargingSchedulePeriod, testing::ElementsAre(PeriodEquals(0, expected_limit)));

    // Now with duration of 0 seconds
    result = handler->calculate_composite_schedule(start_time, start_time, STATION_WIDE_ID, ChargingRateUnitEnum::A,
                                                   false, true);

    EXPECT_EQ(result.evseId, STATION_WIDE_ID);
    EXPECT_EQ(result.scheduleStart, start_time);
    EXPECT_EQ(result.duration, 0);
    EXPECT_EQ(result.chargingRateUnit, ChargingRateUnitEnum::A);

    EXPECT_THAT(result.chargingSchedulePeriod, testing::ElementsAre(PeriodEquals(0, expected_limit)));

    // Now with duration of -1 second
    result = handler->calculate_composite_schedule(end_time, start_time, STATION_WIDE_ID, ChargingRateUnitEnum::A,
                                                   false, true);

    EXPECT_EQ(result.evseId, STATION_WIDE_ID);
    EXPECT_EQ(result.scheduleStart, start_time);
    EXPECT_EQ(result.duration, 0);
    EXPECT_EQ(result.chargingRateUnit, ChargingRateUnitEnum::A);

    EXPECT_THAT(result.chargingSchedulePeriod, testing::ElementsAre(PeriodEquals(0, expected_limit)));
}

TEST_F(CompositeScheduleTestFixtureV2, OfflineDuration_Online) {
    this->load_charging_profiles_for_evse(BASE_JSON_PATH_V2 + "/offline_duration/valid_after_offline_duration/",
                                          DEFAULT_EVSE_ID);

    evse_manager->open_transaction(DEFAULT_EVSE_ID, TX_ID);

    EXPECT_CALL(*database_handler, get_charging_profiles_for_evse(testing::_)).Times(testing::AnyNumber());
    EXPECT_CALL(*database_handler, new_statement(testing::_)).Times(testing::AnyNumber());
    EXPECT_CALL(evse_manager->get_mock(1), get_current_phase_type).Times(testing::AnyNumber());

    const auto start_time = ocpp::DateTime{"2024-01-17T18:00:00"};
    const auto end_time = ocpp::DateTime{"2024-01-18T06:00:00"};

    // Profile with CentralSetpoint has higher StackLevel and therefore is preferred
    EnhancedChargingSchedulePeriod expected_period{};
    expected_period.limit = 2000.0;
    expected_period.numberPhases = 3;
    expected_period.setpoint = -2000.0;
    expected_period.stackLevel = 2;
    expected_period.operationMode = OperationModeEnum::CentralSetpoint;
    expected_period.startPeriod = 0;

    EnhancedCompositeSchedule expected_schedule{};
    expected_schedule.chargingSchedulePeriod = {expected_period};
    expected_schedule.evseId = DEFAULT_EVSE_ID;
    expected_schedule.duration = 43200;
    expected_schedule.scheduleStart = start_time;
    expected_schedule.chargingRateUnit = ChargingRateUnitEnum::W;

    const auto actual_schedule = handler->calculate_composite_schedule(start_time, end_time, DEFAULT_EVSE_ID,
                                                                       ChargingRateUnitEnum::W, false, false);

    ASSERT_EQ(actual_schedule, expected_schedule);
}

TEST_F(CompositeScheduleTestFixtureV2, OfflineDuration_OfflineNotLongEnough) {
    this->load_charging_profiles_for_evse(BASE_JSON_PATH_V2 + "/offline_duration/valid_after_offline_duration/",
                                          DEFAULT_EVSE_ID);

    evse_manager->open_transaction(DEFAULT_EVSE_ID, TX_ID);

    EXPECT_CALL(*database_handler, get_charging_profiles_for_evse(testing::_)).Times(testing::AnyNumber());
    EXPECT_CALL(*database_handler, new_statement(testing::_)).Times(testing::AnyNumber());
    EXPECT_CALL(evse_manager->get_mock(1), get_current_phase_type).Times(testing::AnyNumber());

    EXPECT_CALL(connectivity_manager, get_time_disconnected())
        .WillRepeatedly(testing::Return(std::chrono::steady_clock::now() - std::chrono::seconds(300)));

    const auto start_time = ocpp::DateTime{"2024-01-17T18:00:00"};
    const auto end_time = ocpp::DateTime{"2024-01-18T06:00:00"};

    // Profile with CentralSetpoint is preferred, because it is still valid in the offline case
    EnhancedChargingSchedulePeriod expected_period{};
    expected_period.limit = 2000.0;
    expected_period.numberPhases = 3;
    expected_period.setpoint = -2000.0;
    expected_period.stackLevel = 2;
    expected_period.operationMode = OperationModeEnum::CentralSetpoint;
    expected_period.startPeriod = 0;

    EnhancedCompositeSchedule expected_schedule{};
    expected_schedule.chargingSchedulePeriod = {expected_period};
    expected_schedule.evseId = DEFAULT_EVSE_ID;
    expected_schedule.duration = 43200;
    expected_schedule.scheduleStart = start_time;
    expected_schedule.chargingRateUnit = ChargingRateUnitEnum::W;

    const auto actual_schedule = handler->calculate_composite_schedule(start_time, end_time, DEFAULT_EVSE_ID,
                                                                       ChargingRateUnitEnum::W, false, false);

    ASSERT_EQ(actual_schedule, expected_schedule);
}

TEST_F(CompositeScheduleTestFixtureV2, OfflineDuration_OfflineTooLong) {
    this->load_charging_profiles_for_evse(BASE_JSON_PATH_V2 + "/offline_duration/valid_after_offline_duration/",
                                          DEFAULT_EVSE_ID);

    evse_manager->open_transaction(DEFAULT_EVSE_ID, TX_ID);

    EXPECT_CALL(*database_handler, get_charging_profiles_for_evse(testing::_)).Times(testing::AnyNumber());
    EXPECT_CALL(*database_handler, new_statement(testing::_)).Times(testing::AnyNumber());
    EXPECT_CALL(evse_manager->get_mock(1), get_current_phase_type).Times(testing::AnyNumber());

    EXPECT_CALL(connectivity_manager, get_time_disconnected())
        .WillRepeatedly(testing::Return(std::chrono::steady_clock::now() - std::chrono::seconds(900)));

    const auto start_time = ocpp::DateTime{"2024-01-17T18:00:00"};
    const auto end_time = ocpp::DateTime{"2024-01-18T06:00:00"};

    // Profile with ChargingOnly is preferred, because the other one is invalid now that we have been offline for too
    // long
    EnhancedChargingSchedulePeriod expected_period{};
    expected_period.limit = 2000.0;
    expected_period.numberPhases = 3;
    expected_period.startPeriod = 0;
    expected_period.stackLevel = 1;

    EnhancedCompositeSchedule expected_schedule{};
    expected_schedule.chargingSchedulePeriod = {expected_period};
    expected_schedule.evseId = DEFAULT_EVSE_ID;
    expected_schedule.duration = 43200;
    expected_schedule.scheduleStart = start_time;
    expected_schedule.chargingRateUnit = ChargingRateUnitEnum::W;

    const auto actual_schedule = handler->calculate_composite_schedule(start_time, end_time, DEFAULT_EVSE_ID,
                                                                       ChargingRateUnitEnum::W, false, false);

    ASSERT_EQ(actual_schedule, expected_schedule);
}

TEST_F(CompositeScheduleTestFixtureV2, OfflineDuration_OfflineTooLong_ValidAfterOfflineDuration) {
    this->load_charging_profiles_for_evse(BASE_JSON_PATH_V2 + "/offline_duration/valid_after_offline_duration/",
                                          DEFAULT_EVSE_ID);

    evse_manager->open_transaction(DEFAULT_EVSE_ID, TX_ID);

    EXPECT_CALL(*database_handler, get_charging_profiles_for_evse(testing::_)).Times(testing::AnyNumber());
    EXPECT_CALL(*database_handler, new_statement(testing::_)).Times(testing::AnyNumber());
    EXPECT_CALL(evse_manager->get_mock(1), get_current_phase_type).Times(testing::AnyNumber());

    EXPECT_CALL(connectivity_manager, get_time_disconnected())
        .WillRepeatedly(testing::Return(std::chrono::steady_clock::now() - std::chrono::seconds(900)));

    const auto start_time = ocpp::DateTime{"2024-01-17T18:00:00"};
    const auto end_time = ocpp::DateTime{"2024-01-18T06:00:00"};

    // Profile with ChargingOnly is preferred after being offline for too long
    EnhancedChargingSchedulePeriod expected_period{};
    expected_period.limit = 2000.0;
    expected_period.numberPhases = 3;
    expected_period.startPeriod = 0;
    expected_period.stackLevel = 1;

    EnhancedCompositeSchedule expected_schedule{};
    expected_schedule.chargingSchedulePeriod = {expected_period};
    expected_schedule.evseId = DEFAULT_EVSE_ID;
    expected_schedule.duration = 43200;
    expected_schedule.scheduleStart = start_time;
    expected_schedule.chargingRateUnit = ChargingRateUnitEnum::W;

    auto actual_schedule = handler->calculate_composite_schedule(start_time, end_time, DEFAULT_EVSE_ID,
                                                                 ChargingRateUnitEnum::W, false, false);

    testing::Mock::VerifyAndClearExpectations(&connectivity_manager);

    ASSERT_EQ(actual_schedule, expected_schedule);

    EXPECT_CALL(connectivity_manager, get_time_disconnected())
        .WillRepeatedly(testing::Return(std::chrono::time_point<std::chrono::steady_clock>()));

    // Profile with CentralSetpoint is preferred after being online again
    expected_period.setpoint = -2000.0;
    expected_period.stackLevel = 2;
    expected_period.operationMode = OperationModeEnum::CentralSetpoint;
    expected_schedule.chargingSchedulePeriod = {expected_period};

    actual_schedule = handler->calculate_composite_schedule(start_time, end_time, DEFAULT_EVSE_ID,
                                                            ChargingRateUnitEnum::W, false, false);

    ASSERT_EQ(actual_schedule, expected_schedule);
}

TEST_F(CompositeScheduleTestFixtureV2, OfflineDuration_OfflineTooLong_InvalidAfterOfflineDuration) {
    this->load_charging_profiles_for_evse(BASE_JSON_PATH_V2 + "/offline_duration/invalid_after_offline_duration/",
                                          DEFAULT_EVSE_ID);

    evse_manager->open_transaction(DEFAULT_EVSE_ID, TX_ID);

    EXPECT_CALL(*database_handler, get_charging_profiles_for_evse(testing::_)).Times(testing::AnyNumber());
    EXPECT_CALL(*database_handler, new_statement(testing::_)).Times(testing::AnyNumber());
    EXPECT_CALL(evse_manager->get_mock(1), get_current_phase_type).Times(testing::AnyNumber());

    EXPECT_CALL(connectivity_manager, get_time_disconnected())
        .WillRepeatedly(testing::Return(std::chrono::steady_clock::now() - std::chrono::seconds(900)));

    // Profile with invalidAfterOfflineDuration gets cleared when being offline for too long
    EXPECT_CALL(*database_handler, clear_charging_profiles_matching_criteria(std::optional<int>{3}, testing::_));

    const auto start_time = ocpp::DateTime{"2024-01-17T18:00:00"};
    const auto end_time = ocpp::DateTime{"2024-01-18T06:00:00"};

    EnhancedChargingSchedulePeriod expected_period{};
    expected_period.limit = 2000.0;
    expected_period.numberPhases = 3;
    expected_period.startPeriod = 0;
    expected_period.stackLevel = 1;

    EnhancedCompositeSchedule expected_schedule{};
    expected_schedule.chargingSchedulePeriod = {expected_period};
    expected_schedule.evseId = DEFAULT_EVSE_ID;
    expected_schedule.duration = 43200;
    expected_schedule.scheduleStart = start_time;
    expected_schedule.chargingRateUnit = ChargingRateUnitEnum::W;

    auto actual_schedule = handler->calculate_composite_schedule(start_time, end_time, DEFAULT_EVSE_ID,
                                                                 ChargingRateUnitEnum::W, false, false);

    ASSERT_EQ(actual_schedule, expected_schedule);
}

TEST_F(SmartChargingTestV21, OfflineDuration_TimerDeletesAtDeadline) {
    auto profile = SmartChargingTestUtils::get_charging_profile_from_file(
        "offline_duration/invalid_after_offline_duration/TxProfile_invalid_after_offline_duration.json");
    profile.maxOfflineDuration = 1;
    profile.invalidAfterOfflineDuration = true;
    database_handler->insert_or_update_charging_profile(DEFAULT_EVSE_ID, profile);
    const auto disconnected = std::chrono::steady_clock::now();
    ON_CALL(connectivity_manager, get_time_disconnected()).WillByDefault(testing::Return(disconnected));

    auto notified = std::make_shared<std::promise<void>>();
    auto notification = notified->get_future();
    EXPECT_CALL(set_charging_profiles_callback_mock, Call()).Times(1).WillOnce([this, notified, disconnected] {
        EXPECT_GE(std::chrono::steady_clock::now(), disconnected + std::chrono::seconds(1));
        EXPECT_TRUE(database_handler->get_all_charging_profiles().empty());
        notified->set_value();
    });
    smart_charging.on_connection_lost();

    ASSERT_EQ(notification.wait_for(std::chrono::seconds(4)), std::future_status::ready);
    EXPECT_TRUE(database_handler->get_all_charging_profiles().empty());
}

TEST_F(SmartChargingTestV21, OfflineDuration_TimerKeepsReusableProfile) {
    auto profile = SmartChargingTestUtils::get_charging_profile_from_file(
        "offline_duration/invalid_after_offline_duration/TxProfile_invalid_after_offline_duration.json");
    profile.maxOfflineDuration = 1;
    profile.invalidAfterOfflineDuration = false;
    database_handler->insert_or_update_charging_profile(DEFAULT_EVSE_ID, profile);
    const auto disconnected = std::chrono::steady_clock::now();
    ON_CALL(connectivity_manager, get_time_disconnected()).WillByDefault(testing::Return(disconnected));

    auto notified = std::make_shared<std::promise<void>>();
    auto notification = notified->get_future();
    EXPECT_CALL(set_charging_profiles_callback_mock, Call()).Times(1).WillOnce([this, notified, disconnected] {
        EXPECT_GE(std::chrono::steady_clock::now(), disconnected + std::chrono::seconds(1));
        EXPECT_EQ(database_handler->get_all_charging_profiles().size(), 1);
        notified->set_value();
    });
    smart_charging.on_connection_lost();

    ASSERT_EQ(notification.wait_for(std::chrono::seconds(4)), std::future_status::ready);
    const auto remaining = database_handler->get_all_charging_profiles();
    ASSERT_EQ(remaining.size(), 1);
    EXPECT_EQ(remaining.front().id, profile.id);
}

TEST_F(SmartChargingTestV21, OfflineDuration_TimerDeletesAtSeparateDeadlines) {
    auto profile = SmartChargingTestUtils::get_charging_profile_from_file(
        "offline_duration/invalid_after_offline_duration/TxProfile_invalid_after_offline_duration.json");
    profile.maxOfflineDuration = 1;
    profile.invalidAfterOfflineDuration = true;
    database_handler->insert_or_update_charging_profile(DEFAULT_EVSE_ID, profile);
    profile.id += 1;
    profile.maxOfflineDuration = 2;
    database_handler->insert_or_update_charging_profile(DEFAULT_EVSE_ID, profile);
    const auto disconnected = std::chrono::steady_clock::now();
    ON_CALL(connectivity_manager, get_time_disconnected()).WillByDefault(testing::Return(disconnected));

    auto first = std::make_shared<std::promise<void>>();
    auto second = std::make_shared<std::promise<void>>();
    auto first_notification = first->get_future();
    auto second_notification = second->get_future();
    EXPECT_CALL(set_charging_profiles_callback_mock, Call())
        .Times(2)
        .WillOnce([this, first, disconnected, id = profile.id] {
            EXPECT_GE(std::chrono::steady_clock::now(), disconnected + std::chrono::seconds(1));
            const auto remaining = database_handler->get_all_charging_profiles();
            EXPECT_EQ(remaining.size(), 1);
            if (!remaining.empty()) {
                EXPECT_EQ(remaining.front().id, id);
            }
            first->set_value();
        })
        .WillOnce([this, second, disconnected] {
            EXPECT_GE(std::chrono::steady_clock::now(), disconnected + std::chrono::seconds(2));
            EXPECT_TRUE(database_handler->get_all_charging_profiles().empty());
            second->set_value();
        });
    smart_charging.on_connection_lost();

    ASSERT_EQ(first_notification.wait_for(std::chrono::seconds(4)), std::future_status::ready);
    ASSERT_EQ(second_notification.wait_for(std::chrono::seconds(4)), std::future_status::ready);
    EXPECT_TRUE(database_handler->get_all_charging_profiles().empty());
}

TEST_F(SmartChargingTestV21, OfflineDuration_TimerCancelledOnReconnect) {
    auto profile = SmartChargingTestUtils::get_charging_profile_from_file(
        "offline_duration/invalid_after_offline_duration/TxProfile_invalid_after_offline_duration.json");
    profile.maxOfflineDuration = 1;
    profile.invalidAfterOfflineDuration = false;
    database_handler->insert_or_update_charging_profile(DEFAULT_EVSE_ID, profile);
    profile.id += 1;
    profile.maxOfflineDuration = 3;
    profile.invalidAfterOfflineDuration = true;
    database_handler->insert_or_update_charging_profile(DEFAULT_EVSE_ID, profile);
    const auto disconnected = std::chrono::steady_clock::now();
    ON_CALL(connectivity_manager, get_time_disconnected()).WillByDefault(testing::Return(disconnected));

    auto first = std::make_shared<std::promise<void>>();
    auto restored = std::make_shared<std::promise<void>>();
    auto restored_notification = restored->get_future();
    auto unexpected = std::make_shared<std::promise<void>>();
    auto calls = std::make_shared<std::atomic<unsigned>>(0);
    auto first_notification = first->get_future();
    auto unexpected_notification = unexpected->get_future();
    EXPECT_CALL(set_charging_profiles_callback_mock, Call()).WillRepeatedly([first, restored, unexpected, calls] {
        const auto count = ++*calls;
        if (count == 1) {
            first->set_value();
        } else if (count == 2) {
            restored->set_value();
        } else if (count == 3) {
            unexpected->set_value();
        }
    });
    smart_charging.on_connection_lost();
    ASSERT_EQ(first_notification.wait_for(std::chrono::seconds(4)), std::future_status::ready);

    const auto offline_duration = std::chrono::steady_clock::now() - disconnected;
    ASSERT_LT(offline_duration, std::chrono::seconds(3));
    smart_charging.on_connection_restored(offline_duration);
    ASSERT_EQ(restored_notification.wait_for(std::chrono::seconds(4)), std::future_status::ready);
    EXPECT_EQ(unexpected_notification.wait_until(disconnected + std::chrono::milliseconds(3250)),
              std::future_status::timeout);
    EXPECT_EQ(calls->load(), 2);
    EXPECT_EQ(database_handler->get_all_charging_profiles().size(), 2);
}

TEST_F(CompositeScheduleTestFixtureV21, OfflineDuration_DisconnectDatabaseFailureRetriesOffThread) {
    const auto disconnected = std::chrono::steady_clock::now();
    const auto caller = std::this_thread::get_id();
    ON_CALL(connectivity_manager, get_time_disconnected()).WillByDefault(testing::Return(disconnected));
    auto retried = std::make_shared<std::promise<void>>();
    auto retry = retried->get_future();
    EXPECT_CALL(*database_handler, get_all_charging_profiles())
        .WillOnce([caller]() -> std::vector<ChargingProfile> {
            EXPECT_NE(std::this_thread::get_id(), caller);
            throw std::runtime_error("database unavailable");
        })
        .WillOnce([retried, disconnected] {
            EXPECT_GE(std::chrono::steady_clock::now(), disconnected + std::chrono::seconds(1));
            retried->set_value();
            return std::vector<ChargingProfile>{};
        });
    EXPECT_NO_THROW(handler->on_connection_lost());
    EXPECT_EQ(retry.wait_for(std::chrono::seconds(4)), std::future_status::ready);
    handler.reset();
}

TEST_F(CompositeScheduleTestFixtureV21, OfflineDuration_ReconnectDatabaseFailureRetriesOffThread) {
    auto profile = SmartChargingTestUtils::get_charging_profile_from_file(
        "offline_duration/invalid_after_offline_duration/TxProfile_invalid_after_offline_duration.json");
    const auto caller = std::this_thread::get_id();
    const auto started = std::chrono::steady_clock::now();
    ON_CALL(connectivity_manager, get_time_disconnected())
        .WillByDefault(testing::Return(started - std::chrono::seconds(900)));
    EXPECT_CALL(*database_handler, get_all_charging_profiles())
        .WillOnce([caller]() -> std::vector<ChargingProfile> {
            EXPECT_NE(std::this_thread::get_id(), caller);
            throw std::runtime_error("database unavailable");
        })
        .WillOnce([caller, started, profile] {
            EXPECT_NE(std::this_thread::get_id(), caller);
            EXPECT_GE(std::chrono::steady_clock::now(), started + std::chrono::seconds(1));
            return std::vector<ChargingProfile>{profile};
        });
    EXPECT_CALL(*database_handler, clear_charging_profiles_matching_criteria(testing::Eq(profile.id), testing::_))
        .WillOnce(testing::Return(std::vector<std::int32_t>{profile.id}));
    auto notified = std::make_shared<std::promise<void>>();
    auto notification = notified->get_future();
    EXPECT_CALL(set_charging_profiles_callback_mock, Call()).Times(1).WillOnce([notified, caller] {
        EXPECT_NE(std::this_thread::get_id(), caller);
        notified->set_value();
    });
    EXPECT_NO_THROW(handler->on_connection_restored(std::chrono::seconds(900)));
    EXPECT_EQ(notification.wait_for(std::chrono::seconds(4)), std::future_status::ready);
    handler.reset();
}

TEST_F(CompositeScheduleTestFixtureV21, OfflineDuration_ShorterReconnectRetainsPendingDeletions) {
    const auto profile = SmartChargingTestUtils::get_charging_profile_from_file(
        "offline_duration/invalid_after_offline_duration/TxProfile_invalid_after_offline_duration.json");
    auto first = std::make_shared<std::promise<void>>();
    auto second = std::make_shared<std::promise<void>>();
    auto first_scan = first->get_future();
    auto second_scan = second->get_future();
    EXPECT_CALL(*database_handler, get_all_charging_profiles())
        .WillOnce([first]() -> std::vector<ChargingProfile> {
            first->set_value();
            throw std::runtime_error("database unavailable");
        })
        .WillOnce([second]() -> std::vector<ChargingProfile> {
            second->set_value();
            throw std::runtime_error("database unavailable");
        })
        .WillOnce(testing::Return(std::vector<ChargingProfile>{profile}));
    EXPECT_CALL(*database_handler, clear_charging_profiles_matching_criteria(testing::Eq(profile.id), testing::_))
        .WillOnce(testing::Return(std::vector<std::int32_t>{profile.id}));
    auto notified = std::make_shared<std::promise<void>>();
    auto notification = notified->get_future();
    EXPECT_CALL(set_charging_profiles_callback_mock, Call()).Times(1).WillOnce([notified] { notified->set_value(); });

    handler->on_connection_restored(std::chrono::seconds(900));
    ASSERT_EQ(first_scan.wait_for(std::chrono::seconds(4)), std::future_status::ready);
    ON_CALL(connectivity_manager, get_time_disconnected())
        .WillByDefault(testing::Return(std::chrono::steady_clock::now() - std::chrono::seconds(300)));
    handler->on_connection_lost();
    ASSERT_EQ(second_scan.wait_for(std::chrono::seconds(4)), std::future_status::ready);
    handler->on_connection_restored(std::chrono::seconds(300));
    EXPECT_EQ(notification.wait_for(std::chrono::seconds(4)), std::future_status::ready);
    handler.reset();
}

TEST_F(CompositeScheduleTestFixtureV21, OfflineDuration_PendingReconnectExcludesExpiredProfile) {
    const auto profile = SmartChargingTestUtils::get_charging_profile_from_file(
        "offline_duration/invalid_after_offline_duration/TxProfile_invalid_after_offline_duration.json");
    evse_manager->open_transaction(DEFAULT_EVSE_ID, TX_ID);
    ON_CALL(*database_handler, get_charging_profiles_for_evse(DEFAULT_EVSE_ID))
        .WillByDefault(testing::Return(std::vector<ChargingProfile>{profile}));
    ASSERT_EQ(handler->get_valid_profiles_for_evse(DEFAULT_EVSE_ID, {}).size(), 1);

    auto scanned = std::make_shared<std::promise<void>>();
    auto scan = scanned->get_future();
    EXPECT_CALL(*database_handler, get_all_charging_profiles())
        .WillOnce([scanned]() -> std::vector<ChargingProfile> {
            scanned->set_value();
            throw std::runtime_error("database unavailable");
        })
        .WillRepeatedly(testing::Throw(std::runtime_error("database unavailable")));
    EXPECT_CALL(*database_handler, clear_charging_profiles_matching_criteria(testing::Eq(profile.id), testing::_))
        .Times(2)
        .WillRepeatedly(testing::Return(std::vector<std::int32_t>{profile.id}));
    EXPECT_CALL(set_charging_profiles_callback_mock, Call()).Times(0);

    handler->on_connection_restored(std::chrono::seconds(900));
    ASSERT_EQ(scan.wait_for(std::chrono::seconds(4)), std::future_status::ready);
    EXPECT_TRUE(handler->get_valid_profiles_for_evse(DEFAULT_EVSE_ID, {}).empty());
    ON_CALL(connectivity_manager, get_time_disconnected())
        .WillByDefault(testing::Return(std::chrono::steady_clock::now() - std::chrono::seconds(300)));
    handler->on_connection_lost();
    EXPECT_TRUE(handler->get_valid_profiles_for_evse(DEFAULT_EVSE_ID, {}).empty());
    handler.reset();
}

TEST_F(CompositeScheduleTestFixtureV21, OfflineDuration_ReconnectCallbackFailureRetainsNotification) {
    auto profile = SmartChargingTestUtils::get_charging_profile_from_file(
        "offline_duration/invalid_after_offline_duration/TxProfile_invalid_after_offline_duration.json");
    const auto caller = std::this_thread::get_id();
    ON_CALL(connectivity_manager, get_time_disconnected())
        .WillByDefault(testing::Return(std::chrono::steady_clock::now() - std::chrono::seconds(900)));
    EXPECT_CALL(*database_handler, get_all_charging_profiles())
        .WillOnce(testing::Return(std::vector<ChargingProfile>{profile}))
        .WillOnce(testing::Return(std::vector<ChargingProfile>{}));
    EXPECT_CALL(*database_handler, clear_charging_profiles_matching_criteria(testing::Eq(profile.id), testing::_))
        .WillOnce(testing::Return(std::vector<std::int32_t>{profile.id}));
    auto notified = std::make_shared<std::promise<void>>();
    auto notification = notified->get_future();
    auto first_attempt = std::make_shared<std::chrono::steady_clock::time_point>();
    EXPECT_CALL(set_charging_profiles_callback_mock, Call())
        .Times(2)
        .WillOnce([first_attempt, caller] {
            EXPECT_NE(std::this_thread::get_id(), caller);
            *first_attempt = std::chrono::steady_clock::now();
            throw std::runtime_error("consumer unavailable");
        })
        .WillOnce([first_attempt, notified, caller] {
            EXPECT_NE(std::this_thread::get_id(), caller);
            EXPECT_GE(std::chrono::steady_clock::now() - *first_attempt, std::chrono::seconds(1));
            notified->set_value();
        });
    EXPECT_NO_THROW(handler->on_connection_restored(std::chrono::seconds(900)));
    EXPECT_EQ(notification.wait_for(std::chrono::seconds(4)), std::future_status::ready);
    handler.reset();
}

TEST_F(CompositeScheduleTestFixtureV21, OfflineDuration_PartialDeletionRetainsNotification) {
    auto profile = SmartChargingTestUtils::get_charging_profile_from_file(
        "offline_duration/invalid_after_offline_duration/TxProfile_invalid_after_offline_duration.json");
    profile.maxOfflineDuration = 0;
    auto second = profile;
    second.id += 1;
    const auto disconnected = std::chrono::steady_clock::now();
    ON_CALL(connectivity_manager, get_time_disconnected()).WillByDefault(testing::Return(disconnected));
    auto deleted = std::make_shared<std::atomic<bool>>(false);
    ON_CALL(*database_handler, get_all_charging_profiles()).WillByDefault([profile, second, deleted] {
        return deleted->load() ? std::vector<ChargingProfile>{second} : std::vector<ChargingProfile>{profile, second};
    });
    EXPECT_CALL(*database_handler, clear_charging_profiles_matching_criteria(testing::Eq(profile.id), testing::_))
        .WillOnce([deleted, profile] {
            deleted->store(true);
            return std::vector<std::int32_t>{profile.id};
        });
    EXPECT_CALL(*database_handler, clear_charging_profiles_matching_criteria(testing::Eq(second.id), testing::_))
        .WillOnce(testing::Throw(std::runtime_error("delete failed")))
        .WillOnce(testing::Return(std::vector<std::int32_t>{}));
    auto notified = std::make_shared<std::promise<void>>();
    auto notification = notified->get_future();
    EXPECT_CALL(set_charging_profiles_callback_mock, Call()).Times(1).WillOnce([notified] { notified->set_value(); });
    handler->on_connection_lost();
    EXPECT_EQ(notification.wait_for(std::chrono::seconds(4)), std::future_status::ready);
    handler.reset();
}

TEST_F(CompositeScheduleTestFixtureV21, OfflineDuration_CallbackFailureRetriesWithBackoff) {
    auto profile = SmartChargingTestUtils::get_charging_profile_from_file(
        "offline_duration/invalid_after_offline_duration/TxProfile_invalid_after_offline_duration.json");
    profile.maxOfflineDuration = 0;
    profile.invalidAfterOfflineDuration = false;
    ON_CALL(connectivity_manager, get_time_disconnected())
        .WillByDefault(testing::Return(std::chrono::steady_clock::now()));
    ON_CALL(*database_handler, get_all_charging_profiles())
        .WillByDefault(testing::Return(std::vector<ChargingProfile>{profile}));
    auto notified = std::make_shared<std::promise<void>>();
    auto notification = notified->get_future();
    auto attempts = std::make_shared<std::vector<std::chrono::steady_clock::time_point>>();
    EXPECT_CALL(set_charging_profiles_callback_mock, Call()).Times(3).WillRepeatedly([attempts, notified] {
        attempts->push_back(std::chrono::steady_clock::now());
        if (attempts->size() < 3) {
            throw std::runtime_error("consumer unavailable");
        }
        EXPECT_GE(attempts->at(1) - attempts->at(0), std::chrono::seconds(1));
        EXPECT_GE(attempts->at(2) - attempts->at(1), std::chrono::seconds(2));
        notified->set_value();
    });
    handler->on_connection_lost();
    EXPECT_EQ(notification.wait_for(std::chrono::seconds(6)), std::future_status::ready);
    handler.reset();
}

TEST_F(CompositeScheduleTestFixtureV21, OfflineDuration_ProfileAddedAfterDisconnectArmsTimer) {
    const auto disconnected = std::chrono::steady_clock::now();
    ON_CALL(connectivity_manager, get_time_disconnected()).WillByDefault(testing::Return(disconnected));
    auto scanned = std::make_shared<std::promise<void>>();
    auto scan = scanned->get_future();
    auto stored = std::make_shared<std::atomic<bool>>(false);
    auto profile = SmartChargingTestUtils::get_charging_profile_from_file(
        "offline_duration/invalid_after_offline_duration/TxProfile_invalid_after_offline_duration.json");
    profile.maxOfflineDuration = 0;
    profile.invalidAfterOfflineDuration = false;
    ON_CALL(*database_handler, get_all_charging_profiles()).WillByDefault([stored, scanned, profile] {
        if (stored->load()) {
            return std::vector<ChargingProfile>{profile};
        }
        scanned->set_value();
        return std::vector<ChargingProfile>{};
    });
    auto notified = std::make_shared<std::promise<void>>();
    auto notification = notified->get_future();
    EXPECT_CALL(set_charging_profiles_callback_mock, Call()).Times(1).WillOnce([notified] { notified->set_value(); });
    handler->on_connection_lost();
    ASSERT_EQ(scan.wait_for(std::chrono::seconds(4)), std::future_status::ready);
    stored->store(true);
    ASSERT_EQ(handler->add_profile(profile, DEFAULT_EVSE_ID).status, ChargingProfileStatusEnum::Accepted);
    EXPECT_EQ(notification.wait_for(std::chrono::seconds(4)), std::future_status::ready);
    handler.reset();
}

TEST_F(SmartChargingTestV21, OfflineDuration_ReconnectDeletesExpiredProfile) {
    const auto profiles = SmartChargingTestUtils::get_charging_profiles_from_directory(
        BASE_JSON_PATH_V2 + "/offline_duration/invalid_after_offline_duration/");
    for (auto profile : profiles) {
        profile.stackLevel -= 1;
        database_handler->insert_or_update_charging_profile(DEFAULT_EVSE_ID, profile);
    }
    evse_manager->open_transaction(DEFAULT_EVSE_ID, TX_ID);

    ON_CALL(connectivity_manager, get_time_disconnected())
        .WillByDefault(testing::Return(std::chrono::steady_clock::now() - std::chrono::seconds(900)));
    auto notified = std::make_shared<std::promise<void>>();
    auto notification = notified->get_future();
    EXPECT_CALL(set_charging_profiles_callback_mock, Call()).Times(1).WillOnce([notified] { notified->set_value(); });
    smart_charging.on_connection_restored(std::chrono::seconds(900));
    ASSERT_EQ(notification.wait_for(std::chrono::seconds(4)), std::future_status::ready);

    const auto remaining = database_handler->get_charging_profiles_for_evse(DEFAULT_EVSE_ID);
    ASSERT_EQ(remaining.size(), 1);
    EXPECT_EQ(remaining.front().id, 1);

    ON_CALL(connectivity_manager, get_time_disconnected())
        .WillByDefault(testing::Return(std::chrono::steady_clock::time_point{}));
    const auto schedule = smart_charging.calculate_composite_schedule(
        ocpp::DateTime{"2024-01-17T18:00:00"}, ocpp::DateTime{"2024-01-18T06:00:00"}, DEFAULT_EVSE_ID,
        ChargingRateUnitEnum::W, false, false);
    ASSERT_EQ(schedule.chargingSchedulePeriod.size(), 1);
    EXPECT_EQ(schedule.chargingSchedulePeriod.front().stackLevel, 0);
    EXPECT_EQ(schedule.chargingSchedulePeriod.front().limit, 2000.0);
    EXPECT_FALSE(schedule.chargingSchedulePeriod.front().setpoint.has_value());
}

TEST_F(SmartChargingTestV21, OfflineDuration_ZeroExpiresWithinOneSecond) {
    auto profile = SmartChargingTestUtils::get_charging_profile_from_file(
        "offline_duration/invalid_after_offline_duration/TxProfile_invalid_after_offline_duration.json");
    profile.maxOfflineDuration = 0;
    database_handler->insert_or_update_charging_profile(DEFAULT_EVSE_ID, profile);

    EXPECT_CALL(connectivity_manager, get_time_disconnected()).WillRepeatedly(testing::Invoke([] {
        return std::chrono::steady_clock::now() - std::chrono::milliseconds(100);
    }));
    evse_manager->open_transaction(DEFAULT_EVSE_ID, TX_ID);
    EXPECT_CALL(set_charging_profiles_callback_mock, Call()).Times(0);
    EXPECT_TRUE(smart_charging.get_valid_profiles_for_evse(DEFAULT_EVSE_ID, {}).empty());
    EXPECT_TRUE(database_handler->get_charging_profiles_for_evse(DEFAULT_EVSE_ID).empty());
    testing::Mock::VerifyAndClearExpectations(&set_charging_profiles_callback_mock);

    database_handler->insert_or_update_charging_profile(DEFAULT_EVSE_ID, profile);
    auto notified = std::make_shared<std::promise<void>>();
    auto notification = notified->get_future();
    EXPECT_CALL(set_charging_profiles_callback_mock, Call()).Times(1).WillOnce([notified] { notified->set_value(); });
    smart_charging.on_connection_restored(std::chrono::milliseconds(100));
    ASSERT_EQ(notification.wait_for(std::chrono::seconds(4)), std::future_status::ready);
    EXPECT_TRUE(database_handler->get_charging_profiles_for_evse(DEFAULT_EVSE_ID).empty());
}

TEST_F(SmartChargingTestV21, OfflineDuration_ReconnectKeepsReusableProfile) {
    const auto profiles = SmartChargingTestUtils::get_charging_profiles_from_directory(
        BASE_JSON_PATH_V2 + "/offline_duration/valid_after_offline_duration/");
    for (const auto& profile : profiles) {
        database_handler->insert_or_update_charging_profile(DEFAULT_EVSE_ID, profile);
    }
    evse_manager->open_transaction(DEFAULT_EVSE_ID, TX_ID);

    ON_CALL(connectivity_manager, get_time_disconnected())
        .WillByDefault(testing::Return(std::chrono::steady_clock::now() - std::chrono::seconds(900)));
    const auto offline_schedule = smart_charging.calculate_composite_schedule(
        ocpp::DateTime{"2024-01-17T18:00:00"}, ocpp::DateTime{"2024-01-18T06:00:00"}, DEFAULT_EVSE_ID,
        ChargingRateUnitEnum::W, false, false);
    ASSERT_EQ(offline_schedule.chargingSchedulePeriod.size(), 1);
    EXPECT_EQ(offline_schedule.chargingSchedulePeriod.front().stackLevel, 1);

    auto notified = std::make_shared<std::promise<void>>();
    auto notification = notified->get_future();
    EXPECT_CALL(set_charging_profiles_callback_mock, Call()).Times(1).WillOnce([this, notified] {
        const auto schedule = smart_charging.calculate_composite_schedule(
            ocpp::DateTime{"2024-01-17T18:00:00"}, ocpp::DateTime{"2024-01-18T06:00:00"}, DEFAULT_EVSE_ID,
            ChargingRateUnitEnum::W, false, false);
        ASSERT_EQ(schedule.chargingSchedulePeriod.size(), 1);
        EXPECT_EQ(schedule.chargingSchedulePeriod.front().stackLevel, 2);
        notified->set_value();
    });
    smart_charging.on_connection_restored(std::chrono::seconds(900));
    ASSERT_EQ(notification.wait_for(std::chrono::seconds(4)), std::future_status::ready);
    EXPECT_EQ(database_handler->get_charging_profiles_for_evse(DEFAULT_EVSE_ID).size(), 2);

    // A later outage must not be mistaken for the outage already handled on reconnect.
    ON_CALL(connectivity_manager, get_time_disconnected())
        .WillByDefault(testing::Return(std::chrono::steady_clock::now() - std::chrono::seconds(800)));
    const auto schedule = smart_charging.calculate_composite_schedule(
        ocpp::DateTime{"2024-01-17T18:00:00"}, ocpp::DateTime{"2024-01-18T06:00:00"}, DEFAULT_EVSE_ID,
        ChargingRateUnitEnum::W, false, false);
    ASSERT_EQ(schedule.chargingSchedulePeriod.size(), 1);
    EXPECT_EQ(schedule.chargingSchedulePeriod.front().stackLevel, 1);
}

TEST_F(CompositeScheduleTestFixtureV21, OfflineDuration_ReconnectKeepsUnexpiredProfile) {
    const auto profile = SmartChargingTestUtils::get_charging_profile_from_file(
        "offline_duration/invalid_after_offline_duration/TxProfile_invalid_after_offline_duration.json");
    auto first = std::make_shared<std::promise<void>>();
    auto second = std::make_shared<std::promise<void>>();
    auto first_scan = first->get_future();
    auto second_scan = second->get_future();
    EXPECT_CALL(*database_handler, get_all_charging_profiles())
        .WillOnce([first, profile] {
            first->set_value();
            return std::vector<ChargingProfile>{profile};
        })
        .WillOnce([second, profile] {
            second->set_value();
            return std::vector<ChargingProfile>{profile};
        });
    EXPECT_CALL(*database_handler, clear_charging_profiles_matching_criteria(testing::_, testing::_)).Times(0);
    EXPECT_CALL(set_charging_profiles_callback_mock, Call()).Times(0);
    handler->on_connection_restored(std::chrono::seconds(300));
    ASSERT_EQ(first_scan.wait_for(std::chrono::seconds(4)), std::future_status::ready);
    handler->on_connection_restored(std::chrono::seconds(600));
    EXPECT_EQ(second_scan.wait_for(std::chrono::seconds(4)), std::future_status::ready);
    handler.reset();
}

TEST_F(SmartChargingTestV21, OfflineDuration_ReconnectDeletesAcrossEvsesOnce) {
    auto profile = SmartChargingTestUtils::get_charging_profile_from_file(
        "offline_duration/invalid_after_offline_duration/TxProfile_invalid_after_offline_duration.json");
    for (const auto evse_id : {0, 1, 2}) {
        profile.id = evse_id + 10;
        database_handler->insert_or_update_charging_profile(evse_id, profile);
    }

    auto notified = std::make_shared<std::promise<void>>();
    auto notification = notified->get_future();
    EXPECT_CALL(set_charging_profiles_callback_mock, Call()).Times(1).WillOnce([notified] { notified->set_value(); });
    smart_charging.on_connection_restored(std::chrono::seconds(900));
    ASSERT_EQ(notification.wait_for(std::chrono::seconds(4)), std::future_status::ready);
    EXPECT_TRUE(database_handler->get_all_charging_profiles().empty());
}

TEST_F(SmartChargingTestV21, OfflineDuration_OfflineDeletionDoesNotNotify) {
    auto profile = SmartChargingTestUtils::get_charging_profile_from_file(
        "offline_duration/invalid_after_offline_duration/TxProfile_invalid_after_offline_duration.json");
    database_handler->insert_or_update_charging_profile(DEFAULT_EVSE_ID, profile);
    EXPECT_CALL(connectivity_manager, get_time_disconnected())
        .WillRepeatedly(testing::Return(std::chrono::steady_clock::now() - std::chrono::seconds(900)));

    EXPECT_CALL(set_charging_profiles_callback_mock, Call()).Times(0);
    EXPECT_TRUE(smart_charging.get_valid_profiles_for_evse(DEFAULT_EVSE_ID, {}).empty());
    EXPECT_TRUE(database_handler->get_charging_profiles_for_evse(DEFAULT_EVSE_ID).empty());
    EXPECT_TRUE(smart_charging.get_valid_profiles_for_evse(DEFAULT_EVSE_ID, {}).empty());
}

TEST_F(CompositeScheduleTestFixtureV2, Q03_CentralSetpoint_PreservedInCompositeSchedule) {
    // Create a TxProfile with CentralSetpoint operationMode and a setpoint
    EnhancedChargingSchedulePeriod period{};
    period.startPeriod = 0;
    period.limit = 7000.0F;
    period.setpoint = 5000.0F;
    period.operationMode = OperationModeEnum::CentralSetpoint;
    period.numberPhases = 3;

    auto profile = create_charging_profile(
        DEFAULT_PROFILE_ID, ChargingProfilePurposeEnum::TxProfile,
        create_charge_schedule(ChargingRateUnitEnum::W, {period.get()}, ocpp::DateTime("2024-01-17T17:00:00")), TX_ID);

    ON_CALL(*database_handler, get_charging_profiles_for_evse(DEFAULT_EVSE_ID))
        .WillByDefault(testing::Return(std::vector<ChargingProfile>{profile}));

    evse_manager->open_transaction(DEFAULT_EVSE_ID, TX_ID);

    const DateTime start_time = ocpp::DateTime("2024-01-17T17:00:00");
    const DateTime end_time = ocpp::DateTime("2024-01-17T18:00:00");

    auto result = handler->calculate_composite_schedule(start_time, end_time, DEFAULT_EVSE_ID, ChargingRateUnitEnum::W,
                                                        false, false);

    ASSERT_FALSE(result.chargingSchedulePeriod.empty());
    EXPECT_EQ(result.chargingSchedulePeriod.front().operationMode, OperationModeEnum::CentralSetpoint);
    EXPECT_EQ(result.chargingSchedulePeriod.front().setpoint, 5000.0F);
}

TEST_F(CompositeScheduleTestFixtureV2, Q03_NoSetpoint_OperationModeIsNullopt) {
    // Create a TxProfile with only a limit (no setpoint, no operationMode)
    EnhancedChargingSchedulePeriod period{};
    period.startPeriod = 0;
    period.limit = 7000.0F;
    period.numberPhases = 3;

    auto profile = create_charging_profile(
        DEFAULT_PROFILE_ID, ChargingProfilePurposeEnum::TxProfile,
        create_charge_schedule(ChargingRateUnitEnum::W, {period.get()}, ocpp::DateTime("2024-01-17T17:00:00")), TX_ID);

    ON_CALL(*database_handler, get_charging_profiles_for_evse(DEFAULT_EVSE_ID))
        .WillByDefault(testing::Return(std::vector<ChargingProfile>{profile}));

    evse_manager->open_transaction(DEFAULT_EVSE_ID, TX_ID);

    const DateTime start_time = ocpp::DateTime("2024-01-17T17:00:00");
    const DateTime end_time = ocpp::DateTime("2024-01-17T18:00:00");

    auto result = handler->calculate_composite_schedule(start_time, end_time, DEFAULT_EVSE_ID, ChargingRateUnitEnum::W,
                                                        false, false);

    ASSERT_FALSE(result.chargingSchedulePeriod.empty());
    EXPECT_FALSE(result.chargingSchedulePeriod.front().operationMode.has_value());
}

TEST_F(CompositeScheduleTestFixtureV2, Q03_MultiplePeriodsWithDifferentOperationModes) {
    // Two periods: first ChargingOnly, second CentralSetpoint
    EnhancedChargingSchedulePeriod period1{};
    period1.startPeriod = 0;
    period1.limit = 7000.0F;
    period1.operationMode = OperationModeEnum::ChargingOnly;
    period1.numberPhases = 3;

    EnhancedChargingSchedulePeriod period2{};
    period2.startPeriod = 1800;
    period2.setpoint = 5000.0F;
    period2.limit = 7000.0F;
    period2.operationMode = OperationModeEnum::CentralSetpoint;
    period2.numberPhases = 3;

    auto profile =
        create_charging_profile(DEFAULT_PROFILE_ID, ChargingProfilePurposeEnum::TxProfile,
                                create_charge_schedule(ChargingRateUnitEnum::W, {period1.get(), period2.get()},
                                                       ocpp::DateTime("2024-01-17T17:00:00")),
                                TX_ID);

    ON_CALL(*database_handler, get_charging_profiles_for_evse(DEFAULT_EVSE_ID))
        .WillByDefault(testing::Return(std::vector<ChargingProfile>{profile}));

    evse_manager->open_transaction(DEFAULT_EVSE_ID, TX_ID);

    const DateTime start_time = ocpp::DateTime("2024-01-17T17:00:00");
    const DateTime end_time = ocpp::DateTime("2024-01-17T18:00:00");

    auto result = handler->calculate_composite_schedule(start_time, end_time, DEFAULT_EVSE_ID, ChargingRateUnitEnum::W,
                                                        false, false);

    ASSERT_GE(result.chargingSchedulePeriod.size(), 2);
    // nullopt operationMode is equivalent to ChargingOnly; effective_mode()
    // normalizes the comparison so an explicit ChargingOnly round-trips
    // through the pipeline.
    EXPECT_EQ(effective_mode(result.chargingSchedulePeriod[0].operationMode), OperationModeEnum::ChargingOnly);
    EXPECT_EQ(result.chargingSchedulePeriod[1].operationMode, OperationModeEnum::CentralSetpoint);
    EXPECT_EQ(result.chargingSchedulePeriod[1].setpoint, 5000.0F);
}

} // namespace ocpp::v2
