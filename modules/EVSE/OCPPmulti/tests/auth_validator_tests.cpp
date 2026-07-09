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

TEST_F(GenericOcppProvidesTester, authValidatorAccepted) {
    ProvidedIdToken token{{
                              "MyToken",
                              IdTokenType::Local,
                          },
                          AuthorizationType::RFID};

    ocpp::v2::AuthorizeResponse set_return;
    set_return.idTokenInfo.status = ocpp::v2::AuthorizationStatusEnum::Accepted;
    EXPECT_CALL(chargepoint, validate_token(token)).WillOnce(Return(set_return));

    const auto result = ocpp->handle_validate_token(token);
    EXPECT_EQ(result.authorization_status, AuthorizationStatus::Accepted);
}

TEST_F(GenericOcppProvidesTester, authValidatorInvalid) {
    ProvidedIdToken token{{
                              "MyToken",
                              IdTokenType::KeyCode,
                          },
                          AuthorizationType::RFID};

    ocpp::v2::AuthorizeResponse set_return;
    set_return.idTokenInfo.status = ocpp::v2::AuthorizationStatusEnum::Invalid;
    EXPECT_CALL(chargepoint, validate_token(token)).WillOnce(Return(set_return));

    const auto result = ocpp->handle_validate_token(token);
    EXPECT_EQ(result.authorization_status, AuthorizationStatus::Invalid);
}

TEST_F(GenericOcppProvidesTester, publishValidateResultUpdate) {
    // publish_validate_result_update() called from cb_transaction_event_response
    // when
    // - transaction_event.evse.has_value()
    // - transaction_event_response.idTokenInfo.has_value()

    using ocpp::DateTime;
    using ocpp::v2::AuthorizationStatusEnum;
    using ocpp::v2::EVSE;
    using ocpp::v2::IdTokenInfo;
    using ocpp::v2::Transaction;
    using ocpp::v2::TransactionEventEnum;
    using ocpp::v2::TransactionEventRequest;
    using ocpp::v2::TransactionEventResponse;
    using ocpp::v2::TriggerReasonEnum;

    TransactionEventRequest transaction_event;
    transaction_event.eventType = TransactionEventEnum::Started;
    transaction_event.timestamp = DateTime();
    transaction_event.triggerReason = TriggerReasonEnum::CablePluggedIn;
    transaction_event.seqNo = 10;
    transaction_event.transactionInfo = Transaction{"transactionId"};
    // std::optional<CostDetails> costDetails;
    // std::optional<std::vector<MeterValue>> meterValue;
    // std::optional<bool> offline;
    // std::optional<std::int32_t> numberOfPhasesUsed;
    // std::optional<std::int32_t> cableMaxCurrent;
    // std::optional<std::int32_t> reservationId;
    // std::optional<PreconditioningStatusEnum> preconditioningStatus;
    // std::optional<bool> evseSleep;
    // std::optional<EVSE> evse;
    // std::optional<IdToken> idToken;
    // std::optional<CustomData> customData;

    TransactionEventResponse transaction_event_response{};
    // std::optional<float> totalCost;
    // std::optional<std::int32_t> chargingPriority;
    // std::optional<IdTokenInfo> idTokenInfo;
    // std::optional<TransactionLimit> transactionLimit;
    // std::optional<MessageContent> updatedPersonalMessage;
    // std::optional<std::vector<MessageContent>> updatedPersonalMessageExtra;
    // std::optional<CustomData> customData;

    std::vector<json> received;
    interfaces->subscribe_var("auth_validator", "validate_result_update",
                              [&received](const auto&, const auto&, const auto& data) { received.push_back(data); });

    ocpp->cb_transaction_event_response(transaction_event, transaction_event_response, "transactionId");
    EXPECT_TRUE(received.empty());

    transaction_event.evse = EVSE{1, 0};
    transaction_event_response.idTokenInfo = IdTokenInfo{AuthorizationStatusEnum::Accepted};
    ocpp->cb_transaction_event_response(transaction_event, transaction_event_response, "transactionId");

    ASSERT_EQ(received.size(), 1);
    EXPECT_EQ(
        received[0],
        R"({"connector_id":1,"validation_result":{"authorization_status":"Accepted","tariff_messages":[]}})"_json);
}

} // namespace
