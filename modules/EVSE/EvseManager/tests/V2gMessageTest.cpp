// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "v2gMessage.hpp"

#include <cstddef>
#include <string>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

namespace module {
namespace {

std::string nested_arrays(std::size_t depth) {
    std::string json(depth, '[');
    json.append(depth, ']');
    return json;
}

} // namespace

TEST(V2gMessageTest, FromJsonAcceptsValidJson) {
    v2g_message v2g;
    ASSERT_TRUE(v2g.from_json(R"({"V2G_Message":{"Header":{"SessionID":"0102"}}})"));
    EXPECT_EQ(nlohmann::json::parse(v2g.to_json()),
              nlohmann::json::parse(R"({"V2G_Message":{"Header":{"SessionID":"0102"}}})"));
}

TEST(V2gMessageTest, FromJsonRejectsMalformedJson) {
    v2g_message v2g;
    EXPECT_FALSE(v2g.from_json(R"({"V2G_Message":{"Header":)"));
    EXPECT_FALSE(v2g.from_json("not json"));
    EXPECT_FALSE(v2g.from_json("\"\xff\""));
    EXPECT_EQ(v2g.to_json(), "null");
}

TEST(V2gMessageTest, FromJsonNestingLimitIsInclusive) {
    v2g_message v2g;
    EXPECT_TRUE(v2g.from_json(nested_arrays(128)));
    EXPECT_FALSE(v2g.from_json(nested_arrays(129)));
}

TEST(V2gMessageTest, FromJsonRejectsDeepNesting) {
    v2g_message v2g;
    EXPECT_FALSE(v2g.from_json(nested_arrays(100000)));
    EXPECT_EQ(v2g.to_json(), "null");
}

} // namespace module
