// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "everest_api_types/error_history/codec.hpp"
#include "everest_api_types/ocpp/codec.hpp"
#include "everest_api_types/utilities/codec.hpp"
#include "nlohmann/json.hpp"
#include <gtest/gtest.h>

using everest::lib::API::deserialize;
namespace error_history = everest::lib::API::V1_0::types::error_history;
namespace ocpp = everest::lib::API::V1_0::types::ocpp;

namespace {

std::string timeperiod_filter(const nlohmann::json& timestamp_to) {
    return nlohmann::json{{"timestamp_from", "2026-01-02T03:04:05Z"}, {"timestamp_to", timestamp_to}}.dump();
}

} // namespace

TEST(date_time, date_times_are_read_as_rfc3339_in_utc) {
    for (const auto* timestamp :
         {"2026-01-02T05:04:05.678+02:00", "2026-01-02T03:04:05.678Z", "2026-01-02t03:04:05.678000z"}) {
        error_history::TimeperiodFilter filter;
        ASSERT_TRUE(deserialize(timeperiod_filter(timestamp), filter)) << timestamp;
        EXPECT_EQ(filter.timestamp_to, "2026-01-02T03:04:05.678Z") << timestamp;
    }

    ocpp::SecurityEvent event;
    ASSERT_TRUE(deserialize(R"({"type": "FirmwareUpdated"})", event));
    EXPECT_FALSE(event.timestamp.has_value());
}

TEST(date_time, payloads_with_invalid_date_times_are_rejected) {
    for (const auto& timestamp :
         {nlohmann::json("2026-02-30T03:04:05Z"), nlohmann::json("9999-12-31T23:59:59Z"),
          nlohmann::json("2026-01-02T03:04:05.678"), nlohmann::json("2026-01-02T03:04:05,678Z"),
          nlohmann::json("2026-01-02T05:04:05.678+0200"), nlohmann::json("2026-01-02T05:04:05.678+02"),
          nlohmann::json("garbage"), nlohmann::json(""), nlohmann::json(1767323045)}) {
        error_history::TimeperiodFilter filter;
        EXPECT_FALSE(deserialize(timeperiod_filter(timestamp), filter)) << timestamp;
    }

    ocpp::SecurityEvent event;
    EXPECT_FALSE(deserialize(R"({"type": "FirmwareUpdated", "timestamp": "2026-01-02 03:04:05Z"})", event));
}
