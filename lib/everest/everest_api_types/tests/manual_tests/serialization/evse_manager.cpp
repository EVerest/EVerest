// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "everest_api_types/evse_manager/codec.hpp"
#include "everest_api_types/utilities/codec.hpp"
#include "nlohmann/json.hpp"
#include <gtest/gtest.h>

using namespace everest::lib::API::V1_0::types::evse_manager;

namespace {

constexpr std::string_view session_info_without_reserved = R"({
    "state": "Unplugged",
    "charged_energy_wh": 0,
    "discharged_energy_wh": 0,
    "latest_total_w": 0,
    "session_duration_s": 0,
    "timestamp": "2026-10-06T08:00:00.000Z"
})";

} // namespace

TEST(evse_manager, session_info_without_reserved_defaults_to_false) {
    SessionInfo result;
    result.reserved = true;
    ASSERT_TRUE(everest::lib::API::deserialize(std::string(session_info_without_reserved), result));

    EXPECT_FALSE(result.reserved);
    EXPECT_EQ(result.state, EvseStateEnum::Unplugged);
}

TEST(evse_manager, session_info_with_reserved_is_read) {
    auto payload = nlohmann::json::parse(session_info_without_reserved);
    payload["reserved"] = true;

    SessionInfo result;
    ASSERT_TRUE(everest::lib::API::deserialize(payload.dump(), result));

    EXPECT_TRUE(result.reserved);
}

TEST(evse_manager, session_info_serialization_emits_reserved) {
    SessionInfo info{};
    info.state = EvseStateEnum::Unplugged;

    EXPECT_EQ(nlohmann::json::parse(serialize(info)).at("reserved"), false);

    info.reserved = true;
    EXPECT_EQ(nlohmann::json::parse(serialize(info)).at("reserved"), true);
}
