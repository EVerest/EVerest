// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "everest_api_types/auth/codec.hpp"
#include "everest_api_types/utilities/codec.hpp"
#include <gtest/gtest.h>

using namespace everest::lib::API::V1_0::types::auth;
using everest::lib::API::deserialize;

TEST(auth, validation_result_enforces_the_schema) {
    ValidationResult result;
    EXPECT_TRUE(deserialize(R"({"authorization_status": "Accepted", "tariff_messages": [], "evse_ids": [1, 2],
                                "expiry_time": "2030-01-01T00:00:00Z", "reservation_id": 7})",
                            result));
    EXPECT_FALSE(
        deserialize(R"({"authorization_status": "Accepted", "tariff_messages": [], "evse_ids": [0]})", result));
    EXPECT_FALSE(
        deserialize(R"({"authorization_status": "Accepted", "tariff_messages": [], "evse_ids": [true]})", result));
    EXPECT_FALSE(
        deserialize(R"({"authorization_status": "Accepted", "tariff_messages": [], "evse_ids": [1.5]})", result));
    EXPECT_FALSE(
        deserialize(R"({"authorization_status": "Accepted", "tariff_messages": [], "reservation_id": "7"})", result));
}

TEST(auth, validation_result_update_needs_an_integer_connector_id) {
    ValidationResultUpdate update;
    const std::string result = R"({"authorization_status": "Accepted", "tariff_messages": []})";
    EXPECT_TRUE(deserialize(R"({"connector_id": 1, "validation_result": )" + result + "}", update));
    EXPECT_FALSE(deserialize(R"({"connector_id": "1,", "validation_result": )" + result + "}", update));
    EXPECT_FALSE(deserialize(R"({"connector_id": true, "validation_result": )" + result + "}", update));
}

TEST(auth, withdraw_authorization_request_must_be_an_object) {
    WithdrawAuthorizationRequest request;
    EXPECT_TRUE(deserialize(R"({})", request));
    EXPECT_TRUE(deserialize(R"({"evse_id": 1, "id_token": {"value": "TOKEN1", "type": "ISO14443"}})", request));
    for (const auto* payload : {"5", "null", "true", "[]", R"("x")", R"({"evse_id": true})", R"({"evse_id": "1"})",
                                R"({"evse_id": 1.5})", R"({"id_token": {"value": "X"}})"}) {
        WithdrawAuthorizationRequest rejected;
        EXPECT_FALSE(deserialize(payload, rejected)) << payload;
    }
}
