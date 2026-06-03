// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <everest/compile_time_settings.hpp>
#include <everest/util/async/thread_pool_scaling.hpp>

namespace Everest::detail {

constexpr std::size_t MESSAGE_HANDLER_THREAD_POOL_SCALING_LATENCY_THRESHOLD_MS =
    EVEREST_FRAMEWORK_THREAD_POOL_SCALING_LATENCY_THRESHOLD_MS;
constexpr std::size_t MESSAGE_HANDLER_THREAD_POOL_SCALING_LATENCY_TICK_MS =
    EVEREST_FRAMEWORK_THREAD_POOL_SCALING_LATENCY_TICK_MS;
constexpr std::size_t MESSAGE_HANDLER_THREAD_POOL_SCALING_FIXED_SIZE_THRESHOLD =
    EVEREST_FRAMEWORK_THREAD_POOL_SCALING_FIXED_SIZE_THRESHOLD;

using MessageHandlerLatencyScaling =
    everest::lib::util::LatencyScaling<MESSAGE_HANDLER_THREAD_POOL_SCALING_LATENCY_THRESHOLD_MS,
                                        MESSAGE_HANDLER_THREAD_POOL_SCALING_LATENCY_TICK_MS>;
using MessageHandlerFixedSizeScaling =
    everest::lib::util::FixedSizeScaling<MESSAGE_HANDLER_THREAD_POOL_SCALING_FIXED_SIZE_THRESHOLD>;

#if EVEREST_FRAMEWORK_THREAD_POOL_SCALING_POLICY == EVEREST_FRAMEWORK_THREAD_POOL_SCALING_POLICY_CUSTOM
using MessageHandlerScalingPolicy = EVEREST_FRAMEWORK_THREAD_POOL_SCALING_POLICY_CUSTOM_TYPE;
#elif EVEREST_FRAMEWORK_THREAD_POOL_SCALING_POLICY == EVEREST_FRAMEWORK_THREAD_POOL_SCALING_POLICY_GREEDY
using MessageHandlerScalingPolicy = everest::lib::util::GreedyScaling;
#elif EVEREST_FRAMEWORK_THREAD_POOL_SCALING_POLICY == EVEREST_FRAMEWORK_THREAD_POOL_SCALING_POLICY_CONSERVATIVE
using MessageHandlerScalingPolicy = everest::lib::util::ConservativeScaling;
#elif EVEREST_FRAMEWORK_THREAD_POOL_SCALING_POLICY == EVEREST_FRAMEWORK_THREAD_POOL_SCALING_POLICY_FIXED_SIZE
using MessageHandlerScalingPolicy = MessageHandlerFixedSizeScaling;
#else
using MessageHandlerScalingPolicy = MessageHandlerLatencyScaling;
#endif

} // namespace Everest::detail
