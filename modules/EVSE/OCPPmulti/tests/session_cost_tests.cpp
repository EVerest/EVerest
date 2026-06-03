// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <generic_ocpp.hpp>
#include <optional>

#include "stubs/generic_ocpp_stub.hpp"

namespace {
using namespace stubs;

TEST_F(GenericOcppProvidesTester, publishDefaultPrice) {
    // publish_default_price() called from cb_default_price

    using types::session_cost::DefaultPrice;

    std::vector<json> received;
    interfaces->subscribe_var("session_cost", "default_price",
                              [&received](const auto&, const auto&, const auto& data) { received.push_back(data); });

    DefaultPrice messages;
    messages.messages.push_back({"My Message", std::nullopt, std::nullopt});

    // std::string content; ///< TODO: description
    // std::optional<types::text_message::MessageFormat> format; ///< TODO: description
    // std::optional<std::string> language; ///< TODO: description

    ocpp->cb_default_price(messages);

    ASSERT_EQ(received.size(), 1);
    EXPECT_EQ(received[0], R"({"messages":[{"content":"My Message"}]})"_json);
}

TEST_F(GenericOcppProvidesTester, publishTariffMessage) {
    // publish_tariff_message() called from cb_tariff_message and
    // handle_validate_token

    using ocpp::DisplayMessageContent;
    using ocpp::IdentifierType;
    using ocpp::TariffMessage;
    using ocpp::v2::MessageContent;
    using ocpp::v2::MessageFormatEnum;
    using ocpp::v2::Tariff;
    using ::testing::_;
    using ::testing::Return;
    using types::authorization::AuthorizationType;
    using types::authorization::IdToken;
    using types::authorization::IdTokenType;
    using types::authorization::ProvidedIdToken;

    std::vector<json> received;
    interfaces->subscribe_var("session_cost", "tariff_message",
                              [&received](const auto&, const auto&, const auto& data) { received.push_back(data); });

    TariffMessage message;
    message.ocpp_transaction_id = "TransactionID";
    message.identifier_id = "ID";
    message.identifier_type = IdentifierType::IdToken;
    // std::vector<DisplayMessageContent> message;

    DisplayMessageContent content;
    content.message = "My Message";
    // std::string message;
    // std::optional<std::string> language;
    // std::optional<v2::MessageFormatEnum> message_format;

    message.message.push_back(content);

    ocpp->cb_tariff_message(message);

    ProvidedIdToken token;
    // types::authorization::IdToken id_token;
    // types::authorization::AuthorizationType authorization_type;
    // std::optional<int32_t> request_id;
    // std::optional<types::authorization::IdToken> parent_id_token;
    // std::optional<std::vector<int32_t>> connectors;
    // std::optional<bool> prevalidated;
    // std::optional<std::string> certificate;
    // std::optional<std::vector<types::iso15118::CertificateHashDataInfo>> iso15118CertificateHashData;

    IdToken id_token;
    id_token.value = "ID Token";
    id_token.type = IdTokenType::MacAddress;
    // std::optional<std::vector<types::authorization::CustomIdToken>> additional_info; ///< A list of additional custom
    // id tokens than can be used for providing additional information for validation

    token.id_token = id_token;
    token.authorization_type = AuthorizationType::PlugAndCharge;
    token.request_id = 127;
    token.prevalidated = true;

    ocpp::v2::AuthorizeResponse expected_output;
    expected_output.idTokenInfo.status = ocpp::v2::AuthorizationStatusEnum::Accepted;
    MessageContent msg_content;
    msg_content.format = MessageFormatEnum::ASCII;
    msg_content.content = content.message;
    expected_output.idTokenInfo.personalMessage = msg_content;

    EXPECT_CALL(chargepoint, validate_token(token)).WillOnce(Return(expected_output));

    ocpp->handle_validate_token(token);

    ASSERT_EQ(received.size(), 1);
    EXPECT_EQ(
        received[0],
        R"({"identifier_id":"ID","identifier_type":"IdToken","messages":[{"content":"My Message"}],"ocpp_transaction_id":"TransactionID"})"_json);
}

// void publish_session_cost(const types::session_cost::SessionCost& value) {
TEST_F(GenericOcppProvidesTester, publishSessionCost) {
    // publish_session_cost() called from cb_set_running_cost

    std::vector<json> received;
    interfaces->subscribe_var("session_cost", "session_cost",
                              [&received](const auto&, const auto&, const auto& data) { received.push_back(data); });

    ocpp::RunningCost running_cost;
    running_cost.transaction_id = "TransactionID";
    running_cost.timestamp = ocpp::DateTime{"2026-06-08T08:25:04.255Z"};
    running_cost.cost = 3.14159;
    running_cost.state = ocpp::RunningCostState::Finished;
    // std::optional<std::uint32_t> meter_value;
    // std::optional<RunningCostChargingPrice> charging_price;
    // std::optional<RunningCostIdlePrice> idle_price;
    // std::optional<DateTime> next_period_at_time;
    // std::optional<RunningCostChargingPrice> next_period_charging_price;
    // std::optional<RunningCostIdlePrice> next_period_idle_price;
    // std::optional<std::vector<DisplayMessageContent>> cost_messages;
    // std::optional<std::string> qr_code_text;

    std::uint32_t number_of_decimals{3};
    std::string currency_code{"GBP"};

    ocpp->cb_set_running_cost(running_cost, number_of_decimals, currency_code);

    ASSERT_EQ(received.size(), 1);
    EXPECT_EQ(
        received[0],
        R"({"cost_chunks":[{"cost":{"value":3141},"timestamp_to":"2026-06-08T08:25:04.255Z"}],"currency":{"code":"GBP","decimals":3},"session_id":"TransactionID","status":"Finished"})"_json);
}

} // namespace
