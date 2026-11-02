// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <vector>

#include <nlohmann/json.hpp>

#include <ocpp/v2/messages/ClearVariableMonitoring.hpp>
#include <ocpp/v2/messages/GetDisplayMessages.hpp>
#include <ocpp/v2/ocpp_types.hpp>

using nlohmann::json;

namespace ocpp {
namespace v2 {

TEST(V2MessageFromJsonTest, ClearVariableMonitoringRejectsOutOfRangeId) {
    const auto request = json::parse(R"({"id": [1, -2147483648]})").get<ClearVariableMonitoringRequest>();
    EXPECT_EQ(request.id, (std::vector<std::int32_t>{1, std::numeric_limits<std::int32_t>::min()}));

    EXPECT_THROW(json::parse(R"({"id": [1, 2147483648]})").get<ClearVariableMonitoringRequest>(), json::out_of_range);
    EXPECT_THROW(json::parse(R"({"id": [1, 1e30]})").get<ClearVariableMonitoringRequest>(), json::type_error);
}

TEST(V2MessageFromJsonTest, GetDisplayMessagesRejectsOutOfRangeId) {
    EXPECT_THROW(json::parse(R"({"requestId": 1, "id": [-2147483649]})").get<GetDisplayMessagesRequest>(),
                 json::out_of_range);
    EXPECT_THROW(json::parse(R"({"requestId": 2147483648})").get<GetDisplayMessagesRequest>(), json::out_of_range);
}

TEST(V2MessageFromJsonTest, ChargingSchedulePeriodRejectsOutOfRangeNumbers) {
    EXPECT_THROW(json::parse(R"({"startPeriod": 0, "limit": -1e39})").get<ChargingSchedulePeriod>(),
                 json::out_of_range);
    EXPECT_THROW(json::parse(R"({"startPeriod": 1e30})").get<ChargingSchedulePeriod>(), json::type_error);

    const auto period = json::parse(R"({"startPeriod": 60, "limit": 7.25})").get<ChargingSchedulePeriod>();
    EXPECT_EQ(period.startPeriod, 60);
    ASSERT_TRUE(period.limit.has_value());
    EXPECT_FLOAT_EQ(period.limit.value(), 7.25F);
}

} // namespace v2
} // namespace ocpp
