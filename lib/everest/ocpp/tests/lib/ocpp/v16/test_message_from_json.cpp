// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include <ocpp/v16/messages/ChangeAvailability.hpp>
#include <ocpp/v16/ocpp_types.hpp>

using nlohmann::json;

namespace ocpp {
namespace v16 {

TEST(V16MessageFromJsonTest, ChangeAvailabilityAcceptsInt32ConnectorId) {
    const auto request =
        json::parse(R"({"connectorId": 2147483647, "type": "Operative"})").get<ChangeAvailabilityRequest>();
    EXPECT_EQ(request.connectorId, 2147483647);
}

TEST(V16MessageFromJsonTest, ChangeAvailabilityRejectsOutOfRangeConnectorId) {
    EXPECT_THROW(json::parse(R"({"connectorId": 2147483648, "type": "Operative"})").get<ChangeAvailabilityRequest>(),
                 json::out_of_range);
    EXPECT_THROW(json::parse(R"({"connectorId": 99999999999999999999999, "type": "Operative"})")
                     .get<ChangeAvailabilityRequest>(),
                 json::type_error);
    EXPECT_THROW(json::parse(R"({"connectorId": 1.0, "type": "Operative"})").get<ChangeAvailabilityRequest>(),
                 json::type_error);
}

// connectorId 1 is accepted, the other two are rejected. The last one is beyond the uint64 range, nlohmann parses it
// as a double and converting that to an int32 is undefined behaviour.
TEST(V16MessageFromJsonTest, ChangeAvailabilityConnectorIdRange) {
    const auto request = json::parse(R"({"connectorId":1,"type":"Inoperative"})").get<ChangeAvailabilityRequest>();
    EXPECT_EQ(request.connectorId, 1);
    EXPECT_EQ(request.type, AvailabilityType::Inoperative);

    EXPECT_THROW(json::parse(R"({"connectorId":2147483648,"type":"Inoperative"})").get<ChangeAvailabilityRequest>(),
                 json::out_of_range);
    EXPECT_THROW(json::parse(R"({"connectorId":100000000000000000000000,"type":"Inoperative"})")
                     .get<ChangeAvailabilityRequest>(),
                 json::type_error);
}

TEST(V16MessageFromJsonTest, ChargingSchedulePeriodRejectsOutOfRangeNumbers) {
    EXPECT_THROW(json::parse(R"({"startPeriod": 0, "limit": 1e39})").get<ChargingSchedulePeriod>(), json::out_of_range);
    EXPECT_THROW(
        json::parse(R"({"startPeriod": 0, "limit": 16, "numberPhases": 4294967296})").get<ChargingSchedulePeriod>(),
        json::out_of_range);

    const auto period =
        json::parse(R"({"startPeriod": 0, "limit": 16.5, "numberPhases": 3})").get<ChargingSchedulePeriod>();
    EXPECT_FLOAT_EQ(period.limit, 16.5F);
    ASSERT_TRUE(period.numberPhases.has_value());
    EXPECT_EQ(period.numberPhases.value(), 3);
}

} // namespace v16
} // namespace ocpp
