// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gtest/gtest.h>

#include <chrono>

#include "random_delay/random_delay_duration.hpp"

namespace module {

using std::chrono::seconds;

TEST(RandomDelayDuration, stays_below_max_duration) {
    EXPECT_EQ(random_delay_duration(seconds(600), 0), seconds(0));
    EXPECT_EQ(random_delay_duration(seconds(600), 599), seconds(599));
    EXPECT_EQ(random_delay_duration(seconds(600), 600), seconds(0));
    EXPECT_EQ(random_delay_duration(seconds(600), 1234), seconds(34));
}

TEST(RandomDelayDuration, no_delay_without_positive_max_duration) {
    EXPECT_EQ(random_delay_duration(seconds(0), 1234), seconds(0));
    EXPECT_EQ(random_delay_duration(seconds(-5), 1234), seconds(0));
}

} // namespace module
