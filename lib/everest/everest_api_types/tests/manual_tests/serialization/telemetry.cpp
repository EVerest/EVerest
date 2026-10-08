// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "everest_api_types/telemetry/codec.hpp"
#include "nlohmann/json.hpp"
#include <gtest/gtest.h>

using namespace everest::lib::API::V1_0::types::telemetry;

TEST(telemetry, enum_serialization_uses_strings) {
    EXPECT_EQ(serialize(V2gDin70121CommunicationState::WaitForSessionStop), R"("WaitForSessionStop")");
    EXPECT_EQ(serialize(V2gIso15118AcCommunicationState::WaitForChargingStatusPowerDelivery),
              R"("WaitForChargingStatusPowerDelivery")");
    EXPECT_EQ(serialize(V2gIso15118DcCommunicationState::WaitForCurrentDemandPowerDelivery),
              R"("WaitForCurrentDemandPowerDelivery")");
    EXPECT_EQ(serialize(V2gMessageState::PowerDelivery), R"("PowerDelivery")");
    EXPECT_EQ(serialize(V2gServerStatus::Active), R"("Active")");
    EXPECT_EQ(serialize(V2gEvErrorCode::FAILED_EVRESSMalfunction), R"("FAILED_EVRESSMalfunction")");

    V2gTransport transport;
    transport.comm_state.din70121 = V2gDin70121CommunicationState::WaitForTerminatedSession;
    transport.message_state = V2gMessageState::PowerDelivery;
    transport.udp_server_status = V2gServerStatus::Active;

    auto transport_json = nlohmann::json::parse(serialize(transport));
    EXPECT_EQ(transport_json.at("comm_state"), nlohmann::json::parse(R"({"din70121":"WaitForTerminatedSession"})"));
    EXPECT_EQ(transport_json.at("message_state"), "PowerDelivery");
    EXPECT_EQ(transport_json.at("udp_server_status"), "Active");

    transport.comm_state = {};
    transport.comm_state.iso15118_ac = V2gIso15118AcCommunicationState::WaitForSessionSetup;
    transport_json = nlohmann::json::parse(serialize(transport));
    EXPECT_TRUE(transport_json.at("comm_state").contains("iso15118_ac"));
    EXPECT_FALSE(transport_json.at("comm_state").contains("din70121"));
    EXPECT_FALSE(transport_json.at("comm_state").contains("iso15118_dc"));

    V2gEvElectrical ev_electrical;
    ev_electrical.error_code = V2gEvErrorCode::FAILED_EVRESSMalfunction;

    auto ev_electrical_json = nlohmann::json::parse(serialize(ev_electrical));
    EXPECT_EQ(ev_electrical_json.at("error_code"), "FAILED_EVRESSMalfunction");
}

TEST(telemetry, enum_deserialization_uses_strings) {
    EXPECT_EQ(deserialize<V2gDin70121CommunicationState>(R"("WaitForSessionStop")"),
              V2gDin70121CommunicationState::WaitForSessionStop);
    EXPECT_EQ(
        deserialize<V2gIso15118AcCommunicationState>(R"("WaitForPaymentDetailsCertificateInstallCertificateUpdate")"),
        V2gIso15118AcCommunicationState::WaitForPaymentDetailsCertificateInstallCertificateUpdate);
    EXPECT_EQ(deserialize<V2gIso15118DcCommunicationState>(R"("WaitForCurrentDemand")"),
              V2gIso15118DcCommunicationState::WaitForCurrentDemand);
    EXPECT_EQ(deserialize<V2gMessageState>(R"("PowerDelivery")"), V2gMessageState::PowerDelivery);
    EXPECT_EQ(deserialize<V2gServerStatus>(R"("Active")"), V2gServerStatus::Active);
    EXPECT_EQ(deserialize<V2gEvErrorCode>(R"("FAILED_EVRESSMalfunction")"), V2gEvErrorCode::FAILED_EVRESSMalfunction);

    auto parsed_din = deserialize<V2gCommunicationState>(R"({"din70121":"WaitForTerminatedSession"})");
    EXPECT_TRUE(parsed_din.din70121.has_value());
    EXPECT_FALSE(parsed_din.iso15118_ac.has_value());
    EXPECT_FALSE(parsed_din.iso15118_dc.has_value());
    EXPECT_EQ(parsed_din.din70121.value(), V2gDin70121CommunicationState::WaitForTerminatedSession);
}
