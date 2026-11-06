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

TEST(evse_manager, session_info_without_reservation_omits_it) {
    SessionInfo info{};
    info.state = EvseStateEnum::Unplugged;
    info.reserved = true;

    EXPECT_FALSE(nlohmann::json::parse(serialize(info)).contains("reservation"));

    SessionInfo result;
    ASSERT_TRUE(everest::lib::API::deserialize(std::string(session_info_without_reserved), result));
    EXPECT_FALSE(result.reservation.has_value());
}

TEST(evse_manager, session_info_reservation_round_trips) {
    SessionInfo info{};
    info.state = EvseStateEnum::Unplugged;
    info.reserved = true;
    info.reservation = Reservation{7, "RFID1", "PARENT1", "2026-10-06T09:00:00.000Z"};

    const auto payload = nlohmann::json::parse(serialize(info));
    EXPECT_EQ(payload.at("reservation").at("id"), 7);
    EXPECT_EQ(payload.at("reservation").at("id_token"), "RFID1");
    EXPECT_EQ(payload.at("reservation").at("parent_id_token"), "PARENT1");
    EXPECT_EQ(payload.at("reservation").at("expiry_time"), "2026-10-06T09:00:00.000Z");

    SessionInfo result;
    ASSERT_TRUE(everest::lib::API::deserialize(payload.dump(), result));
    ASSERT_TRUE(result.reservation.has_value());
    EXPECT_EQ(result.reservation->id, 7);
    EXPECT_EQ(result.reservation->id_token, "RFID1");
    EXPECT_EQ(result.reservation->parent_id_token, "PARENT1");
    EXPECT_EQ(result.reservation->expiry_time, "2026-10-06T09:00:00.000Z");
}

TEST(evse_manager, reservation_without_parent_id_token_omits_it) {
    const Reservation reservation{7, "RFID1", std::nullopt, "2026-10-06T09:00:00.000Z"};

    const auto payload = nlohmann::json::parse(serialize(reservation));
    EXPECT_FALSE(payload.contains("parent_id_token"));

    Reservation result;
    ASSERT_TRUE(everest::lib::API::deserialize(payload.dump(), result));
    EXPECT_FALSE(result.parent_id_token.has_value());
}

TEST(evse_manager, session_event_reservation_round_trips) {
    SessionEvent event{};
    event.uuid = "";
    event.timestamp = "2026-10-06T08:00:00.000Z";
    event.event = SessionEventEnum::ReservationStart;
    event.reservation = Reservation{7, "RFID1", std::nullopt, "2026-10-06T09:00:00.000Z"};

    const auto payload = nlohmann::json::parse(serialize(event));
    EXPECT_EQ(payload.at("reservation").at("id"), 7);
    EXPECT_EQ(payload.at("reservation").at("id_token"), "RFID1");

    SessionEvent result;
    ASSERT_TRUE(everest::lib::API::deserialize(payload.dump(), result));
    ASSERT_TRUE(result.reservation.has_value());
    EXPECT_EQ(result.reservation->id, 7);
    EXPECT_EQ(result.reservation->id_token, "RFID1");
}
