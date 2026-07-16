// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <generic_ocpp.hpp>

#include "generic_chargepoint_interface.hpp"
#include "stubs/generic_ocpp_stub.hpp"

namespace {
using namespace stubs;
using ocpp::v2::IdToken;
using ocpp::v2::RequestStartTransactionRequest;
using types::authorization::IdTokenType;

TEST_F(GenericOcppProvidesTester, publishProvidedToken) {
    // publish_provided_token() called from cb_provide_token

    using IdToken = ocpp_multi::GenericChargePointCallbacks::IdToken;

    IdToken token;
    token.token = {"TokenID", "Local"};
    token.request_id = 1234;
    token.prevalidated = true;

    std::vector<json> received;
    interfaces->subscribe_var("auth_provider", "provided_token",
                              [&received](const auto&, const auto&, const auto& data) { received.push_back(data); });

    ocpp->cb_provide_token(token);
    token.request_id = 4321;
    token.prevalidated = false;
    ocpp->cb_provide_token(token);

    ASSERT_EQ(received.size(), 2);
    EXPECT_EQ(
        received[0],
        R"({"authorization_type":"OCPP","id_token":{"type":"Local","value":"TokenID"},"prevalidated":true,"request_id":1234})"_json);
    EXPECT_EQ(
        received[1],
        R"({"authorization_type":"OCPP","id_token":{"type":"Local","value":"TokenID"},"prevalidated":false,"request_id":4321})"_json);
}

} // namespace
