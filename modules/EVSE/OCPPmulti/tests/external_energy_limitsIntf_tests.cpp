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

} // namespace
