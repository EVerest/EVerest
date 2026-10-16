// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gtest/gtest.h>

#include <ocpp/v2/message_permission.hpp>

namespace ocpp {
namespace v2 {

TEST(MessagePermissionTest, an_empty_list_grants_nothing) {
    // Default deny: a connection with no allow list may be sent nothing.
    EXPECT_TRUE(parse_message_permissions("").empty());
    EXPECT_TRUE(parse_message_permissions("   ").empty());
    EXPECT_TRUE(parse_message_permissions(",,").empty());
}

TEST(MessagePermissionTest, parses_the_message_set_of_the_hems_use_case) {
    // Application note table 2.
    const auto permissions = parse_message_permissions("TransactionEvent,MeterValues");

    EXPECT_EQ(permissions.size(), 2u);
    EXPECT_EQ(permissions.count(MessageType::TransactionEvent), 1u);
    EXPECT_EQ(permissions.count(MessageType::MeterValues), 1u);
}

TEST(MessagePermissionTest, parses_the_message_set_of_the_hems_downstream_use_case) {
    // Application note table 1.
    const auto permissions = parse_message_permissions("SetChargingProfile,ClearChargingProfile,GetCompositeSchedule");

    EXPECT_EQ(permissions.size(), 3u);
    EXPECT_EQ(permissions.count(MessageType::SetChargingProfile), 1u);
    EXPECT_EQ(permissions.count(MessageType::ClearChargingProfile), 1u);
    EXPECT_EQ(permissions.count(MessageType::GetCompositeSchedule), 1u);
}

TEST(MessagePermissionTest, surrounding_whitespace_is_ignored) {
    // The application note writes these lists with spaces after the commas.
    const auto permissions = parse_message_permissions(" TransactionEvent , MeterValues ");

    EXPECT_EQ(permissions.size(), 2u);
    EXPECT_EQ(permissions.count(MessageType::TransactionEvent), 1u);
    EXPECT_EQ(permissions.count(MessageType::MeterValues), 1u);
}

TEST(MessagePermissionTest, an_unknown_name_does_not_void_the_rest_of_the_list) {
    // A typo in a list written by a CSMS must not silently grant or revoke everything else.
    const auto permissions = parse_message_permissions("MeterValues,NotAnOcppMessage,TransactionEvent");

    EXPECT_EQ(permissions.size(), 2u);
    EXPECT_EQ(permissions.count(MessageType::MeterValues), 1u);
    EXPECT_EQ(permissions.count(MessageType::TransactionEvent), 1u);
}

TEST(MessagePermissionTest, names_carry_no_request_or_response_suffix) {
    // Section 3.5: the name is the Action of the CALL, so the suffixed spelling is not a message
    // name and is rejected like any other unknown token.
    EXPECT_TRUE(parse_message_permissions("TransactionEventRequest").empty());
}

TEST(MessagePermissionTest, a_repeated_name_is_harmless) {
    const auto permissions = parse_message_permissions("MeterValues,MeterValues");
    EXPECT_EQ(permissions.size(), 1u);
}

} // namespace v2
} // namespace ocpp
