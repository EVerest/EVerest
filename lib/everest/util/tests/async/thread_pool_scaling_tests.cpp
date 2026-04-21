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
    // This should spawn a new thread already.
    pool.run([&]() { second_task_started = true; });

    ASSERT_FALSE(second_task_started.load());

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
    constexpr int lhs = 10;
    constexpr int rhs = 32;
    auto fut = pool([](int first, int second) { return first + second; }, lhs, rhs);
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
    constexpr std::size_t max_threads = 20;
    constexpr int burst_tasks = 40;
    constexpr int trickle_tasks = 5;
    constexpr int tasks_per_iteration = burst_tasks + trickle_tasks;
    thread_pool_scaling<GreedyScaling> pool(1, max_threads, 20ms);
    std::atomic<int> completed_tasks{0};
    const int iterations = 30;

    for (int i = 0; i < iterations; ++i) {
        for (int j = 0; j < burst_tasks; ++j) {
            pool.run([&]() {
                std::this_thread::sleep_for(2ms);
                completed_tasks++;
            });
        }
        std::this_thread::sleep_for(30ms); // Allow some threads to start idling/retiring
        for (int j = 0; j < trickle_tasks; ++j) {
            pool.run([&]() { completed_tasks++; });
        }
    }

    auto start = std::chrono::steady_clock::now();
    while (completed_tasks < (iterations * tasks_per_iteration)) {
        std::this_thread::sleep_for(50ms);
        if (std::chrono::steady_clock::now() - start > 10s) {
            break;
        }
    }
    EXPECT_EQ(completed_tasks.load(), iterations * tasks_per_iteration);
}

// =================================================================
// 6. High Contention and Race Condition Tests
// =================================================================

/**
 * @test HighContentionProducers
 */
TEST(ThreadPoolScalingStressTest, HighContentionProducers) {
    constexpr int num_producers = 8;
    constexpr int tasks_per_producer = 2000;
    constexpr std::size_t max_threads = 16;
    thread_pool_scaling<LatencyScaling<5>> pool(4, max_threads, 1s);

    std::atomic<size_t> total_sum{0};
    std::vector<std::thread> producers;
    producers.reserve(static_cast<std::size_t>(num_producers));

    for (int prod = 0; prod < num_producers; ++prod) {
        producers.emplace_back([&]() {
            for (int i = 0; i < tasks_per_producer; ++i) {
                pool.run([&total_sum]() { total_sum.fetch_add(1, std::memory_order_relaxed); });
            }
        });
    }

    for (auto& thr : producers) {
        thr.join();
    }

    const auto expected = static_cast<std::size_t>(num_producers) * static_cast<std::size_t>(tasks_per_producer);
    auto start = std::chrono::steady_clock::now();
    while (total_sum.load() < expected) {
        std::this_thread::sleep_for(50ms);
        if (std::chrono::steady_clock::now() - start > 10s) {
            break;
        }
    }
    EXPECT_EQ(total_sum.load(), expected);
}

// =================================================================
// 7. Thread retirement versus pool destruction
// =================================================================

/**
 * @test DestructorVsActiveScalingRace
 */
TEST(ThreadPoolScalingStressTest, DestructorVsActiveScalingRace) {
    constexpr int repetitions = 50;
    constexpr std::size_t max_threads = 10;
    constexpr int tasks_per_rep = 20;
    for (int i = 0; i < repetitions; ++i) {
        {
            thread_pool_scaling<GreedyScaling> pool(1, max_threads, 5ms);
            for (int j = 0; j < tasks_per_rep; ++j) {
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

    for (auto& prom : promises) {
        prom.set_value();
    }

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
    constexpr int inner_tasks = 10;
    constexpr int total_tasks = inner_tasks + 1; // outer task + inner tasks
    thread_pool_scaling<LatencyScaling<10>> pool(1, 4, 1s);
    std::atomic<int> completed{0};

    pool.run([&]() {
        for (int i = 0; i < inner_tasks; ++i) {
            pool.run([&]() { completed++; });
        }
        completed++;
    });

    auto start = std::chrono::steady_clock::now();
    while (completed < total_tasks && std::chrono::steady_clock::now() - start < 2s) {
        std::this_thread::sleep_for(10ms);
    }
    EXPECT_EQ(completed.load(), total_tasks);
}

// =================================================================
// 10. Latency Boundary Check
// =================================================================

/**
 * @test LatencyThresholdBoundary
 */
TEST(ThreadPoolScalingTest, LatencyThresholdBoundary) {
    // 100 ms threshold: tasks that wait less than 100 ms should NOT trigger scaling
    constexpr std::size_t threshold_ms = 100;
    thread_pool_scaling<LatencyScaling<threshold_ms>> pool(1, 2, 1s);

    std::promise<void> block;
    auto fut = block.get_future().share();
    pool.run([fut]() { fut.wait(); });

    std::atomic<bool> task2_started{false};
    pool.run([&]() { task2_started = true; });

    std::this_thread::sleep_for(50ms);

    EXPECT_FALSE(task2_started.load());

    std::this_thread::sleep_for(100ms);

    auto start = std::chrono::steady_clock::now();
    while (!task2_started.load() && std::chrono::steady_clock::now() - start < 1s) {
        std::this_thread::sleep_for(10ms);
    }

    EXPECT_TRUE(task2_started.load());
    block.set_value();
}

// =================================================================
// 11. ConservativeScaling Policy Tests
// =================================================================

/**
 * @test ConservativeScalingDoesNotGrowBelowThreshold
 * @brief With 1 worker, queue_size must exceed workers*2 (>2) to trigger growth.
 *        At exactly 2 queued tasks the policy should NOT scale.
 */
TEST(ThreadPoolScalingTest, ConservativeScalingDoesNotGrowBelowThreshold) {
    // min=1, max=2 — second thread must NOT appear unless queue_size > 2
    thread_pool_scaling<ConservativeScaling> pool(1, 2, 1s);

    std::promise<void> block;
    auto fut = block.get_future().share();

    // Task 1: occupies the sole min thread
    pool.run([fut]() { fut.wait(); });

    // Task 2: queue_size after push == 2, workers == 1, 2 > (1*2) is false → no growth
    std::atomic<bool> task2_ran{false};
    pool.run([&]() { task2_ran = true; });

    std::this_thread::sleep_for(100ms);
    EXPECT_FALSE(task2_ran.load()); // still blocked behind task 1

    block.set_value();

    auto start = std::chrono::steady_clock::now();
    while (!task2_ran.load() && std::chrono::steady_clock::now() - start < 2s) {
        std::this_thread::sleep_for(10ms);
    }
    EXPECT_TRUE(task2_ran.load());
}

/**
 * @test ConservativeScalingGrowsAboveThreshold
 * @brief With 1 worker, submitting 3 tasks (queue_size==3 > workers*2==2) must trigger growth.
 */
TEST(ThreadPoolScalingTest, ConservativeScalingGrowsAboveThreshold) {
    thread_pool_scaling<ConservativeScaling> pool(1, 3, 1s);

    std::promise<void> block;
    auto fut = block.get_future().share();

    // Task 1: pins the min thread
    pool.run([fut]() { fut.wait(); });

    // Tasks 2 and 3: queue_size after task 3 == 3, workers == 1, 3 > 2 → growth
    std::atomic<int> ran{0};
    pool.run([&]() { ran++; });
    pool.run([&]() { ran++; });

    block.set_value();

    auto start = std::chrono::steady_clock::now();
    while (ran.load() < 2 && std::chrono::steady_clock::now() - start < 2s) {
        std::this_thread::sleep_for(10ms);
    }
    EXPECT_EQ(ran.load(), 2);
}

// =================================================================
// 12. FixedSizeScaling Policy Tests
// =================================================================

/**
 * @test FixedSizeScalingDoesNotGrowBeforeLimit
 * @brief Pool must not scale when queue_size is below the fixed limit.
 */
TEST(ThreadPoolScalingTest, FixedSizeScalingDoesNotGrowBeforeLimit) {
    // Limit=3: grows only when queue_size >= 3
    thread_pool_scaling<FixedSizeScaling<3>> pool(1, 2, 1s);

    std::promise<void> block;
    auto fut = block.get_future().share();

    pool.run([fut]() { fut.wait(); });

    // queue_size == 2 after this push, 2 < 3 → no growth
    std::atomic<bool> task2_ran{false};
    pool.run([&]() { task2_ran = true; });

    std::this_thread::sleep_for(100ms);
    EXPECT_FALSE(task2_ran.load());

    block.set_value();

    auto start = std::chrono::steady_clock::now();
    while (!task2_ran.load() && std::chrono::steady_clock::now() - start < 2s) {
        std::this_thread::sleep_for(10ms);
    }
    EXPECT_TRUE(task2_ran.load());
}

/**
 * @test FixedSizeScalingGrowsAtLimit
 * @brief Pool must spawn a new thread exactly when queue_size reaches the fixed limit.
 */
TEST(ThreadPoolScalingTest, FixedSizeScalingGrowsAtLimit) {
    // Limit=2: grows when queue_size >= 2
    thread_pool_scaling<FixedSizeScaling<2>> pool(1, 2, 1s);

    std::promise<void> block;
    auto fut = block.get_future().share();

    pool.run([fut]() { fut.wait(); });

    // queue_size == 2 after this push, 2 >= 2 → growth
    std::atomic<bool> task2_ran{false};
    pool.run([&]() { task2_ran = true; });

    auto start = std::chrono::steady_clock::now();
    while (!task2_ran.load() && std::chrono::steady_clock::now() - start < 2s) {
        std::this_thread::sleep_for(10ms);
    }
    EXPECT_TRUE(task2_ran.load());

    block.set_value();
}

// =================================================================
// 13. min == max Degenerate Case
// =================================================================

/**
 * @test FixedSizePoolNeverGrows
 * @brief When min == max the pool must never spawn additional threads regardless of backlog.
 */
TEST(ThreadPoolScalingTest, FixedSizePoolNeverGrows) {
    // min == max == 1: only ever one worker
    thread_pool_scaling<GreedyScaling> pool(1, 1, 1s);

    std::promise<void> block;
    auto fut = block.get_future().share();

    pool.run([fut]() { fut.wait(); });

    // Queue up several tasks; none can run until the first finishes
    std::atomic<int> ran{0};
    for (int i = 0; i < 4; ++i) {
        pool.run([&]() { ran++; });
    }

    std::this_thread::sleep_for(100ms);
    EXPECT_EQ(ran.load(), 0); // no second thread spawned → all queued behind task 1

    block.set_value();

    auto start = std::chrono::steady_clock::now();
    while (ran.load() < 4 && std::chrono::steady_clock::now() - start < 2s) {
        std::this_thread::sleep_for(10ms);
    }
    EXPECT_EQ(ran.load(), 4); // all tasks complete once the single worker is unblocked
}

// =================================================================
// 14. Zombie reaping correctness
// =================================================================

/**
 * @test ZombiesAreJoinedAfterRetirement
 * @brief Surplus threads that voluntarily retire must be fully joined — verified by
 *        the pool destructor completing without hanging or calling std::terminate().
 */
TEST(ThreadPoolScalingTest, ZombiesAreJoinedAfterRetirement) {
    // Short idle timeout so surplus threads retire quickly
    thread_pool_scaling<GreedyScaling> pool(1, 4, 20ms);

    std::promise<void> gate;
    auto fut = gate.get_future().share();

    // Flood the pool to force scale-up to max
    for (int i = 0; i < 4; ++i) {
        pool.run([fut]() { fut.wait(); });
    }
    gate.set_value();

    // Let all surplus threads go idle and retire into the zombie deque
    std::this_thread::sleep_for(200ms);

    // Destructor must complete cleanly: all zombies are joined before destruction
}

/**
 * @test ZombiesReapedConcurrentlyWithTaskExecution
 * @brief Zombies created during task execution must be reaped correctly
 *        by the worker loop while other tasks continue to execute.
 */
TEST(ThreadPoolScalingStressTest, ZombiesReapedConcurrentlyWithTaskExecution) {
    constexpr std::size_t max_threads = 8;
    constexpr int num_waves = 5;
    constexpr int tasks_per_wave = 10;
    constexpr int total_tasks = num_waves * tasks_per_wave;

    // Short timeout forces retirement while tasks keep arriving
    thread_pool_scaling<GreedyScaling> pool(1, max_threads, 10ms);
    std::atomic<int> completed{0};

    // Submit waves of tasks separated by the idle timeout to repeatedly
    // grow-then-shrink the pool, generating zombies during active execution
    for (int wave = 0; wave < num_waves; ++wave) {
        for (int i = 0; i < tasks_per_wave; ++i) {
            pool.run([&]() {
                std::this_thread::sleep_for(5ms);
                completed++;
            });
        }
        std::this_thread::sleep_for(15ms); // retire surplus threads between waves
    }

    auto start = std::chrono::steady_clock::now();
    while (completed.load() < total_tasks && std::chrono::steady_clock::now() - start < 5s) {
        std::this_thread::sleep_for(20ms);
    }
    EXPECT_EQ(completed.load(), total_tasks);
    // Destructor must complete cleanly with no unjoined zombie threads
}
