// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#include "gtest/gtest.h"
#include <atomic>
#include <chrono>
#include <ctime>
#include <everest/util/async/thread_pool_scaling.hpp>
#include <future>
#include <mutex>
#include <random>
#include <stdexcept>
#include <utility>
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

    // Wait until the sole min thread is confirmed to be inside the blocking task. Only then is
    // the queue guaranteed to still hold everything pushed below, so the depth the policy sees
    // is deterministic instead of a race against that thread popping.
    std::atomic<bool> blocker_running{false};
    pool.run([fut, &blocker_running]() {
        blocker_running = true;
        fut.wait();
    });

    auto start = std::chrono::steady_clock::now();
    while (!blocker_running.load() && std::chrono::steady_clock::now() - start < 2s) {
        std::this_thread::sleep_for(1ms);
    }
    ASSERT_TRUE(blocker_running.load());

    // queue_size == 1 after this push, 1 < 2 → no growth
    std::atomic<int> ran{0};
    pool.run([&]() { ran++; });

    std::this_thread::sleep_for(100ms);
    EXPECT_EQ(ran.load(), 0);

    // queue_size == 2 after this push, 2 >= 2 → growth
    pool.run([&]() { ran++; });

    start = std::chrono::steady_clock::now();
    while (ran.load() < 2 && std::chrono::steady_clock::now() - start < 2s) {
        std::this_thread::sleep_for(10ms);
    }
    EXPECT_EQ(ran.load(), 2);

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

namespace {
// The only worker blocks until a second task runs; that task is queued once and nothing else is submitted, so only
// the supervisor can add the worker that resolves the dependency.
bool dependent_task_runs_without_further_submissions(thread_pool_scaling<LatencyScaling<20, 5>>& pool) {
    std::promise<void> second_ran;
    auto second_ran_future = second_ran.get_future().share();
    std::promise<void> first_done;
    auto first_done_future = first_done.get_future();
    pool.run([second_ran_future, &first_done] {
        const auto status = second_ran_future.wait_for(2s);
        first_done.set_value();
        (void)status;
    });
    std::this_thread::sleep_for(10ms);
    pool.run([&second_ran] { second_ran.set_value(); });
    const bool ran = second_ran_future.wait_for(1s) == std::future_status::ready;
    first_done_future.wait();
    return ran;
}
} // namespace

/**
 * @test SupervisorResolvesBlockedWorkerWithoutNewSubmissions
 * @brief A task queued behind a blocked worker runs although no further task is submitted.
 */
TEST(ThreadPoolScalingTest, SupervisorResolvesBlockedWorkerWithoutNewSubmissions) {
    thread_pool_scaling<LatencyScaling<20, 5>> pool(1, 4, 5s);
    EXPECT_TRUE(dependent_task_runs_without_further_submissions(pool));
}

/**
 * @test SupervisorWakesFromIdle
 * @brief The supervisor sleeps while the queue is empty and still resolves a blocked worker once work arrives.
 * The idle timeout is short so that the worker added in the first round has retired before the second round;
 * otherwise that worker would take the dependent task and the supervisor would not be involved again.
 */
TEST(ThreadPoolScalingTest, SupervisorWakesFromIdle) {
    thread_pool_scaling<LatencyScaling<20, 5>> pool(1, 4, 50ms);
    std::this_thread::sleep_for(200ms);
    EXPECT_TRUE(dependent_task_runs_without_further_submissions(pool));
    std::this_thread::sleep_for(300ms);
    EXPECT_TRUE(dependent_task_runs_without_further_submissions(pool));
}

/**
 * @test SupervisorWokenForTaskQueuedBehindJustPoppedTask
 * @brief A task submitted in the moment the only worker has popped the previous task, but not yet started it, must
 * still reach the supervisor. The submission gap is swept over the first microseconds after the first task so that
 * the pop of that task and the second submission interleave; a pool that counts the popping worker as idle leaves
 * the second task unattended until the first one gives up. The interleaving needs a second core, so this test
 * cannot detect the regression on a single core; ThreadSafeBoundedQueueTest.WaitingConsumersExcludePoppedConsumer
 * pins the counting deterministically.
 */
TEST(ThreadPoolScalingTest, SupervisorWokenForTaskQueuedBehindJustPoppedTask) {
    std::mt19937 rng(1);
    for (int i = 0; i < 200; ++i) {
        std::promise<void> second_ran;
        auto second_ran_future = second_ran.get_future().share();
        std::promise<void> first_done;
        auto first_done_future = first_done.get_future();
        thread_pool_scaling<LatencyScaling<2, 1>> pool(1, 4, 60s);
        std::this_thread::sleep_for(1ms);
        pool.run([second_ran_future, &first_done] {
            (void)second_ran_future.wait_for(300ms);
            first_done.set_value();
        });
        // sleep_for cannot produce gaps in the microsecond range, hence the busy wait
        const auto gap = std::chrono::nanoseconds(rng() % 50000);
        const auto start = std::chrono::steady_clock::now();
        while (std::chrono::steady_clock::now() - start < gap) {
        }
        pool.run([&second_ran] { second_ran.set_value(); });
        const bool resolved = second_ran_future.wait_for(200ms) == std::future_status::ready;
        EXPECT_TRUE(resolved) << "iteration " << i << ", gap " << gap.count() << " ns";
        first_done_future.wait();
        if (not resolved) {
            break;
        }
    }
}

namespace {
// LatencyScaling that records when it decided to grow.
struct RecordingLatencyScaling : LatencyScaling<20, 5> {
    static inline std::mutex mutex;
    static inline std::vector<std::chrono::steady_clock::time_point> grow_decisions;
    static bool should_grow(std::size_t current_workers, std::size_t queue_size,
                            std::optional<std::chrono::steady_clock::time_point> oldest_arrival) {
        const bool grow = LatencyScaling<20, 5>::should_grow(current_workers, queue_size, oldest_arrival);
        if (grow) {
            std::lock_guard lock(mutex);
            grow_decisions.push_back(std::chrono::steady_clock::now());
        }
        return grow;
    }
};
} // namespace

/**
 * @test SupervisorBacksOffAfterSpawning
 * @brief After adding a worker for an overdue task the supervisor waits at least one tick before it evaluates the
 * same task again, since the new worker needs a moment to pop it. Without the back-off it spawns once per loop until
 * the task is gone or the limit is reached.
 */
TEST(ThreadPoolScalingTest, SupervisorBacksOffAfterSpawning) {
    {
        std::lock_guard lock(RecordingLatencyScaling::mutex);
        RecordingLatencyScaling::grow_decisions.clear();
    }
    std::promise<void> release;
    auto released = release.get_future().share();
    std::promise<void> second_ran;
    auto second_ran_future = second_ran.get_future();
    thread_pool_scaling<RecordingLatencyScaling> pool(1, 16, 60s);
    pool.run([released] { released.wait(); });
    // the second task is submitted well within the threshold, so only the supervisor decides to grow for it
    std::this_thread::sleep_for(2ms);
    pool.run([&second_ran] { second_ran.set_value(); });
    EXPECT_EQ(second_ran_future.wait_for(1s), std::future_status::ready);
    release.set_value();

    std::lock_guard lock(RecordingLatencyScaling::mutex);
    const auto& decisions = RecordingLatencyScaling::grow_decisions;
    ASSERT_GE(decisions.size(), 1u);
    for (std::size_t i = 1; i < decisions.size(); ++i) {
        EXPECT_GE(decisions[i] - decisions[i - 1], 5ms) << "grow decisions " << i - 1 << " and " << i;
    }
}

/**
 * @test SupervisorDoesNotSpinAtThreadLimit
 * @brief With every worker blocked at the thread limit and a task overdue, the supervisor sleeps until a worker
 * retires instead of spinning on a deadline in the past.
 */
TEST(ThreadPoolScalingTest, SupervisorDoesNotSpinAtThreadLimit) {
    thread_pool_scaling<LatencyScaling<5, 5>> pool(1, 1, 5s);
    std::promise<void> release;
    auto released = release.get_future().share();
    std::atomic<bool> second_ran{false};
    pool.run([released] { released.wait(); });
    pool.run([&second_ran] { second_ran = true; });

    const auto cpu_before = std::clock();
    std::this_thread::sleep_for(300ms);
    const auto cpu_ms = 1000.0 * static_cast<double>(std::clock() - cpu_before) / CLOCKS_PER_SEC;
    EXPECT_LT(cpu_ms, 50.0);
    EXPECT_FALSE(second_ran);

    release.set_value();
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (not second_ran and std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(1ms);
    }
    EXPECT_TRUE(second_ran);
}

// =================================================================
// 4. Shutdown Tests
// =================================================================

/**
 * @test DestructorDrainsDependencyOfRunningTask
 * @brief A task that waits for a second, already accepted task must not be stranded by destruction: the pool keeps
 * scaling for accepted tasks while it drains, so the second task gets a worker of its own.
 */
TEST(ThreadPoolScalingShutdownTest, DestructorDrainsDependencyOfRunningTask) {
    std::promise<void> release;
    auto released = release.get_future().share();
    std::promise<void> entered;
    auto entered_future = entered.get_future();
    std::atomic<bool> timed_out{false};
    const auto start = std::chrono::steady_clock::now();
    {
        thread_pool_scaling<LatencyScaling<20, 5>> pool(1, 4, 60s);
        pool.run([&] {
            entered.set_value();
            timed_out = released.wait_for(2s) != std::future_status::ready;
        });
        entered_future.wait();
        pool.run([&release] { release.set_value(); });
    }
    EXPECT_FALSE(timed_out);
    EXPECT_LT(std::chrono::steady_clock::now() - start, 1s);
}

/**
 * @test DestructorRunsQueuedTaskOfWorkerlessPool
 * @brief A pool with a minimum of zero workers gets a worker for the drain, with and without a supervisor.
 */
TEST(ThreadPoolScalingShutdownTest, DestructorRunsQueuedTaskOfWorkerlessPool) {
    std::atomic<int> done{0};
    {
        thread_pool_scaling<LatencyScaling<100, 5>> pool(0, 4, 60s);
        pool.run([&done] { ++done; });
    }
    EXPECT_EQ(done, 1);
    {
        thread_pool_scaling<FixedSizeScaling<2>> pool(0, 4, 60s);
        pool.run([&done] { ++done; });
    }
    EXPECT_EQ(done, 2);
}

/**
 * @test DestructorExitsSupervisorWaitingAtThreadLimit
 * @brief With every worker busy at the thread limit and a task queued, destruction waits for the workers to drain
 * the queue and returns; the supervisor parked at the limit must not keep the destructor waiting.
 */
TEST(ThreadPoolScalingShutdownTest, DestructorExitsSupervisorWaitingAtThreadLimit) {
    std::promise<void> release;
    auto released = release.get_future().share();
    std::atomic<int> done{0};
    std::thread releaser;
    const auto start = std::chrono::steady_clock::now();
    {
        thread_pool_scaling<LatencyScaling<5, 5>> pool(1, 1, 60s);
        pool.run([released] { released.wait(); });
        pool.run([&done] { ++done; });
        std::this_thread::sleep_for(30ms); // the supervisor has found the overdue task and parked at the limit
        releaser = std::thread([&release] {
            std::this_thread::sleep_for(100ms);
            release.set_value();
        });
    }
    releaser.join();
    EXPECT_EQ(done, 1);
    EXPECT_LT(std::chrono::steady_clock::now() - start, 1s);
}

/**
 * @test DestructorDrainsTaskAcceptedAfterTimedOutPop
 * @brief A surplus worker whose timed pop has just expired must not exit on shutdown while a task accepted in the
 * meantime is queued: it still occupies the only slot, so no other worker could take that task. The destruction is
 * timed close to the idle timeout to make the timed-out pop and the shutdown coincide. The window is a few hundred
 * nanoseconds wide, so this test is a smoke test only; the stress harness kept with the review notes reproduces the
 * hang within a few thousand rounds.
 */
TEST(ThreadPoolScalingShutdownTest, DestructorDrainsTaskAcceptedAfterTimedOutPop) {
    for (int i = 0; i < 300; ++i) {
        std::atomic<int> done{0};
        {
            thread_pool_scaling<LatencyScaling<0, 1>> pool(0, 1, 1ms);
            pool([] {}).wait();
            std::this_thread::sleep_for(std::chrono::microseconds(800 + i % 400));
            pool.run([&done] { ++done; });
        }
        ASSERT_EQ(done, 1) << "iteration " << i;
    }
}

/**
 * @test DestructorNeverStrandsChildSubmittedDuringShutdown
 * @brief A running task that submits a child during destruction and waits for it must observe either the child's
 * result (accepted before the queue closed, drained with growth) or a broken promise (rejected after the queue
 * closed), and destruction must return promptly. What it must never see is an accepted child that nobody runs.
 */
TEST(ThreadPoolScalingShutdownTest, DestructorNeverStrandsChildSubmittedDuringShutdown) {
    for (int i = 0; i < 200; ++i) {
        std::promise<void> entered;
        auto entered_future = entered.get_future();
        std::atomic<int> outcome{0}; // 1 = child ran, 2 = child rejected, 3 = timed out waiting
        const auto start = std::chrono::steady_clock::now();
        {
            thread_pool_scaling<LatencyScaling<2, 1>> pool(1, 2, 60s);
            pool.run([&pool, &entered, &outcome, i] {
                entered.set_value();
                std::this_thread::sleep_for(std::chrono::microseconds(i % 50));
                auto child = pool([] { return 1; });
                if (child.wait_for(1s) != std::future_status::ready) {
                    outcome = 3;
                    return;
                }
                try {
                    outcome = child.get() == 1 ? 1 : 3;
                } catch (const std::future_error& e) {
                    outcome = e.code() == std::future_errc::broken_promise ? 2 : 3;
                }
            });
            entered_future.wait();
        }
        ASSERT_NE(outcome, 3) << "iteration " << i;
        ASSERT_LT(std::chrono::steady_clock::now() - start, 1s) << "iteration " << i;
    }
}

/**
 * @test DestructorDrainsDependencyWithoutSupervisor
 * @brief A policy without supervisor that does not grow for a single queued task leaves that task to destruction,
 * which starts a worker for it. FixedSizeScaling<2> grows only at two queued tasks, so the first task only starts
 * once the second is queued behind it, and the second is left to the drain.
 */
TEST(ThreadPoolScalingShutdownTest, DestructorDrainsDependencyWithoutSupervisor) {
    std::promise<void> release;
    auto released = release.get_future().share();
    std::promise<void> entered;
    auto entered_future = entered.get_future();
    std::atomic<bool> timed_out{false};
    const auto start = std::chrono::steady_clock::now();
    {
        thread_pool_scaling<FixedSizeScaling<2>> pool(0, 2, 60s);
        pool.run([&] {
            entered.set_value();
            timed_out = released.wait_for(2s) != std::future_status::ready;
        });
        pool.run([&release] { release.set_value(); });
        entered_future.wait();
    }
    EXPECT_FALSE(timed_out);
    EXPECT_LT(std::chrono::steady_clock::now() - start, 1s);
}

/**
 * @test DestructorKeepsConcurrencyLimit
 * @brief Destruction must not add an executor: with a limit of one worker, a queued task waits for the running
 * one, so callbacks never overlap even while the pool is torn down.
 */
TEST(ThreadPoolScalingShutdownTest, DestructorKeepsConcurrencyLimit) {
    std::atomic<int> active{0};
    std::atomic<int> peak{0};
    std::atomic<int> completed{0};
    const auto callback = [&] {
        const int now_active = ++active;
        int seen = peak.load();
        while (seen < now_active and not peak.compare_exchange_weak(seen, now_active)) {
        }
        std::this_thread::sleep_for(50ms);
        --active;
        ++completed;
    };
    {
        thread_pool_scaling<LatencyScaling<5, 5>> pool(1, 1, 60s);
        pool.run(callback);
        std::this_thread::sleep_for(10ms);
        pool.run(callback);
    }
    EXPECT_EQ(completed, 2);
    EXPECT_EQ(peak, 1);
}

/**
 * @test DestructorDrainsChainOfDependencies
 * @brief Three accepted tasks each waiting for the next one are drained on destruction by starting a worker for
 * each queued task that a running one waits for, within the limit.
 */
TEST(ThreadPoolScalingShutdownTest, DestructorDrainsChainOfDependencies) {
    std::promise<void> b_done;
    auto b_done_future = b_done.get_future().share();
    std::promise<void> c_done;
    auto c_done_future = c_done.get_future().share();
    std::promise<void> entered;
    auto entered_future = entered.get_future();
    std::atomic<int> timed_out{0};
    const auto start = std::chrono::steady_clock::now();
    {
        thread_pool_scaling<LatencyScaling<50, 5>> pool(1, 3, 60s);
        pool.run([&] {
            entered.set_value();
            if (b_done_future.wait_for(2s) != std::future_status::ready) {
                ++timed_out;
            }
        });
        entered_future.wait();
        pool.run([&] {
            if (c_done_future.wait_for(2s) != std::future_status::ready) {
                ++timed_out;
            }
            b_done.set_value();
        });
        pool.run([&c_done] { c_done.set_value(); });
    }
    EXPECT_EQ(timed_out, 0);
    EXPECT_LT(std::chrono::steady_clock::now() - start, 1s);
}

/**
 * @test DestructorGrowsWorkerlessPoolBeyondPolicy
 * @brief A zero-minimum pool with a policy that never grows for so few tasks accepts dependent work without any
 * worker; destruction starts the workers the drain needs regardless of the policy.
 */
TEST(ThreadPoolScalingShutdownTest, DestructorGrowsWorkerlessPoolBeyondPolicy) {
    std::promise<void> release;
    auto released = release.get_future().share();
    std::atomic<bool> timed_out{false};
    std::atomic<int> completed{0};
    const auto start = std::chrono::steady_clock::now();
    {
        thread_pool_scaling<FixedSizeScaling<100>> pool(0, 2, 60s);
        pool.run([&] {
            timed_out = released.wait_for(2s) != std::future_status::ready;
            ++completed;
        });
        pool.run([&] {
            release.set_value();
            ++completed;
        });
        std::this_thread::sleep_for(10ms); // nothing runs: no worker, and the policy does not grow for two tasks
        EXPECT_EQ(completed, 0);
    }
    EXPECT_EQ(completed, 2);
    EXPECT_FALSE(timed_out);
    EXPECT_LT(std::chrono::steady_clock::now() - start, 1s);
}

/**
 * @test RejectsMaximumOfZeroOrBelowMinimum
 * @brief A maximum of 0, or one below the minimum, could never pass a growth decision, so an accepted task might
 * never run; the constructor rejects both instead of building a pool that silently does not grow.
 */
TEST(ThreadPoolScalingTest, RejectsMaximumOfZeroOrBelowMinimum) {
    EXPECT_THROW((thread_pool_scaling<GreedyScaling>(0, 0, 60s)), std::invalid_argument);
    EXPECT_THROW((thread_pool_scaling<GreedyScaling>(2, 1, 60s)), std::invalid_argument);
    EXPECT_THROW((thread_pool_scaling<LatencyScaling<5, 5>>(2, 0, 60s)), std::invalid_argument);
    EXPECT_NO_THROW((thread_pool_scaling<GreedyScaling>(2, 2, 60s)));
}

// =================================================================
// 10. Conditions reported to the exception policy
// =================================================================

/**
 * @brief Exception policy that keeps every reported condition.
 */
struct RecordingExceptions {
    static inline std::mutex mutex;
    static inline std::vector<std::exception_ptr> reported;

    static void handle_exception(std::exception_ptr eptr) noexcept {
        std::lock_guard lock(mutex);
        reported.push_back(std::move(eptr));
    }

    static void clear() {
        std::lock_guard lock(mutex);
        reported.clear();
    }

    static std::vector<std::exception_ptr> take() {
        std::lock_guard lock(mutex);
        return std::exchange(reported, {});
    }
};

/**
 * @brief Releases a blocked worker when the scope ends, so that a failed assertion cannot leave the pool's
 * destructor waiting for it. Declare after the pool.
 */
struct release_on_exit {
    std::promise<void>& release;
    ~release_on_exit() {
        release.set_value();
    }
};

/**
 * @test ReportsTaskStalledAtThreadLimit
 * @brief A submission that finds the oldest task older than the stall threshold, the pool at its limit and no worker
 * coming reports a thread_limit_stall with the task's age and the limit; a submission before the threshold does not.
 */
TEST(ThreadPoolScalingConditionTest, ReportsTaskStalledAtThreadLimit) {
    RecordingExceptions::clear();
    std::promise<void> release;
    auto released = release.get_future().share();
    {
        thread_pool_scaling<GreedyScaling, RecordingExceptions> pool(1, 1, 60s, 0, 50ms);
        release_on_exit releaser{release};
        pool.run([released] { released.wait(); });
        std::this_thread::sleep_for(20ms);
        pool.run([] {});
        pool.run([] {});
        EXPECT_TRUE(RecordingExceptions::take().empty());

        std::this_thread::sleep_for(100ms);
        pool.run([] {});
        const auto reported = RecordingExceptions::take();
        ASSERT_EQ(reported.size(), 1u);
        try {
            std::rethrow_exception(reported.front());
            FAIL() << "no condition";
        } catch (const thread_limit_stall& stall) {
            EXPECT_GE(stall.waited, 50ms);
            EXPECT_LT(stall.waited, 5s);
            EXPECT_EQ(stall.thread_limit, 1u);
        }
    }
    EXPECT_TRUE(RecordingExceptions::take().empty());
}

/**
 * @test NoStallReportWhileAWorkerIsComing
 * @brief Below the thread limit an old task is grown for, not reported; the stall report is only for the case the
 * pool cannot resolve itself.
 */
TEST(ThreadPoolScalingConditionTest, NoStallReportWhileAWorkerIsComing) {
    RecordingExceptions::clear();
    std::promise<void> release;
    auto released = release.get_future().share();
    std::atomic<int> ran{0};
    {
        thread_pool_scaling<FixedSizeScaling<3>, RecordingExceptions> pool(1, 2, 60s, 0, 20ms);
        pool.run([released] { released.wait(); });
        pool.run([&ran] { ++ran; });
        std::this_thread::sleep_for(50ms);
        pool.run([&ran] { ++ran; });
        pool.run([&ran] { ++ran; });
        const auto deadline = std::chrono::steady_clock::now() + 1s;
        while (ran < 3 and std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(1ms);
        }
        EXPECT_EQ(ran, 3);
        EXPECT_TRUE(RecordingExceptions::take().empty());
        release.set_value();
    }
}

// =================================================================
// 11. Greedy Scaling Tests
// =================================================================

/**
 * @test GreedyGrowsForSingleTaskBehindBlockedWorker
 * @brief A single task queued behind a blocked worker gets a worker at submission, with no supervisor and no
 * further submission.
 */
TEST(ThreadPoolScalingTest, GreedyGrowsForSingleTaskBehindBlockedWorker) {
    std::promise<void> release;
    auto released = release.get_future().share();
    std::promise<void> entered;
    auto entered_future = entered.get_future();
    std::promise<void> second_ran;
    auto second_ran_future = second_ran.get_future();
    thread_pool_scaling<GreedyScaling> pool(1, 2, 60s);
    pool.run([released, &entered] {
        entered.set_value();
        released.wait();
    });
    entered_future.wait();
    pool.run([&second_ran] { second_ran.set_value(); });
    EXPECT_EQ(second_ran_future.wait_for(200ms), std::future_status::ready);
    release.set_value();
}

namespace {
struct RecordingGreedyScaling : GreedyScaling {
    static inline std::atomic<int> asked{0};
    static bool should_grow(std::size_t current_workers, std::size_t queue_size,
                            std::optional<std::chrono::steady_clock::time_point> oldest_arrival) {
        asked++;
        return GreedyScaling::should_grow(current_workers, queue_size, oldest_arrival);
    }
};
} // namespace

/**
 * @test NoGrowthWhenIdleWorkerTakesTask
 * @brief A task that an idle worker takes at once does not even reach the policy; the pool asks should_grow()
 * only for a task that may wait.
 */
TEST(ThreadPoolScalingTest, NoGrowthWhenIdleWorkerTakesTask) {
    RecordingGreedyScaling::asked = 0;
    std::promise<void> ran;
    auto ran_future = ran.get_future();
    thread_pool_scaling<RecordingGreedyScaling> pool(2, 4, 60s);
    std::this_thread::sleep_for(20ms);
    pool.run([&ran] { ran.set_value(); });
    EXPECT_EQ(ran_future.wait_for(1s), std::future_status::ready);
    EXPECT_EQ(RecordingGreedyScaling::asked, 0);
}

/**
 * @test GreedyBurstStaysWithinLimit
 * @brief A burst of short tasks may start workers up to the limit but never beyond it, and every task runs once.
 */
TEST(ThreadPoolScalingTest, GreedyBurstStaysWithinLimit) {
    std::atomic<int> active{0};
    std::atomic<int> peak{0};
    std::atomic<int> completed{0};
    {
        thread_pool_scaling<GreedyScaling> pool(1, 6, 60s);
        for (int i = 0; i < 40; ++i) {
            pool.run([&] {
                const int now_active = ++active;
                int seen = peak.load();
                while (seen < now_active and not peak.compare_exchange_weak(seen, now_active)) {
                }
                std::this_thread::sleep_for(1ms);
                --active;
                ++completed;
            });
        }
    }
    EXPECT_EQ(completed, 40);
    EXPECT_LE(peak, 6);
    EXPECT_GE(peak, 2);
}
