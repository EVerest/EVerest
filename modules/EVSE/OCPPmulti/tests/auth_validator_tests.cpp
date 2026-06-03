// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <generic_ocpp.hpp>

#include "stubs/generic_ocpp_stub.hpp"

namespace {
using namespace stubs;
using ocpp::v2::AuthorizationStatusEnum;
using ::testing::_;
using ::testing::Return;
using types::authorization::AuthorizationStatus;
using types::authorization::AuthorizationType;
using types::authorization::IdTokenType;
using types::authorization::ProvidedIdToken;
using types::authorization::ValidationResult;

// ValidationResult handle_validate_token(types::authorization::ProvidedIdToken& provided_token)

// types::authorization::IdToken id_token; ///< IdToken of the provided token
// types::authorization::AuthorizationType authorization_type; ///< Authorization type of the token.
// std::optional<int32_t> request_id; ///< Id of the authorization request of this token. Could be used to put
// remoteStartId of OCPP2.0.1 std::optional<types::authorization::IdToken> parent_id_token; ///< Parent IdToken of
// the provided token std::optional<std::vector<int32_t>> connectors; ///< A list of connector ids to which the
// authorization can be assigned std::optional<bool> prevalidated; ///< Indicates that the id token is already
// validated by the provider std::optional<std::string> certificate; ///< The X.509 certificated presented by EV and
// encoded in PEM format std::optional<std::vector<types::iso15118::CertificateHashDataInfo>>
// iso15118CertificateHashData; ///< Contains the information needed to verify the EV Contract Certificate via OCSP

TEST_F(GenericOcppProvidesTester, authValidatorAccepted) {
    ProvidedIdToken token{{
                              "MyToken",
                              IdTokenType::Local,
                          },
                          AuthorizationType::RFID};

    ocpp::v2::IdToken expected_token{"MyToken", "Local"};
    ocpp::v2::AuthorizeResponse set_return;
    set_return.idTokenInfo.status = ocpp::v2::AuthorizationStatusEnum::Accepted;
    EXPECT_CALL(chargepoint, validate_token(expected_token, _, _)).WillOnce(Return(set_return));

    const auto result = ocpp.handle_validate_token(token);
    EXPECT_EQ(result.authorization_status, AuthorizationStatus::Accepted);
}

TEST_F(GenericOcppProvidesTester, authValidatorInvalid) {
    ProvidedIdToken token{{
                              "MyToken",
                              IdTokenType::KeyCode,
                          },
                          AuthorizationType::RFID};

    ocpp::v2::IdToken expected_token{"MyToken", "KeyCode"};
    ocpp::v2::AuthorizeResponse set_return;
    set_return.idTokenInfo.status = ocpp::v2::AuthorizationStatusEnum::Invalid;
    EXPECT_CALL(chargepoint, validate_token(expected_token, _, _)).WillOnce(Return(set_return));

    const auto result = ocpp.handle_validate_token(token);
    EXPECT_EQ(result.authorization_status, AuthorizationStatus::Invalid);
}

} // namespace
