// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <algorithm>
#include <optional>

#include <gtest/gtest.h>

#include <storage/record_conversions.hpp>

namespace {

using module::storage::compute_id_token_hash;
using module::storage::generate_id_token_hash;
using module::storage::strip_id_tag;

types::authorization::IdToken make_id_token(const std::string& value, types::authorization::IdTokenType type) {
    types::authorization::IdToken id_token{};
    id_token.value = value;
    id_token.type = type;
    return id_token;
}

types::authorization::ProvidedIdToken make_provided_id_token() {
    types::authorization::ProvidedIdToken provided_id_token{};
    provided_id_token.id_token = make_id_token("ABAD1DEA", types::authorization::IdTokenType::ISO14443);
    provided_id_token.authorization_type = types::authorization::AuthorizationType::RFID;
    return provided_id_token;
}

types::session_cost::SessionCost make_session_cost() {
    types::money::Currency currency{};
    currency.code = types::money::CurrencyCode::EUR;
    currency.decimals = 2;

    types::session_cost::SessionCostChunk chunk{};
    chunk.timestamp_from = "2026-08-21T10:00:00Z";
    chunk.timestamp_to = "2026-08-21T11:00:00Z";
    chunk.cost = types::money::MoneyAmount{1234};
    chunk.category = types::session_cost::CostCategory::Energy;

    types::session_cost::SessionCost cost{};
    cost.session_id = "session-1";
    cost.currency = currency;
    cost.status = types::session_cost::SessionStatus::Finished;
    cost.id_tag = make_provided_id_token();
    cost.cost_chunks = std::vector<types::session_cost::SessionCostChunk>{chunk};
    return cost;
}

TEST(RecordConversionsTest, strip_id_tag_clears_the_id_tag) {
    const auto stripped = strip_id_tag(make_session_cost());
    EXPECT_FALSE(stripped.id_tag.has_value());
}

TEST(RecordConversionsTest, strip_id_tag_preserves_everything_else) {
    const auto cost = make_session_cost();
    const auto stripped = strip_id_tag(cost);

    EXPECT_EQ(stripped.session_id, cost.session_id);
    EXPECT_EQ(stripped.currency, cost.currency);
    EXPECT_EQ(stripped.status, cost.status);
    ASSERT_TRUE(stripped.cost_chunks.has_value());
    EXPECT_EQ(stripped.cost_chunks, cost.cost_chunks);
}

// The expected values are the hashes libocpp stores in its authorization cache for the same tokens
TEST(RecordConversionsTest, generate_id_token_hash_central_token) {
    const auto id_token = make_id_token("valid", types::authorization::IdTokenType::Central);
    EXPECT_EQ(generate_id_token_hash(id_token), "63f3202a9c2e08a033a861481c6259e7a70a2b6e243f91233ebf26f33859c113");
}

TEST(RecordConversionsTest, generate_id_token_hash_iso14443_token) {
    const auto id_token = make_id_token("ABAD1DEA", types::authorization::IdTokenType::ISO14443);
    EXPECT_EQ(generate_id_token_hash(id_token), "1cc0ce8b95f44d43273c46a062af3d15a06e3d2170909b1fdebd634027aebef1");
}

TEST(RecordConversionsTest, generate_id_token_hash_depends_on_token_type) {
    const auto iso14443 = make_id_token("ABAD1DEA", types::authorization::IdTokenType::ISO14443);
    const auto iso15693 = make_id_token("ABAD1DEA", types::authorization::IdTokenType::ISO15693);
    EXPECT_NE(generate_id_token_hash(iso14443), generate_id_token_hash(iso15693));
}

TEST(RecordConversionsTest, generate_id_token_hash_is_lower_case_hex_of_64_characters) {
    const auto hash = generate_id_token_hash(make_id_token("DEADBEEF", types::authorization::IdTokenType::KeyCode));
    EXPECT_EQ(hash.size(), 64);
    EXPECT_TRUE(std::all_of(hash.begin(), hash.end(), [](const char character) {
        return (character >= '0' and character <= '9') or (character >= 'a' and character <= 'f');
    }));
}

TEST(RecordConversionsTest, compute_id_token_hash_hashes_the_inner_token) {
    const auto provided_id_token = make_provided_id_token();
    const auto hash = compute_id_token_hash(provided_id_token);

    ASSERT_TRUE(hash.has_value());
    EXPECT_EQ(hash.value(), generate_id_token_hash(provided_id_token.id_token));
}

TEST(RecordConversionsTest, compute_id_token_hash_returns_a_full_length_hash) {
    const auto hash = compute_id_token_hash(make_provided_id_token());

    ASSERT_TRUE(hash.has_value());
    EXPECT_EQ(hash->size(), 64);
}

} // namespace
