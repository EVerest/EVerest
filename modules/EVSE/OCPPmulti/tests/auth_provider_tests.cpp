// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <generic_ocpp.hpp>

#include "stubs/generic_ocpp_stub.hpp"

namespace {
using namespace stubs;
using ocpp::v2::IdToken;
using ocpp::v2::RequestStartTransactionRequest;
using types::authorization::IdTokenType;

TEST_F(GenericOcppProvidesTester, publishProvidedToken) {
    // publish_provided_token() called from cb_remote_start_transaction

    const IdToken token{"TokenID", "Local"};
    RequestStartTransactionRequest req;
    req.idToken = token;
    req.remoteStartId = 1234;
    // std::optional<IdToken> groupIdToken;
    // std::optional<ChargingProfile> chargingProfile;
    // std::optional<CustomData> customData;

    std::vector<json> received;
    interfaces.subscribe_var("auth_provider", "provided_token",
                             [&received](const auto&, const auto&, const auto& data) { received.push_back(data); });

    ocpp.cb_remote_start_transaction(req, false);
    req.remoteStartId = 4321;
    ocpp.cb_remote_start_transaction(req, true);

    ASSERT_EQ(received.size(), 2);
    EXPECT_EQ(
        received[0],
        R"({"authorization_type":"OCPP","id_token":{"type":"Local","value":"TokenID"},"prevalidated":true,"request_id":1234})"_json);
    EXPECT_EQ(
        received[1],
        R"({"authorization_type":"OCPP","id_token":{"type":"Local","value":"TokenID"},"prevalidated":false,"request_id":4321})"_json);
}

} // namespace
