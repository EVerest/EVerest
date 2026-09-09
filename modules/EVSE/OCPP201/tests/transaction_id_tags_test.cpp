// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gtest/gtest.h>

#include <transaction_id_tags.hpp>

namespace {
using module::TransactionIdTags;

types::authorization::ProvidedIdToken id_tag(const std::string& value,
                                             const types::authorization::AuthorizationType authorization_type =
                                                 types::authorization::AuthorizationType::BankCard) {
    types::authorization::ProvidedIdToken token;
    token.id_token.value = value;
    token.id_token.type = types::authorization::IdTokenType::Local;
    token.authorization_type = authorization_type;
    return token;
}

std::optional<std::string> pop_value(TransactionIdTags& tags, const std::string& transaction_id) {
    const auto token = tags.pop(transaction_id);
    if (!token.has_value()) {
        return std::nullopt;
    }
    return token->id_token.value;
}

TEST(TransactionIdTags, unknown_transaction_has_no_id_tag) {
    TransactionIdTags tags;
    EXPECT_EQ(pop_value(tags, "tx1"), std::nullopt);
}

TEST(TransactionIdTags, pop_returns_the_id_tag_once) {
    TransactionIdTags tags;
    tags.push("tx1", id_tag("card"));

    const auto token = tags.pop("tx1");
    ASSERT_TRUE(token.has_value());
    EXPECT_EQ(token->id_token.value, "card");
    EXPECT_EQ(token->authorization_type, types::authorization::AuthorizationType::BankCard);
    EXPECT_EQ(pop_value(tags, "tx1"), std::nullopt);
}

TEST(TransactionIdTags, only_bank_cards_are_remembered) {
    TransactionIdTags tags;
    tags.push("rfid", id_tag("rfid", types::authorization::AuthorizationType::RFID));
    tags.push("ocpp", id_tag("remote", types::authorization::AuthorizationType::OCPP));

    EXPECT_EQ(pop_value(tags, "rfid"), std::nullopt);
    EXPECT_EQ(pop_value(tags, "ocpp"), std::nullopt);
}

TEST(TransactionIdTags, pushing_a_transaction_again_keeps_a_single_entry) {
    TransactionIdTags tags;
    tags.push("tx1", id_tag("first"));
    tags.push("tx1", id_tag("second"));

    EXPECT_EQ(pop_value(tags, "tx1"), "first");
    EXPECT_EQ(pop_value(tags, "tx1"), std::nullopt);
}

TEST(TransactionIdTags, transactions_are_kept_until_popped) {
    TransactionIdTags tags;
    tags.push("pending", id_tag("offline card"));
    for (std::size_t i = 0; i < TransactionIdTags::MAX_TRANSACTIONS - 1; i++) {
        tags.push("tx" + std::to_string(i), id_tag("card"));
    }

    // The bound is not reached yet: the oldest transaction is still known.
    EXPECT_EQ(pop_value(tags, "pending"), "offline card");
}

TEST(TransactionIdTags, the_oldest_transaction_is_dropped_when_the_bound_is_reached) {
    TransactionIdTags tags;
    tags.push("oldest", id_tag("a"));
    tags.push("second", id_tag("b"));
    for (std::size_t i = 0; i < TransactionIdTags::MAX_TRANSACTIONS - 1; i++) {
        tags.push("tx" + std::to_string(i), id_tag("card"));
    }

    EXPECT_EQ(pop_value(tags, "oldest"), std::nullopt);
    EXPECT_EQ(pop_value(tags, "second"), "b");
    EXPECT_EQ(pop_value(tags, "tx" + std::to_string(TransactionIdTags::MAX_TRANSACTIONS - 2)), "card");
}

} // namespace
