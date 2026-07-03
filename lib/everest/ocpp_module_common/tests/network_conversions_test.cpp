// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gtest/gtest.h>

#include <everest/ocpp_module_common/conversions.hpp>

namespace {

using ocpp_module_common::conversions::to_everest_configure_network_request;
using ocpp_module_common::conversions::to_everest_interface_class;
using ocpp_module_common::conversions::to_everest_network_apn;
using ocpp_module_common::conversions::to_everest_network_vpn;

TEST(NetworkConversions, interface_class_all_mappings) {
    EXPECT_EQ(to_everest_interface_class(ocpp::v2::OCPPInterfaceEnum::Wired0), types::network::InterfaceClass::Wired0);
    EXPECT_EQ(to_everest_interface_class(ocpp::v2::OCPPInterfaceEnum::Wired1), types::network::InterfaceClass::Wired1);
    EXPECT_EQ(to_everest_interface_class(ocpp::v2::OCPPInterfaceEnum::Wired2), types::network::InterfaceClass::Wired2);
    EXPECT_EQ(to_everest_interface_class(ocpp::v2::OCPPInterfaceEnum::Wired3), types::network::InterfaceClass::Wired3);
    EXPECT_EQ(to_everest_interface_class(ocpp::v2::OCPPInterfaceEnum::Wireless0),
              types::network::InterfaceClass::Wireless0);
    EXPECT_EQ(to_everest_interface_class(ocpp::v2::OCPPInterfaceEnum::Wireless1),
              types::network::InterfaceClass::Wireless1);
    EXPECT_EQ(to_everest_interface_class(ocpp::v2::OCPPInterfaceEnum::Wireless2),
              types::network::InterfaceClass::Wireless2);
    EXPECT_EQ(to_everest_interface_class(ocpp::v2::OCPPInterfaceEnum::Wireless3),
              types::network::InterfaceClass::Wireless3);
    EXPECT_EQ(to_everest_interface_class(ocpp::v2::OCPPInterfaceEnum::Any), types::network::InterfaceClass::Any);
}

TEST(NetworkConversions, apn_all_optionals_set) {
    ocpp::v2::APN apn;
    apn.apn = "internet";
    apn.apnAuthentication = ocpp::v2::APNAuthenticationEnum::CHAP;
    apn.apnUserName = "user";
    apn.apnPassword = "pass";
    apn.simPin = 1234;
    apn.preferredNetwork = "LTE";
    apn.useOnlyPreferredNetwork = true;

    const auto result = to_everest_network_apn(apn);

    EXPECT_EQ(result.apn, "internet");
    ASSERT_TRUE(result.apn_authentication.has_value());
    EXPECT_EQ(result.apn_authentication.value(), types::network::Apn_authentication::CHAP);
    ASSERT_TRUE(result.apn_user_name.has_value());
    EXPECT_EQ(result.apn_user_name.value(), "user");
    ASSERT_TRUE(result.apn_password.has_value());
    EXPECT_EQ(result.apn_password.value(), "pass");
    ASSERT_TRUE(result.sim_pin.has_value());
    EXPECT_EQ(result.sim_pin.value(), "1234");
    ASSERT_TRUE(result.preferred_network.has_value());
    EXPECT_EQ(result.preferred_network.value(), "LTE");
    ASSERT_TRUE(result.use_only_preferred_network.has_value());
    EXPECT_TRUE(result.use_only_preferred_network.value());
}

TEST(NetworkConversions, apn_only_required_fields) {
    ocpp::v2::APN apn;
    apn.apn = "internet";
    apn.apnAuthentication = ocpp::v2::APNAuthenticationEnum::NONE;

    const auto result = to_everest_network_apn(apn);

    EXPECT_EQ(result.apn, "internet");
    ASSERT_TRUE(result.apn_authentication.has_value());
    EXPECT_EQ(result.apn_authentication.value(), types::network::Apn_authentication::NONE);
    EXPECT_FALSE(result.apn_user_name.has_value());
    EXPECT_FALSE(result.apn_password.has_value());
    EXPECT_FALSE(result.sim_pin.has_value());
    EXPECT_FALSE(result.preferred_network.has_value());
    EXPECT_FALSE(result.use_only_preferred_network.has_value());
}

TEST(NetworkConversions, apn_authentication_all_mappings) {
    ocpp::v2::APN apn;
    apn.apn = "internet";

    apn.apnAuthentication = ocpp::v2::APNAuthenticationEnum::PAP;
    EXPECT_EQ(to_everest_network_apn(apn).apn_authentication.value(), types::network::Apn_authentication::PAP);
    apn.apnAuthentication = ocpp::v2::APNAuthenticationEnum::CHAP;
    EXPECT_EQ(to_everest_network_apn(apn).apn_authentication.value(), types::network::Apn_authentication::CHAP);
    apn.apnAuthentication = ocpp::v2::APNAuthenticationEnum::NONE;
    EXPECT_EQ(to_everest_network_apn(apn).apn_authentication.value(), types::network::Apn_authentication::NONE);
    apn.apnAuthentication = ocpp::v2::APNAuthenticationEnum::AUTO;
    EXPECT_EQ(to_everest_network_apn(apn).apn_authentication.value(), types::network::Apn_authentication::AUTO);
}

TEST(NetworkConversions, apn_sim_pin_int_to_string) {
    ocpp::v2::APN apn;
    apn.apn = "internet";
    apn.apnAuthentication = ocpp::v2::APNAuthenticationEnum::NONE;
    apn.simPin = 1234;

    const auto result = to_everest_network_apn(apn);
    ASSERT_TRUE(result.sim_pin.has_value());
    EXPECT_EQ(result.sim_pin.value(), "1234");
}

TEST(NetworkConversions, vpn_full) {
    ocpp::v2::VPN vpn;
    vpn.server = "vpn.example.com";
    vpn.user = "vpnuser";
    vpn.password = "vpnpass";
    vpn.key = "secretkey";
    vpn.type = ocpp::v2::VPNEnum::IKEv2;
    vpn.group = "vpngroup";

    const auto result = to_everest_network_vpn(vpn);

    EXPECT_EQ(result.server, "vpn.example.com");
    EXPECT_EQ(result.user, "vpnuser");
    EXPECT_EQ(result.password, "vpnpass");
    EXPECT_EQ(result.key, "secretkey");
    EXPECT_EQ(result.type, types::network::Type::IKEv2);
    ASSERT_TRUE(result.group.has_value());
    EXPECT_EQ(result.group.value(), "vpngroup");
}

TEST(NetworkConversions, vpn_without_group) {
    ocpp::v2::VPN vpn;
    vpn.server = "vpn.example.com";
    vpn.user = "vpnuser";
    vpn.password = "vpnpass";
    vpn.key = "secretkey";
    vpn.type = ocpp::v2::VPNEnum::IPSec;

    const auto result = to_everest_network_vpn(vpn);

    EXPECT_EQ(result.type, types::network::Type::IPSec);
    EXPECT_FALSE(result.group.has_value());
}

TEST(NetworkConversions, vpn_type_all_mappings) {
    ocpp::v2::VPN vpn;
    vpn.server = "s";
    vpn.user = "u";
    vpn.password = "p";
    vpn.key = "k";

    vpn.type = ocpp::v2::VPNEnum::IKEv2;
    EXPECT_EQ(to_everest_network_vpn(vpn).type, types::network::Type::IKEv2);
    vpn.type = ocpp::v2::VPNEnum::IPSec;
    EXPECT_EQ(to_everest_network_vpn(vpn).type, types::network::Type::IPSec);
    vpn.type = ocpp::v2::VPNEnum::L2TP;
    EXPECT_EQ(to_everest_network_vpn(vpn).type, types::network::Type::L2TP);
    vpn.type = ocpp::v2::VPNEnum::PPTP;
    EXPECT_EQ(to_everest_network_vpn(vpn).type, types::network::Type::PPTP);
}

TEST(NetworkConversions, configure_network_request_with_apn_and_vpn) {
    ocpp::v2::NetworkConnectionProfile profile{};
    profile.ocppInterface = ocpp::v2::OCPPInterfaceEnum::Wireless0;
    profile.ocppCsmsUrl = "wss://csms.example.com"; // must NOT leak

    ocpp::v2::APN apn;
    apn.apn = "internet";
    apn.apnAuthentication = ocpp::v2::APNAuthenticationEnum::CHAP;
    profile.apn = apn;

    ocpp::v2::VPN vpn;
    vpn.server = "vpn.example.com";
    vpn.user = "u";
    vpn.password = "p";
    vpn.key = "k";
    vpn.type = ocpp::v2::VPNEnum::L2TP;
    profile.vpn = vpn;

    const auto result = to_everest_configure_network_request(42, profile);

    EXPECT_EQ(result.request_id, 42);
    EXPECT_EQ(result.interface, types::network::InterfaceClass::Wireless0);
    EXPECT_FALSE(result.interface_name.has_value());
    ASSERT_TRUE(result.apn.has_value());
    EXPECT_EQ(result.apn.value().apn, "internet");
    ASSERT_TRUE(result.vpn.has_value());
    EXPECT_EQ(result.vpn.value().type, types::network::Type::L2TP);
}

TEST(NetworkConversions, configure_network_request_without_apn_or_vpn) {
    ocpp::v2::NetworkConnectionProfile profile{};
    profile.ocppInterface = ocpp::v2::OCPPInterfaceEnum::Wired1;

    const auto result = to_everest_configure_network_request(7, profile);

    EXPECT_EQ(result.request_id, 7);
    EXPECT_EQ(result.interface, types::network::InterfaceClass::Wired1);
    EXPECT_FALSE(result.interface_name.has_value());
    EXPECT_FALSE(result.apn.has_value());
    EXPECT_FALSE(result.vpn.has_value());
}

} // namespace
