// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

// cmds:
//   set_external_limits:
//
// vars:
//   capabilities: <not used>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <generic_ocpp.hpp>

#include "stubs/generic_ocpp_stub.hpp"

namespace {
using namespace stubs;

using ocpp::v2::ChargingRateUnitEnum;
using ocpp::v2::EnhancedChargingSchedulePeriod;
using ocpp::v2::EnhancedCompositeSchedule;

constexpr auto EXT_LIMIT_SOURCE = "ocpp/OCPP_set_external_limits";

EnhancedCompositeSchedule make_schedule(std::int32_t evse_id, ChargingRateUnitEnum unit) {
    EnhancedCompositeSchedule schedule;
    schedule.evseId = evse_id;
    schedule.duration = 1500;
    schedule.scheduleStart = ocpp::DateTime{"2026-06-05T13:37:36.409Z"};
    schedule.chargingRateUnit = unit;
    return schedule;
}

EnhancedChargingSchedulePeriod make_period(std::int32_t start_period, std::optional<float> limit,
                                           std::optional<float> discharge_limit) {
    EnhancedChargingSchedulePeriod period;
    period.startPeriod = start_period;
    period.limit = limit;
    period.dischargeLimit = discharge_limit;
    period.stackLevel = 8;
    return period;
}

json with_source(json value) {
    return {{"source", EXT_LIMIT_SOURCE}, {"value", std::move(value)}};
}

json limits_entry(json limits_to_leaves, const json& timestamp) {
    return {{"limits_to_leaves", std::move(limits_to_leaves)},
            {"limits_to_root", json::object()},
            {"timestamp", timestamp}};
}

// timestamps are taken at call time, the export entry of a period must carry the import entry's timestamp
json import_timestamp(const json& received, std::size_t index) {
    return received["value"]["schedule_import"][index]["timestamp"];
}

TEST_F(GenericOcppProvidesTester, callSetExternalLimits) {
    // call_set_external_limits() used in set_external_limits()

    using ocpp::DateTime;
    using ocpp::v2::ChargingRateUnitEnum;
    using ocpp::v2::EnhancedChargingSchedulePeriod;
    using ocpp::v2::EnhancedCompositeSchedule;

    std::vector<json> received;
    interfaces->subscribe_var("external_energy_limits", "call_set_external_limits",
                              [&received](const auto&, const auto&, const auto& data) { received.push_back(data); });

    std::vector<EnhancedCompositeSchedule> composite_schedules;
    EnhancedCompositeSchedule schedule;
    schedule.evseId = 1;
    schedule.duration = 1500;
    schedule.scheduleStart = DateTime{"2026-06-05T13:37:36.409Z"};
    schedule.chargingRateUnit = ChargingRateUnitEnum::A;

    EnhancedChargingSchedulePeriod period;
    period.startPeriod = 0;
    period.limit = 16.;
    // std::optional<float> limit_L2;
    // std::optional<float> limit_L3;
    // std::optional<std::int32_t> numberPhases;
    // std::optional<std::int32_t> phaseToUse;
    // std::optional<float> dischargeLimit;
    // std::optional<float> dischargeLimit_L2;
    // std::optional<float> dischargeLimit_L3;
    // std::optional<float> setpoint;
    // std::optional<float> setpoint_L2;
    // std::optional<float> setpoint_L3;
    // std::optional<float> setpointReactive;
    // std::optional<float> setpointReactive_L2;
    // std::optional<float> setpointReactive_L3;
    // std::optional<bool> preconditioningRequest;
    // std::optional<bool> evseSleep;
    // std::optional<float> v2xBaseline;
    // std::optional<OperationModeEnum> operationMode;
    // std::optional<std::vector<V2XFreqWattPoint>> v2xFreqWattCurve;
    // std::optional<std::vector<V2XSignalWattPoint>> v2xSignalWattCurve;
    // std::optional<CustomData> customData;
    period.stackLevel = 8;

    schedule.chargingSchedulePeriod.push_back(period);
    period.startPeriod = 120;
    period.limit = 24.;
    schedule.chargingSchedulePeriod.push_back(period);

    composite_schedules.push_back(schedule);

    ocpp->set_external_limits(composite_schedules);

    ASSERT_EQ(received.size(), 1);

    // timestamps make the comparison tricky

    auto expected = R"({"value":{"schedule_export":[],"schedule_import":[
        {"limits_to_leaves":{"ac_max_current_A":{"source":"ocpp/OCPP_set_external_limits","value":16.0}},
        "limits_to_root":{},"timestamp":"2026-06-08T13:40:12.226Z"},
        {"limits_to_leaves":{"ac_max_current_A":{"source":"ocpp/OCPP_set_external_limits","value":24.0}},
        "limits_to_root":{},"timestamp":"2026-06-08T13:42:12.226Z"}],"schedule_setpoints":[]}})"_json;

    // {
    //   "value": {
    //     "schedule_export": [],
    //     "schedule_import": [
    //       {
    //         "limits_to_leaves": {
    //           "ac_max_current_A": {
    //             "source": "ocpp/OCPP_set_external_limits",
    //             "value": 16
    //           }
    //         },
    //         "limits_to_root": {},
    //         "timestamp": "2026-06-08T13:42:41.283Z"
    //       },
    //       {
    //         "limits_to_leaves": {
    //           "ac_max_current_A": {
    //             "source": "ocpp/OCPP_set_external_limits",
    //             "value": 24
    //           }
    //         },
    //         "limits_to_root": {},
    //         "timestamp": "2026-06-08T13:44:41.283Z"
    //       }
    //     ],
    //     "schedule_setpoints": []
    //   }
    // }

    expected["value"]["schedule_import"][0]["timestamp"] = received[0]["value"]["schedule_import"][0]["timestamp"];
    expected["value"]["schedule_import"][1]["timestamp"] = received[0]["value"]["schedule_import"][1]["timestamp"];

    EXPECT_EQ(received[0], expected);
}

TEST_F(GenericOcppProvidesTester, setExternalLimitsDischargeAmps) {
    std::vector<json> received;
    interfaces->subscribe_var("external_energy_limits", "call_set_external_limits",
                              [&received](const auto&, const auto&, const auto& data) { received.push_back(data); });

    auto schedule = make_schedule(1, ChargingRateUnitEnum::A);
    auto period = make_period(0, 0.F, -19.27855F);
    period.setpoint = -19.27855F;
    period.numberPhases = 3;
    schedule.chargingSchedulePeriod.push_back(period);

    ocpp->set_external_limits({schedule});

    ASSERT_EQ(received.size(), 1);
    const auto& value = received[0]["value"];
    const auto timestamp = import_timestamp(received[0], 0);

    const json expected_import = json::array(
        {limits_entry({{"ac_max_current_A", with_source(0.F)}, {"ac_max_phase_count", with_source(3)}}, timestamp)});
    const json expected_export = json::array({limits_entry(
        {{"ac_max_current_A", with_source(19.27855F)}, {"ac_max_phase_count", with_source(3)}}, timestamp)});
    EXPECT_EQ(value["schedule_import"], expected_import);
    EXPECT_EQ(value["schedule_export"], expected_export);
    ASSERT_EQ(value["schedule_setpoints"].size(), 1);
    EXPECT_EQ(value["schedule_setpoints"][0]["setpoint"]["ac_current_A"], json(-19.27855F));
    EXPECT_EQ(value["schedule_setpoints"][0]["timestamp"], timestamp);
}

TEST_F(GenericOcppProvidesTester, setExternalLimitsDischargeWatts) {
    std::vector<json> received;
    interfaces->subscribe_var("external_energy_limits", "call_set_external_limits",
                              [&received](const auto&, const auto&, const auto& data) { received.push_back(data); });

    auto schedule = make_schedule(1, ChargingRateUnitEnum::W);
    auto period = make_period(0, 0.F, -8000.F);
    period.setpoint = -8000.F;
    schedule.chargingSchedulePeriod.push_back(period);

    ocpp->set_external_limits({schedule});

    ASSERT_EQ(received.size(), 1);
    const auto& value = received[0]["value"];
    const auto timestamp = import_timestamp(received[0], 0);

    EXPECT_EQ(value["schedule_import"], json::array({limits_entry({{"total_power_W", with_source(0.F)}}, timestamp)}));
    EXPECT_EQ(value["schedule_export"],
              json::array({limits_entry({{"total_power_W", with_source(8000.F)}}, timestamp)}));
    ASSERT_EQ(value["schedule_setpoints"].size(), 1);
    EXPECT_EQ(value["schedule_setpoints"][0]["setpoint"]["total_power_W"], json(-8000.F));
}

TEST_F(GenericOcppProvidesTester, setExternalLimitsDischargeZero) {
    std::vector<json> received;
    interfaces->subscribe_var("external_energy_limits", "call_set_external_limits",
                              [&received](const auto&, const auto&, const auto& data) { received.push_back(data); });

    auto with_zero = make_schedule(1, ChargingRateUnitEnum::W);
    with_zero.chargingSchedulePeriod.push_back(make_period(0, 11000.F, 0.F));
    auto without = make_schedule(1, ChargingRateUnitEnum::W);
    without.chargingSchedulePeriod.push_back(make_period(0, 11000.F, std::nullopt));

    ocpp->set_external_limits({with_zero});
    ocpp->set_external_limits({without});

    ASSERT_EQ(received.size(), 2);
    EXPECT_EQ(received[0]["value"]["schedule_export"],
              json::array({limits_entry({{"total_power_W", with_source(0.F)}}, import_timestamp(received[0], 0))}));
    EXPECT_EQ(received[1]["value"]["schedule_export"], json::array());
}

TEST_F(GenericOcppProvidesTester, setExternalLimitsDischargePeriods) {
    std::vector<json> received;
    interfaces->subscribe_var("external_energy_limits", "call_set_external_limits",
                              [&received](const auto&, const auto&, const auto& data) { received.push_back(data); });

    auto schedule = make_schedule(1, ChargingRateUnitEnum::A);
    schedule.chargingSchedulePeriod.push_back(make_period(0, 16.F, -10.F));
    schedule.chargingSchedulePeriod.push_back(make_period(60, 16.F, std::nullopt));
    schedule.chargingSchedulePeriod.push_back(make_period(120, 16.F, -6.F));

    ocpp->set_external_limits({schedule});

    ASSERT_EQ(received.size(), 1);
    const json expected_export =
        json::array({limits_entry({{"ac_max_current_A", with_source(10.F)}}, import_timestamp(received[0], 0)),
                     limits_entry(json::object(), import_timestamp(received[0], 1)),
                     limits_entry({{"ac_max_current_A", with_source(6.F)}}, import_timestamp(received[0], 2))});
    EXPECT_EQ(received[0]["value"]["schedule_export"], expected_export);
}

TEST_F(GenericOcppProvidesTester, setExternalLimitsDischargeNotPersisted) {
    std::vector<json> received;
    interfaces->subscribe_var("external_energy_limits", "call_set_external_limits",
                              [&received](const auto&, const auto&, const auto& data) { received.push_back(data); });

    auto discharging = make_schedule(1, ChargingRateUnitEnum::A);
    discharging.chargingSchedulePeriod.push_back(make_period(0, 0.F, -10.F));
    auto charging = make_schedule(1, ChargingRateUnitEnum::A);
    charging.chargingSchedulePeriod.push_back(make_period(0, 16.F, std::nullopt));

    ocpp->set_external_limits({discharging});
    ocpp->set_external_limits({charging});

    ASSERT_EQ(received.size(), 2);
    EXPECT_EQ(received[0]["value"]["schedule_export"].size(), 1);
    EXPECT_EQ(received[1]["value"]["schedule_export"], json::array());
}

TEST_F(GenericOcppProvidesTester, setExternalLimitsDischargePhases) {
    std::vector<json> received;
    interfaces->subscribe_var("external_energy_limits", "call_set_external_limits",
                              [&received](const auto&, const auto&, const auto& data) { received.push_back(data); });

    auto schedule = make_schedule(1, ChargingRateUnitEnum::A);
    auto period = make_period(0, 16.F, -10.F);
    period.dischargeLimit_L2 = -4.F;
    period.dischargeLimit_L3 = -2.F;
    period.numberPhases = 3;
    schedule.chargingSchedulePeriod.push_back(period);

    ocpp->set_external_limits({schedule});

    ASSERT_EQ(received.size(), 1);
    const json expected_export =
        json::array({limits_entry({{"ac_max_current_A", with_source(10.F)}, {"ac_max_phase_count", with_source(3)}},
                                  import_timestamp(received[0], 0))});
    EXPECT_EQ(received[0]["value"]["schedule_export"], expected_export);
}

TEST_F(GenericOcppProvidesTester, setExternalLimitsDischargeRouting) {
    interfaces->add_evse_energy_sink("energy_node_2", 2);

    std::size_t received_all{0};
    std::vector<json> received_1;
    std::vector<json> received_2;
    interfaces->subscribe_var("external_energy_limits", "call_set_external_limits",
                              [&received_all](const auto&, const auto&, const auto&) { ++received_all; });
    interfaces->subscribe_var(
        "external_energy_limits", "call_set_external_limits", 0,
        [&received_1](const auto&, const auto&, const auto& data) { received_1.push_back(data); });
    interfaces->subscribe_var(
        "external_energy_limits", "call_set_external_limits", 1,
        [&received_2](const auto&, const auto&, const auto& data) { received_2.push_back(data); });

    auto evse_1 = make_schedule(1, ChargingRateUnitEnum::W);
    evse_1.chargingSchedulePeriod.push_back(make_period(0, 0.F, -8000.F));
    auto evse_2 = make_schedule(2, ChargingRateUnitEnum::W);
    evse_2.chargingSchedulePeriod.push_back(make_period(0, 0.F, -4000.F));
    auto evse_3 = make_schedule(3, ChargingRateUnitEnum::W);
    evse_3.chargingSchedulePeriod.push_back(make_period(0, 0.F, -2000.F));

    ocpp->set_external_limits({evse_1, evse_2, evse_3});

    EXPECT_EQ(received_all, 2);
    ASSERT_EQ(received_1.size(), 1);
    ASSERT_EQ(received_2.size(), 1);
    EXPECT_EQ(
        received_1[0]["value"]["schedule_export"],
        json::array({limits_entry({{"total_power_W", with_source(8000.F)}}, import_timestamp(received_1[0], 0))}));
    EXPECT_EQ(
        received_2[0]["value"]["schedule_export"],
        json::array({limits_entry({{"total_power_W", with_source(4000.F)}}, import_timestamp(received_2[0], 0))}));
}

TEST_F(GenericOcppProvidesTester, setExternalLimitsChargingOnly) {
    std::vector<json> received;
    interfaces->subscribe_var("external_energy_limits", "call_set_external_limits",
                              [&received](const auto&, const auto&, const auto& data) { received.push_back(data); });

    auto schedule = make_schedule(1, ChargingRateUnitEnum::A);
    auto period = make_period(0, 16.F, std::nullopt);
    period.setpoint = 10.F;
    schedule.chargingSchedulePeriod.push_back(period);

    ocpp->set_external_limits({schedule});

    ASSERT_EQ(received.size(), 1);
    const auto& value = received[0]["value"];
    EXPECT_EQ(value["schedule_export"], json::array());
    EXPECT_EQ(value["schedule_import"],
              json::array({limits_entry({{"ac_max_current_A", with_source(16.F)}}, import_timestamp(received[0], 0))}));
    ASSERT_EQ(value["schedule_setpoints"].size(), 1);
    EXPECT_EQ(value["schedule_setpoints"][0]["setpoint"]["ac_current_A"], json(10.F));
}

} // namespace
