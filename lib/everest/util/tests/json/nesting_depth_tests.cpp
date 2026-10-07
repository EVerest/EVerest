// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <everest/util/json/nesting_depth.hpp>

#include <gtest/gtest.h>

#include <string>

namespace {

using everest::lib::util::exceeds_json_nesting_depth;

std::string nested_arrays(std::size_t depth) {
    return std::string(depth, '[') + std::string(depth, ']');
}

TEST(nesting_depth, counts_array_and_object_nesting) {
    EXPECT_FALSE(exceeds_json_nesting_depth("", 2));
    EXPECT_FALSE(exceeds_json_nesting_depth("42", 0));
    EXPECT_FALSE(exceeds_json_nesting_depth(R"({"a":[1,2],"b":{"c":3}})", 2));
    EXPECT_TRUE(exceeds_json_nesting_depth(R"({"a":[1,2],"b":{"c":[3]}})", 2));
}

TEST(nesting_depth, limit_is_inclusive) {
    EXPECT_FALSE(exceeds_json_nesting_depth(nested_arrays(128), 128));
    EXPECT_TRUE(exceeds_json_nesting_depth(nested_arrays(129), 128));
    EXPECT_TRUE(exceeds_json_nesting_depth(nested_arrays(1000000), 128));
}

TEST(nesting_depth, ignores_brackets_inside_strings) {
    EXPECT_FALSE(exceeds_json_nesting_depth(R"({"key":"[[[{{{"})", 1));
    EXPECT_FALSE(exceeds_json_nesting_depth(R"(["escaped \" quote [[["])", 1));
    EXPECT_FALSE(exceeds_json_nesting_depth(R"(["backslash at end \\", "[[["])", 1));
    EXPECT_TRUE(exceeds_json_nesting_depth(R"(["backslash at end \\", [[1]]])", 2));
}

} // namespace
