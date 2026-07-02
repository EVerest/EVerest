// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

// cmds:
//   set_display_message:
//   get_display_messages:
//   clear_display_message:

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <generic_ocpp.hpp>

#include "stubs/generic_ocpp_stub.hpp"

namespace {
using namespace stubs;

TEST_F(GenericOcppRequiresTester, callSetDisplayMessage) {
    // call_set_display_message() used in cb_set_display_message()

    using ocpp::DisplayMessage;
    using ocpp::DisplayMessageContent;
    using ocpp::IdentifierType;
    using ocpp::v2::MessageFormatEnum;
    using ocpp::v2::MessagePriorityEnum;
    using ocpp::v2::MessageStateEnum;
    using ocpp::v2::SetDisplayMessageResponse;

    std::vector<json> received;
    interfaces->subscribe_var("display_message", "call_set_display_message",
                              [&received](const auto&, const auto&, const auto& data) { received.push_back(data); });

    DisplayMessageContent content;
    content.message = "Available";
    content.language = "en.UK";
    content.message_format = MessageFormatEnum::ASCII;

    DisplayMessage message;
    message.id = 2587;
    message.priority = MessagePriorityEnum::NormalCycle;
    message.state = MessageStateEnum::Idle;
    message.identifier_id = "ID";
    message.identifier_type = IdentifierType::IdToken;
    message.message = content;
    // std::optional<DateTime> timestamp_from;
    // std::optional<DateTime> timestamp_to;
    // std::optional<std::string> qr_code;

    std::vector<DisplayMessage> request;
    request.push_back(message);

    const json expected_response = R"({"status": "Accepted"})"_json;

    interfaces->add_cmd_result(expected_response);

    const auto response = ocpp->cb_set_display_message(request);

    ASSERT_EQ(received.size(), 1);
    EXPECT_EQ(
        received[0],
        R"({"request":[{"id":2587,"identifier_id":"ID","identifier_type":"IdToken","message":{"content":"Available","format":"ASCII","language":"en.UK"},"priority":"NormalCycle","state":"Idle"}]})"_json);
    EXPECT_EQ(response, expected_response);
}

TEST_F(GenericOcppRequiresTester, callGetDisplayMessages) {
    // call_get_display_messages() used in cb_get_display_message()

    using ocpp::DisplayMessage;
    using ocpp::DisplayMessageContent;
    using ocpp::v2::GetDisplayMessagesRequest;
    using ocpp::v2::MessageFormatEnum;
    using ocpp::v2::MessagePriorityEnum;
    using ocpp::v2::MessageStateEnum;

    std::vector<json> received;
    interfaces->subscribe_var("display_message", "call_get_display_messages",
                              [&received](const auto&, const auto&, const auto& data) { received.push_back(data); });

    GetDisplayMessagesRequest request;
    // std::optional<std::vector<int32_t>> id;
    // std::optional<types::display_message::MessagePriorityEnum> priority;
    // std::optional<types::display_message::MessageStateEnum> state;
    std::vector<std::int32_t> ids;
    ids.push_back(1);
    ids.push_back(2);
    ids.push_back(3);
    request.id = std::move(ids);
    request.requestId = 97524;
    request.priority = MessagePriorityEnum::AlwaysFront;
    request.state = MessageStateEnum::Charging;

    // GetDisplayMessageResponse:
    //   description: Response on the 'get display message' request. Will return the requested display messages.
    //   type: object
    //   properties:
    //     status_info:
    //       description: Detailed status information
    //       type: string
    //     messages:
    //       description: Requested messages, if any
    //       type: array
    //       items:
    //         type: object
    //         $ref: /display_message#/DisplayMessage
    //
    //  DisplayMessage:
    //    description: Message to show on a display
    //    type: object
    //    additionalProperties: false
    //    properties:
    //      id:
    //        description: The message id
    //        type: integer
    //      priority:
    //        description: >-
    //          Priority of the message. For OCPP 2.0.1, this is a required property. But as we also use this interface
    //          outside of ocpp, for other messages it is not required and if priority is not given, we assume it is the
    //          lowest priority, 'NormalCycle'. When priority is 'AlwaysFront' and there already is a message with
    //          priority 'AlwaysFront', the last received message shall replace the already existing message.
    //        type: string
    //        $ref: /display_message#/MessagePriorityEnum
    //      state:
    //        description: >-
    //          During what state should this message be shown. When omitted, this message should be shown in any state
    //          of the Charging Station
    //        type: string
    //        $ref: /display_message#/MessageStateEnum
    //      timestamp_from:
    //        description: >-
    //          From what date-time should this message be shown. If omitted: directly.
    //        type: string
    //        format: date-time
    //      timestamp_to:
    //        description: >-
    //          Until what date-time should this message be shown, after this date/time this message SHALL be removed.
    //          If omitted, message can be shown 'forever' (until it is specifically removed).
    //        type: string
    //        format: date-time
    //      identifier_id:
    //        description: >-
    //          To which user / during which session shall this message be shown. User can be identified by id token
    //          (when the session did not start yet), session id or transaction id. identifier_type will hold the type
    //          of identifier_id (default session id). Message SHALL be removed by the Charging Station after session
    //          has ended. If omitted, message is not tight to a session.
    //        type: string
    //        minLength: 0
    //        maxLength: 36
    //      identifier_type:
    //        description: >-
    //          The type of 'identifier_id'. If omitted, type will be session id.
    //        type: string
    //        $ref: /display_message#/IdentifierType
    //      message:
    //        type: object
    //        description: The message to show
    //        $ref: /text_message#/MessageContent
    //      qr_code:
    //        description: >-
    //          QR Code to scan for more information.
    //        type: string
    //    required:
    //      - message
    //
    //  MessageContent:
    //    description: Contains message details
    //    required:
    //      - content
    //    type: object
    //    properties:
    //      format:
    //        type: string
    //        $ref: /text_message#/MessageFormat
    //      language:
    //        type: string
    //      content:
    //        type: string
    //  MessageFormat:
    //    description: Format of the message to be displayed
    //    type: string
    //    enum:
    //      - ASCII
    //      - HTML
    //      - URI
    //      - UTF8
    //      - QRCODE

    const json cmd_response = R"({
        "messages": [
        {
            "id": 97524,
            "priority": "AlwaysFront",
            "state": "Charging",
            "message": {"content": "MyMessage", "language": "en.gb", "format": "ASCII"}
        }
    ]})"_json;
    interfaces->add_cmd_result(cmd_response);

    const auto response = ocpp->cb_get_display_message(request);

    ASSERT_EQ(received.size(), 1);
    EXPECT_EQ(received[0], R"({"request":{"id":[],"priority":"AlwaysFront","state":"Charging"}})"_json);

    // response is std::vector<ocpp::DisplayMessage>
    ASSERT_EQ(response.size(), 1);

    DisplayMessage expected_0{97524,
                              MessagePriorityEnum::AlwaysFront,
                              MessageStateEnum::Charging,
                              std::nullopt,
                              std::nullopt,
                              std::nullopt,
                              std::nullopt,
                              {"MyMessage", "en.gb", MessageFormatEnum::ASCII},
                              std::nullopt};

    EXPECT_EQ(response[0], expected_0);
}

TEST_F(GenericOcppRequiresTester, callClearDisplayMessage) {
    // call_clear_display_message() used in cb_get_display_message()

    using ocpp::v2::ClearDisplayMessageRequest;
    using ocpp::v2::ClearDisplayMessageResponse;
    using ocpp::v2::ClearMessageStatusEnum;

    std::vector<json> received;
    interfaces->subscribe_var("display_message", "call_clear_display_message",
                              [&received](const auto&, const auto&, const auto& data) { received.push_back(data); });

    const json cmd_response = R"({"status": "Accepted"})"_json;
    interfaces->add_cmd_result(cmd_response);

    ClearDisplayMessageRequest request;
    request.id = 15874;
    // std::optional<CustomData> customData;

    const auto response = ocpp->cb_clear_display_message(request);

    ASSERT_EQ(received.size(), 1);
    EXPECT_EQ(received[0], R"({"request":{"id":15874}})"_json);

    // response is ClearDisplayMessageResponse
    // ClearMessageStatusEnum status;
    // std::optional<StatusInfo> statusInfo;
    // std::optional<CustomData> customData;

    EXPECT_EQ(response.status, ClearMessageStatusEnum::Accepted);
}

} // namespace
