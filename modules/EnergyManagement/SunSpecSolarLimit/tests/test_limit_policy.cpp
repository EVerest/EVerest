// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <gtest/gtest.h>

#include <sunspec/limit_policy.hpp>

#include <chrono>

namespace {

using namespace std::chrono_literals;
using sunspec_solar::LimitPolicy;
using sunspec_solar::PolicyConfig;

const LimitPolicy::Clock::time_point T0{};

PolicyConfig config() {
    PolicyConfig c;
    c.reserve_W = 500.0;
    c.fallback_power_W = 1400.0;
    c.stale_timeout = 30s;
    c.resend_interval = 10s;
    return c;
}

TEST(LimitPolicy, FallbackBeforeFirstReading) {
    const LimitPolicy policy(config());

    EXPECT_DOUBLE_EQ(policy.current_limit_W(T0), 1400.0);
}

TEST(LimitPolicy, LimitIsProductionMinusReserve) {
    LimitPolicy policy(config());

    policy.on_production(4000.0, T0);

    EXPECT_DOUBLE_EQ(policy.current_limit_W(T0 + 1s), 3500.0);
}

TEST(LimitPolicy, LimitNeverBelowZero) {
    LimitPolicy policy(config());

    policy.on_production(300.0, T0);
    EXPECT_DOUBLE_EQ(policy.current_limit_W(T0), 0.0);

    policy.on_production(-50.0, T0);
    EXPECT_DOUBLE_EQ(policy.current_limit_W(T0), 0.0);
}

TEST(LimitPolicy, FallbackOnceReadingIsStale) {
    LimitPolicy policy(config());
    policy.on_production(4000.0, T0);

    EXPECT_DOUBLE_EQ(policy.current_limit_W(T0 + 30s), 3500.0);
    EXPECT_DOUBLE_EQ(policy.current_limit_W(T0 + 30s + 1ms), 1400.0);
}

TEST(LimitPolicy, NewReadingEndsFallback) {
    LimitPolicy policy(config());
    policy.on_production(4000.0, T0);

    policy.on_production(2000.0, T0 + 60s);

    EXPECT_DOUBLE_EQ(policy.current_limit_W(T0 + 60s), 1500.0);
}

TEST(LimitPolicy, SendsFirstLimitThenOnlyChanges) {
    LimitPolicy policy(config());

    EXPECT_EQ(policy.limit_to_send(T0), 1400.0);
    EXPECT_FALSE(policy.limit_to_send(T0 + 1s).has_value());

    policy.on_production(4000.0, T0 + 2s);
    EXPECT_EQ(policy.limit_to_send(T0 + 2s), 3500.0);

    policy.on_production(4000.4, T0 + 3s);
    EXPECT_FALSE(policy.limit_to_send(T0 + 3s).has_value());
}

TEST(LimitPolicy, ResendsUnchangedLimitAfterInterval) {
    LimitPolicy policy(config());
    policy.on_production(4000.0, T0);
    ASSERT_TRUE(policy.limit_to_send(T0).has_value());

    policy.on_production(4000.0, T0 + 9s);
    EXPECT_FALSE(policy.limit_to_send(T0 + 9s).has_value());

    policy.on_production(4000.0, T0 + 10s);
    EXPECT_EQ(policy.limit_to_send(T0 + 10s), 3500.0);
}

TEST(LimitPolicy, SendsFallbackWhenReadingGoesStale) {
    LimitPolicy policy(config());
    policy.on_production(4000.0, T0);
    ASSERT_EQ(policy.limit_to_send(T0), 3500.0);

    EXPECT_EQ(policy.limit_to_send(T0 + 31s), 1400.0);
}

} // namespace
