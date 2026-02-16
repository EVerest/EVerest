// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#include "gtest/gtest.h"
#include <atomic>
#include <chrono>
#include <everest/util/async/thread_pool_scaling.hpp>
#include <future>
#include <vector>

using namespace std::chrono_literals;
using namespace everest::lib::util;

// =================================================================
// 1. Latency Scaling Tests
// =================================================================

/**
 * @test ScalesOnLatencyThreshold
 * @brief Verifies that the pool spawns a new thread when a task waits too long.
 */
TEST(ThreadPoolScalingTest, ScalesOnLatencyThreshold) {
    thread_pool_scaling<LatencyScaling<10>> pool(1, 2, 1s);

    std::promise<void> block_first_task;
    std::shared_future<void> block_future = block_first_task.get_future();

    pool.run([block_future]() { block_future.wait(); });

    std::this_thread::sleep_for(5ms);

    std::atomic<bool> second_task_started{false};
    pool.run([&]() { second_task_started = true; });

    ASSERT_FALSE(second_task_started.load());

    std::this_thread::sleep_for(30ms);
    pool.run([]() {}); // Trigger scaling check

    std::this_thread::sleep_for(50ms);
    EXPECT_TRUE(second_task_started.load());

    block_first_task.set_value();
}

// =================================================================
// 2. Thread Retirement Tests
// =================================================================

/**
 * @test SurplusThreadsRetireAfterTimeout
 * @brief Verifies that threads exceeding the minimum count retire when idle.
 */
TEST(ThreadPoolScalingTest, SurplusThreadsRetireAfterTimeout) {
    thread_pool_scaling<GreedyScaling> pool(1, 2, 100ms);

    std::promise<void> p1, p2;
    auto f1 = p1.get_future().share();
    auto f2 = p2.get_future().share();

    pool.run([f1]() { f1.wait(); });
    pool.run([f2]() { f2.wait(); });

    p1.set_value();
    p2.set_value();

    std::this_thread::sleep_for(300ms);

    std::atomic<int> counter{0};
    for (int i = 0; i < 5; ++i)
        pool.run([&]() { counter++; });

    std::this_thread::sleep_for(50ms);
    EXPECT_EQ(counter.load(), 5);
}

// =================================================================
// 3. Backpressure Tests
// =================================================================

/**
 * @test BackpressureBlocksProducer
 * @brief Ensures the calling thread blocks when the queue limit is reached.
 */
TEST(ThreadPoolScalingTest, BackpressureBlocksProducer) {
    thread_pool_scaling<GreedyScaling> pool(1, 1, 1s, 1);

    std::promise<void> block;
    auto fut = block.get_future().share();

    pool.run([fut]() { fut.wait(); });
    pool.run([]() {});

    std::atomic<bool> producer_unblocked{false};
    std::thread producer([&]() {
        pool.run([]() {});
        producer_unblocked = true;
    });

    std::this_thread::sleep_for(100ms);
    EXPECT_FALSE(producer_unblocked.load());

    block.set_value();
    producer.join();
    EXPECT_TRUE(producer_unblocked.load());
}

// =================================================================
// 4. Future Interface Tests
// =================================================================

/**
 * @test OperatorReturnsValidFuture
 */
TEST(ThreadPoolScalingTest, OperatorReturnsValidFuture) {
    thread_pool_scaling<GreedyScaling> pool(1, 2, 1s);
    auto fut = pool([](int a, int b) { return a + b; }, 10, 32);
    EXPECT_EQ(fut.get(), 42);
}

// =================================================================
// 5. Scaling and Retirement Stress Tests
// =================================================================

/**
 * @test RapidScalingThrash
 * @brief Verifies stability during high-frequency fluctuations in workload.
 * @details Updated with more robust timing to handle OS scheduling jitter.
 */
TEST(ThreadPoolScalingStressTest, RapidScalingThrash) {
    thread_pool_scaling<GreedyScaling> pool(1, 20, 20ms);
    std::atomic<int> completed_tasks{0};
    const int iterations = 30;

    for (int i = 0; i < iterations; ++i) {
        for (int j = 0; j < 40; ++j) {
            pool.run([&]() {
                std::this_thread::sleep_for(2ms);
                completed_tasks++;
            });
        }
        std::this_thread::sleep_for(30ms); // Allow some threads to start idling/retiring
        for (int j = 0; j < 5; ++j) {
            pool.run([&]() { completed_tasks++; });
        }
    }

    auto start = std::chrono::steady_clock::now();
    while (completed_tasks < (iterations * 45)) {
        std::this_thread::sleep_for(50ms);
        if (std::chrono::steady_clock::now() - start > 10s)
            break;
    }
    EXPECT_EQ(completed_tasks.load(), iterations * 45);
}

// =================================================================
// 6. High Contention and Race Condition Tests
// =================================================================

/**
 * @test HighContentionProducers
 */
TEST(ThreadPoolScalingStressTest, HighContentionProducers) {
    const int num_producers = 8;
    const int tasks_per_producer = 2000;
    thread_pool_scaling<LatencyScaling<5>> pool(4, 16, 1s);

    std::atomic<size_t> total_sum{0};
    std::vector<std::thread> producers;

    for (int p = 0; p < num_producers; ++p) {
        producers.emplace_back([&]() {
            for (int i = 0; i < tasks_per_producer; ++i) {
                pool.run([&total_sum]() { total_sum.fetch_add(1, std::memory_order_relaxed); });
            }
        });
    }

    for (auto& t : producers) {
        t.join();
    }

    auto start = std::chrono::steady_clock::now();
    while (total_sum.load() < (num_producers * tasks_per_producer)) {
        std::this_thread::sleep_for(50ms);
        if (std::chrono::steady_clock::now() - start > 10s)
            break;
    }
    EXPECT_EQ(total_sum.load(), num_producers * tasks_per_producer);
}

// =================================================================
// 7. Thread retirement versus pool destruction
// =================================================================

/**
 * @test DestructorVsActiveScalingRace
 */
TEST(ThreadPoolScalingStressTest, DestructorVsActiveScalingRace) {
    for (int i = 0; i < 50; ++i) {
        {
            thread_pool_scaling<GreedyScaling> pool(1, 10, 5ms);
            for (int j = 0; j < 20; ++j) {
                pool.run([]() { std::this_thread::sleep_for(1ms); });
            }
            std::this_thread::sleep_for(6ms);
        }
    }
}

// =================================================================
// 8. Edge Case: Full Idle Reset
// =================================================================

/**
 * @test FullIdleResetToMinimum
 */
TEST(ThreadPoolScalingTest, FullIdleResetToMinimum) {
    const size_t min = 2;
    const size_t max = 5;
    const auto timeout = 50ms;
    thread_pool_scaling<GreedyScaling> pool(min, max, timeout);

    std::vector<std::promise<void>> promises(max);
    for (int i = 0; i < max; ++i) {
        pool.run([&promises, i]() { promises[i].get_future().wait(); });
    }

    for (auto& p : promises)
        p.set_value();

    std::this_thread::sleep_for(timeout * 3);

    std::atomic<bool> functional_check{false};
    pool.run([&]() { functional_check = true; });

    auto start = std::chrono::steady_clock::now();
    while (!functional_check.load() && std::chrono::steady_clock::now() - start < 1s) {
        std::this_thread::yield();
    }

    ASSERT_TRUE(functional_check.load());
}

// =================================================================
// 9. Re-entrancy and Policy Boundary Tests
// =================================================================

/**
 * @test ReentrantScaling
 */
TEST(ThreadPoolScalingStressTest, ReentrantScaling) {
    thread_pool_scaling<LatencyScaling<10>> pool(1, 4, 1s);
    std::atomic<int> completed{0};

    pool.run([&]() {
        for (int i = 0; i < 10; ++i) {
            pool.run([&]() { completed++; });
        }
        completed++;
    });

    auto start = std::chrono::steady_clock::now();
    while (completed < 11 && std::chrono::steady_clock::now() - start < 2s) {
        std::this_thread::sleep_for(10ms);
    }
    EXPECT_EQ(completed.load(), 11);
}

// =================================================================
// 10. Latency Boundary Check
// =================================================================

/**
 * @test LatencyThresholdBoundary
 */
TEST(ThreadPoolScalingTest, LatencyThresholdBoundary) {
    thread_pool_scaling<LatencyScaling<100>> pool(1, 2, 1s);

    std::promise<void> block;
    auto f = block.get_future().share();
    pool.run([f]() { f.wait(); });

    std::atomic<bool> task2_started{false};
    pool.run([&]() { task2_started = true; });

    std::this_thread::sleep_for(50ms);
    pool.run([]() {});

    EXPECT_FALSE(task2_started.load());

    std::this_thread::sleep_for(100ms);
    pool.run([]() {});

    auto start = std::chrono::steady_clock::now();
    while (!task2_started.load() && std::chrono::steady_clock::now() - start < 1s) {
        std::this_thread::sleep_for(10ms);
    }

    EXPECT_TRUE(task2_started.load());
    block.set_value();
}
