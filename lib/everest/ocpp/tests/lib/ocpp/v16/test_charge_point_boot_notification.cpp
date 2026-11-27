// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

/// \file test_charge_point_boot_notification.cpp
/// \brief Tests of the BootNotification content built by ChargePointImpl.

#include "charge_point_test_base.hpp"

namespace ocpp {
namespace v16 {

class ChargePointBootNotificationTest : public ChargePointTestBase {
protected:
    nlohmann::json boot_payload_for_modem(const std::string& iccid, const std::string& imsi) {
        this->configuration->setChargepointModemInformation(iccid, imsi);
        auto charge_point = make_charge_point();
        charge_point->init({{0, ChargePointStatus::Available}, {1, ChargePointStatus::Available}}, {});
        charge_point->start({}, BootReasonEnum::PowerUp, {});
        charge_point->on_websocket_connected(0, ocpp::v2::NetworkConnectionProfile{}, ocpp::OcppProtocolVersion::v16);
        EXPECT_TRUE(wait_for_action_count("BootNotification", 1, WAIT_TIMEOUT));
        EXPECT_TRUE(wait_for_all_answered());
        const auto payload = last_payload("BootNotification");
        charge_point->stop();
        return payload.value_or(nlohmann::json::object());
    }
};

TEST_F(ChargePointBootNotificationTest, ModemInformationIsSent) {
    const auto payload = boot_payload_for_modem("8914800000000000000", "262012345678901");
    EXPECT_EQ(payload.value("iccid", ""), "8914800000000000000");
    EXPECT_EQ(payload.value("imsi", ""), "262012345678901");
}

TEST_F(ChargePointBootNotificationTest, EmptyModemInformationIsOmitted) {
    const auto payload = boot_payload_for_modem("", "");
    EXPECT_FALSE(payload.contains("iccid"));
    EXPECT_FALSE(payload.contains("imsi"));
}

TEST_F(ChargePointBootNotificationTest, OnlyEmptyModemFieldIsOmitted) {
    const auto payload = boot_payload_for_modem("8914800000000000000", "");
    EXPECT_EQ(payload.value("iccid", ""), "8914800000000000000");
    EXPECT_FALSE(payload.contains("imsi"));
}

} // namespace v16
} // namespace ocpp
