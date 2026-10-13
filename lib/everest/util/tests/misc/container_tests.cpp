// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <everest/util/misc/container.hpp>

#include <gtest/gtest.h>

#include <set>
#include <vector>

namespace {

using namespace everest::lib::util;

TEST(container, exists_any_matches_any_argument) {
    std::set<int> const s{1, 2, 3};
    std::vector<int> const v{1, 2, 3};
    EXPECT_TRUE(exists_any(s, 3));
    EXPECT_TRUE(exists_any(s, 7, 8, 2));
    EXPECT_FALSE(exists_any(s, 7, 8));
    EXPECT_TRUE(exists_any(v, 7, 1));
    EXPECT_FALSE(exists_any(v, 7));
}

TEST(container, first_present_follows_argument_order) {
    std::set<int> const s{2, 3};
    std::vector<int> const v{2, 3};
    EXPECT_EQ(first_present(s, 1, 3, 2), std::optional<int>{3});
    EXPECT_EQ(first_present(s, 2, 3), std::optional<int>{2});
    EXPECT_EQ(first_present(s, 1), std::nullopt);
    EXPECT_EQ(first_present(s, 1, 4), std::nullopt);
    EXPECT_EQ(first_present(v, 1, 3, 2), std::optional<int>{3});
    EXPECT_EQ(first_present(v, 1), std::nullopt);
}

} // namespace
