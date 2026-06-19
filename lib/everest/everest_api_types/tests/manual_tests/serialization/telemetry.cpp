// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "everest_api_types/telemetry/codec.hpp"
#include "nlohmann/json.hpp"
#include <gtest/gtest.h>

using namespace everest::lib::API::V1_0::types::telemetry;

TEST(telemetry, enum_serialization_uses_strings) {
    EXPECT_EQ(serialize(V2gCommunicationState::StateId14), R"("StateId14")");
    EXPECT_EQ(serialize(V2gMessageState::PowerDelivery), R"("PowerDelivery")");
    EXPECT_EQ(serialize(V2gServerStatus::Active), R"("Active")");
    EXPECT_EQ(serialize(V2gEvErrorCode::FAILED_EVRESSMalfunction), R"("FAILED_EVRESSMalfunction")");

    V2gTransport transport;
    transport.comm_state = V2gCommunicationState::StateId14;
    transport.message_state = V2gMessageState::PowerDelivery;
    transport.udp_server_status = V2gServerStatus::Active;

    auto transport_json = nlohmann::json::parse(serialize(transport));
    EXPECT_EQ(transport_json.at("comm_state"), "StateId14");
    EXPECT_EQ(transport_json.at("message_state"), "PowerDelivery");
    EXPECT_EQ(transport_json.at("udp_server_status"), "Active");

    V2gEvElectrical ev_electrical;
    ev_electrical.error_code = V2gEvErrorCode::FAILED_EVRESSMalfunction;

    auto ev_electrical_json = nlohmann::json::parse(serialize(ev_electrical));
    EXPECT_EQ(ev_electrical_json.at("error_code"), "FAILED_EVRESSMalfunction");
}

TEST(telemetry, enum_deserialization_uses_strings) {
    EXPECT_EQ(deserialize<V2gCommunicationState>(R"("StateId14")"), V2gCommunicationState::StateId14);
    EXPECT_EQ(deserialize<V2gMessageState>(R"("PowerDelivery")"), V2gMessageState::PowerDelivery);
    EXPECT_EQ(deserialize<V2gServerStatus>(R"("Active")"), V2gServerStatus::Active);
    EXPECT_EQ(deserialize<V2gEvErrorCode>(R"("FAILED_EVRESSMalfunction")"), V2gEvErrorCode::FAILED_EVRESSMalfunction);
}
