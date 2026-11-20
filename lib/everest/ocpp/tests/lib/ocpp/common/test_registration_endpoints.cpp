// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <gtest/gtest.h>

#include <string>

#include <ocpp/common/registration_endpoints.hpp>

using namespace ocpp;

namespace {
const std::string IDENTITY = "cp001";
} // namespace

TEST(RegistrationEndpointsTest, NormalisedCsmsUrlIgnoresHostCaseDefaultPortAndTrailingSlash) {
    const auto url = normalised_csms_url("ws://csms.example.com/ocpp", IDENTITY);
    EXPECT_EQ(normalised_csms_url("ws://CSMS.Example.com/ocpp", IDENTITY), url);
    EXPECT_EQ(normalised_csms_url("ws://csms.example.com:80/ocpp", IDENTITY), url);
    EXPECT_EQ(normalised_csms_url("ws://csms.example.com/ocpp/", IDENTITY), url);
    EXPECT_EQ(normalised_csms_url("wss://csms.example.com:443/ocpp", IDENTITY),
              normalised_csms_url("wss://csms.example.com/ocpp", IDENTITY));
}

// The security profile picks the scheme of a URL without one when connecting. A change of security profile does not
// change the CSMS, so such a URL is taken as `ws://`.
TEST(RegistrationEndpointsTest, NormalisedCsmsUrlTakesUrlWithoutSchemeAsUnsecured) {
    EXPECT_EQ(normalised_csms_url("csms.example.com/ocpp", IDENTITY),
              normalised_csms_url("ws://csms.example.com/ocpp", IDENTITY));
    EXPECT_EQ(normalised_csms_url("CSMS.example.com:9000/ocpp/", IDENTITY),
              normalised_csms_url("ws://csms.example.com:9000/ocpp", IDENTITY));
    EXPECT_NE(normalised_csms_url("csms.example.com/ocpp", IDENTITY),
              normalised_csms_url("wss://csms.example.com/ocpp", IDENTITY));
    EXPECT_NE(normalised_csms_url("csms.example.com:9000/ocpp", IDENTITY),
              normalised_csms_url("csms.example.com:9001/ocpp", IDENTITY));
}

// A CSMS URL may carry the identity of the charging station as its last segment, which the connection appends anyway
TEST(RegistrationEndpointsTest, NormalisedCsmsUrlIgnoresIdentityAsLastSegment) {
    EXPECT_EQ(normalised_csms_url("ws://csms.example.com/ocpp/cp001", IDENTITY),
              normalised_csms_url("ws://csms.example.com/ocpp", IDENTITY));
    EXPECT_NE(normalised_csms_url("ws://csms.example.com/ocpp/cp002", IDENTITY),
              normalised_csms_url("ws://csms.example.com/ocpp", IDENTITY));
}

TEST(RegistrationEndpointsTest, NormalisedCsmsUrlKeepsSchemeHostPortAndPathApart) {
    const auto url = normalised_csms_url("ws://csms.example.com/ocpp", IDENTITY);
    EXPECT_NE(normalised_csms_url("ws://other.example.com/ocpp", IDENTITY), url);
    EXPECT_NE(normalised_csms_url("ws://csms.example.com:9000/ocpp", IDENTITY), url);
    EXPECT_NE(normalised_csms_url("ws://csms.example.com/other", IDENTITY), url);
    EXPECT_NE(normalised_csms_url("ws://csms.example.com/OCPP", IDENTITY), url);
    EXPECT_NE(normalised_csms_url("wss://csms.example.com/ocpp", IDENTITY), url);
}

TEST(RegistrationEndpointsTest, NormalisedCsmsUrlReturnsUnusableUrlAsWritten) {
    EXPECT_EQ(normalised_csms_url("ws:csms.example.com", IDENTITY), "ws:csms.example.com");
    EXPECT_EQ(normalised_csms_url("", IDENTITY), "");
}

// Without an identity there is no last segment to strip, but the URL is still compared as the websocket URL
TEST(RegistrationEndpointsTest, NormalisedCsmsUrlWithoutIdentityIgnoresHostCaseDefaultPortAndTrailingSlash) {
    const auto url = normalised_csms_url("ws://csms.example.com/ocpp", "");
    EXPECT_EQ(normalised_csms_url("ws://CSMS.example.com:80/ocpp/", ""), url);
    EXPECT_EQ(normalised_csms_url("ws://csms.example.com/ocpp", IDENTITY), url);
    EXPECT_NE(normalised_csms_url("ws://csms.example.com/ocpp/cp001", ""), url);
}

TEST(RegistrationEndpointsTest, ConnectReturnsNothingBeforeARegistration) {
    RegistrationEndpoints endpoints;
    EXPECT_FALSE(endpoints.connect({1, "ws://csms.example.com"}).has_value());
    EXPECT_FALSE(endpoints.connect({2, "ws://other.example.com"}).has_value());
}

TEST(RegistrationEndpointsTest, ConnectReturnsRegisteredEndpointWhenItDiffers) {
    RegistrationEndpoints endpoints;
    endpoints.connect({1, "ws://csms.example.com"});
    endpoints.register_connected();

    const auto other_slot = endpoints.connect({2, "ws://csms.example.com"});
    ASSERT_TRUE(other_slot.has_value());
    EXPECT_EQ(other_slot->configuration_slot, 1);

    const auto other_url = endpoints.connect({1, "ws://other.example.com"});
    ASSERT_TRUE(other_url.has_value());
    EXPECT_EQ(other_url->ocpp_csms_url, "ws://csms.example.com");
}
