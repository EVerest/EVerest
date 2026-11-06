// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

/// \file test_charge_point_malformed_call.cpp
/// \brief Tests of CALL handling in ChargePointImpl for payloads that do not fit the generated types.

#include "charge_point_test_base.hpp"

#include <string>
#include <utility>
#include <vector>

namespace ocpp {
namespace v16 {

namespace {
constexpr std::int32_t CONNECTOR = 1;
} // namespace

class ChargePointMalformedCallTest : public ChargePointTestBase {
protected:
    std::unique_ptr<ChargePointImpl> make_booted_charge_point() {
        auto charge_point = make_charge_point();
        charge_point->start({{0, ChargePointStatus::Available}, {CONNECTOR, ChargePointStatus::Available}},
                            BootReasonEnum::PowerUp, {});
        charge_point->on_websocket_connected(0, ocpp::v2::NetworkConnectionProfile{}, ocpp::OcppProtocolVersion::v16);
        EXPECT_TRUE(wait_for_action_count("StatusNotification", 2, WAIT_TIMEOUT))
            << "boot handshake did not produce the initial StatusNotification.req";
        EXPECT_TRUE(wait_for_all_answered());
        return charge_point;
    }

    /// \brief Send a ChangeAvailability.req with \p payload and return the charge point's answer
    std::optional<nlohmann::json> change_availability(const std::string& message_id, const std::string& payload) {
        this->to_charge_point(R"([2, ")" + message_id + R"(", "ChangeAvailability", )" + payload + "]");
        return wait_for_answer(message_id);
    }
};

// Invalid payloads: an integer beyond the int32 range, one beyond the uint64 range, which nlohmann parses as a double,
// and a float.
TEST_F(ChargePointMalformedCallTest, OutOfRangeConnectorIdIsAnsweredWithFormationViolation) {
    auto charge_point = make_booted_charge_point();

    for (const auto& [message_id, payload] : std::vector<std::pair<std::string, std::string>>{
             {"beyond-int32", R"({"connectorId":2147483648,"type":"Inoperative"})"},
             {"beyond-uint64", R"({"connectorId":100000000000000000000000,"type":"Inoperative"})"},
             {"float", R"({"connectorId":1.0,"type":"Inoperative"})"},
         }) {
        const auto answer = change_availability(message_id, payload);
        ASSERT_TRUE(answer.has_value()) << "no answer to " << payload;
        EXPECT_EQ(answer->at(0), 4) << payload;
        EXPECT_EQ(answer->at(2), "FormationViolation") << payload;
    }

    const auto answer = change_availability("in-range", R"({"connectorId":1,"type":"Operative"})");
    ASSERT_TRUE(answer.has_value());
    EXPECT_EQ(answer->at(0), 3);
    EXPECT_EQ(answer->at(2).at("status"), "Accepted");

    EXPECT_TRUE(wait_for_all_answered());
    charge_point->stop();
}

} // namespace v16
} // namespace ocpp
