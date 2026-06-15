// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <utils/message_handler_scaling_policy.hpp>

#include <catch2/catch_test_macros.hpp>

#include <type_traits>

TEST_CASE("MessageHandler scaling policy matches CMake configuration", "[message_handler][scaling_policy]") {
    STATIC_REQUIRE(EVEREST_FRAMEWORK_THREAD_POOL_SCALING_POLICY ==
                   EVEREST_FRAMEWORK_EXPECTED_THREAD_POOL_SCALING_POLICY);
    STATIC_REQUIRE(EVEREST_FRAMEWORK_THREAD_POOL_SCALING_LATENCY_THRESHOLD_MS ==
                   EVEREST_FRAMEWORK_EXPECTED_THREAD_POOL_SCALING_LATENCY_THRESHOLD_MS);
    STATIC_REQUIRE(EVEREST_FRAMEWORK_THREAD_POOL_SCALING_LATENCY_TICK_MS ==
                   EVEREST_FRAMEWORK_EXPECTED_THREAD_POOL_SCALING_LATENCY_TICK_MS);
    STATIC_REQUIRE(EVEREST_FRAMEWORK_THREAD_POOL_SCALING_FIXED_SIZE_THRESHOLD ==
                   EVEREST_FRAMEWORK_EXPECTED_THREAD_POOL_SCALING_FIXED_SIZE_THRESHOLD);

#if EVEREST_FRAMEWORK_THREAD_POOL_SCALING_POLICY == EVEREST_FRAMEWORK_THREAD_POOL_SCALING_POLICY_CUSTOM
    STATIC_REQUIRE(std::is_same_v<Everest::detail::MessageHandlerScalingPolicy,
                                  EVEREST_FRAMEWORK_THREAD_POOL_SCALING_POLICY_CUSTOM_TYPE>);
#elif EVEREST_FRAMEWORK_THREAD_POOL_SCALING_POLICY == EVEREST_FRAMEWORK_THREAD_POOL_SCALING_POLICY_GREEDY
    STATIC_REQUIRE(std::is_same_v<Everest::detail::MessageHandlerScalingPolicy, everest::lib::util::GreedyScaling>);
#elif EVEREST_FRAMEWORK_THREAD_POOL_SCALING_POLICY == EVEREST_FRAMEWORK_THREAD_POOL_SCALING_POLICY_CONSERVATIVE
    STATIC_REQUIRE(
        std::is_same_v<Everest::detail::MessageHandlerScalingPolicy, everest::lib::util::ConservativeScaling>);
#elif EVEREST_FRAMEWORK_THREAD_POOL_SCALING_POLICY == EVEREST_FRAMEWORK_THREAD_POOL_SCALING_POLICY_FIXED_SIZE
    STATIC_REQUIRE(
        std::is_same_v<Everest::detail::MessageHandlerScalingPolicy, Everest::detail::MessageHandlerFixedSizeScaling>);
#else
    STATIC_REQUIRE(
        std::is_same_v<Everest::detail::MessageHandlerScalingPolicy, Everest::detail::MessageHandlerLatencyScaling>);
#endif
}
