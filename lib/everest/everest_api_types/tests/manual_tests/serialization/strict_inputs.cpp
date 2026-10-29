// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "everest_api_types/evse_manager/codec.hpp"
#include "everest_api_types/money/codec.hpp"
#include "everest_api_types/ocpp/codec.hpp"
#include "everest_api_types/system/codec.hpp"
#include "everest_api_types/utilities/codec.hpp"
#include <gtest/gtest.h>

using everest::lib::API::deserialize;

TEST(strict_inputs, ev_info_numbers_and_ranges) {
    everest::lib::API::V1_0::types::evse_manager::EVInfo info;
    EXPECT_TRUE(deserialize(R"({"soc": 67, "departure_time": "2026-10-28T18:00:00Z"})", info));
    EXPECT_FALSE(deserialize(R"({"soc": true})", info));
    EXPECT_FALSE(deserialize(R"({"soc": 101})", info));
    EXPECT_FALSE(deserialize(R"({"soc": -1})", info));
}

TEST(strict_inputs, money_and_status_integers) {
    everest::lib::API::V1_0::types::money::Currency currency;
    EXPECT_TRUE(deserialize(R"({"code": "EUR", "decimals": 2})", currency));
    EXPECT_FALSE(deserialize(R"({"code": "EUR", "decimals": -1})", currency));
    EXPECT_FALSE(deserialize(R"({"code": "EUR", "decimals": 1.5})", currency));
    everest::lib::API::V1_0::types::system::LogStatus status;
    EXPECT_TRUE(deserialize(R"({"log_status": "Uploaded", "request_id": 3})", status));
    EXPECT_FALSE(deserialize(R"({"log_status": "Uploaded", "request_id": true})", status));
}

TEST(strict_inputs, ocpp_evse_id) {
    everest::lib::API::V1_0::types::ocpp::ChangeAvailabilityRequest request;
    EXPECT_TRUE(deserialize(R"({"operational_status": "Inoperative", "evse": {"id": 1}})", request));
    EXPECT_FALSE(deserialize(R"({"operational_status": "Inoperative", "evse": {"id": true}})", request));
    EXPECT_FALSE(deserialize(R"({"operational_status": "Inoperative", "evse": {"id": 0}})", request));
}
