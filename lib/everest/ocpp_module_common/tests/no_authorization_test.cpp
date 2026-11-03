// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gtest/gtest.h>

#include <everest/ocpp_module_common/no_authorization.hpp>

namespace {

using ocpp_module_common::make_no_authorization_token;

TEST(NoAuthorization, token_is_prevalidated_for_the_evse) {
    const auto token = make_no_authorization_token(2, "");
    EXPECT_EQ(token.id_token.value, "");
    EXPECT_EQ(token.id_token.type, types::authorization::IdTokenType::NoAuthorization);
    EXPECT_EQ(token.authorization_type, types::authorization::AuthorizationType::OCPP);
    EXPECT_EQ(token.prevalidated, true);
    EXPECT_EQ(token.connectors, std::vector<std::int32_t>{2});
    EXPECT_FALSE(token.request_id.has_value());

    EXPECT_EQ(make_no_authorization_token(1, ocpp_module_common::NO_AUTHORIZATION_ID_TAG_V16).id_token.value,
              "FREESERVICE");
}

types::evse_manager::SessionEvent session_event(types::evse_manager::SessionEventEnum event) {
    types::evse_manager::SessionEvent result;
    result.event = event;
    return result;
}

struct NoAuthorizationAuthorizerTest : public testing::Test {
    std::vector<std::int32_t> authorised;
    ocpp_module_common::NoAuthorizationAuthorizer authorizer{
        [this](std::int32_t evse_id) { this->authorised.push_back(evse_id); }};

    void auth_required(std::int32_t evse_id) {
        authorizer.on_session_event(evse_id, session_event(types::evse_manager::SessionEventEnum::AuthRequired));
    }
};

TEST_F(NoAuthorizationAuthorizerTest, nothing_while_enabled) {
    authorizer.set_auth_enabled(true);
    auth_required(1);
    EXPECT_TRUE(authorised.empty());
}

TEST_F(NoAuthorizationAuthorizerTest, waiting_before_the_setting_is_read) {
    auth_required(1);
    EXPECT_TRUE(authorised.empty());

    authorizer.set_auth_enabled(false);
    EXPECT_EQ(authorised, std::vector<std::int32_t>{1});
}

TEST_F(NoAuthorizationAuthorizerTest, waiting_when_switched_off) {
    authorizer.set_auth_enabled(true);
    auth_required(1);
    auth_required(2);
    authorizer.on_session_event(2, session_event(types::evse_manager::SessionEventEnum::Authorized));

    authorizer.set_auth_enabled(false);
    EXPECT_EQ(authorised, std::vector<std::int32_t>{1});

    authorizer.set_auth_enabled(false);
    EXPECT_EQ(authorised, std::vector<std::int32_t>{1});
}

TEST_F(NoAuthorizationAuthorizerTest, auth_required_while_disabled) {
    authorizer.set_auth_enabled(false);
    auth_required(2);
    EXPECT_EQ(authorised, std::vector<std::int32_t>{2});

    authorizer.on_session_event(2, session_event(types::evse_manager::SessionEventEnum::SessionFinished));
    authorizer.set_auth_enabled(true);
    authorizer.set_auth_enabled(false);
    EXPECT_EQ(authorised, std::vector<std::int32_t>{2});
}

TEST_F(NoAuthorizationAuthorizerTest, deauthorised_evse_no_longer_waits) {
    authorizer.set_auth_enabled(true);
    auth_required(1);
    authorizer.on_session_event(1, session_event(types::evse_manager::SessionEventEnum::Deauthorized));

    authorizer.set_auth_enabled(false);
    EXPECT_TRUE(authorised.empty());
}

TEST_F(NoAuthorizationAuthorizerTest, retry_authorises_waiting_evses_while_disabled) {
    authorizer.set_auth_enabled(true);
    auth_required(1);
    authorizer.retry();
    EXPECT_TRUE(authorised.empty());

    authorizer.set_auth_enabled(false);
    authorizer.retry();
    EXPECT_EQ(authorised, (std::vector<std::int32_t>{1, 1}));

    authorizer.on_session_event(1, session_event(types::evse_manager::SessionEventEnum::Authorized));
    authorizer.retry();
    EXPECT_EQ(authorised, (std::vector<std::int32_t>{1, 1}));
}

TEST(NoAuthorization, token_is_no_remote_start) {
    EXPECT_FALSE(ocpp_module_common::is_remote_start(make_no_authorization_token(1, "")));

    types::authorization::ProvidedIdToken remote_start;
    remote_start.id_token = {"TOKEN", types::authorization::IdTokenType::Central};
    remote_start.authorization_type = types::authorization::AuthorizationType::OCPP;
    remote_start.request_id = 7;
    EXPECT_TRUE(ocpp_module_common::is_remote_start(remote_start));

    remote_start.id_token = {"", types::authorization::IdTokenType::NoAuthorization};
    EXPECT_TRUE(ocpp_module_common::is_remote_start(remote_start));

    remote_start.request_id.reset();
    EXPECT_FALSE(ocpp_module_common::is_remote_start(remote_start));

    remote_start.id_token = {"TOKEN", types::authorization::IdTokenType::Central};
    EXPECT_TRUE(ocpp_module_common::is_remote_start(remote_start));

    remote_start.request_id = 7;
    remote_start.authorization_type = types::authorization::AuthorizationType::RFID;
    EXPECT_FALSE(ocpp_module_common::is_remote_start(remote_start));
}

} // namespace
