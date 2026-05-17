// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#include "gtest/gtest.h"
#include <atomic>
#include <chrono>
#include <everest/util/queue/thread_safe_bounded_queue.hpp>
#include <optional>
#include <thread>
#include <vector>

using namespace std::chrono_literals;
using namespace everest::lib::util;

/**
 * @brief Helper struct to mimic the TrackedAction used in the thread pool,
 * as the queue now expects a type with an .arrival member.
 */
struct TestTask {
    int value;
    std::chrono::steady_clock::time_point arrival;

    explicit TestTask(int v = 0) : value(v), arrival(std::chrono::steady_clock::now()) {
    }
};

// =================================================================
// 1. Bounded Functionality Tests (Backpressure)
// =================================================================

TEST(ThreadSafeBoundedQueueTest, PushBlocksWhenFull) {
    const size_t limit = 2;
    thread_safe_bounded_queue<TestTask> queue(limit);

    // Fill the queue to the limit
    queue.push(TestTask(1));
    queue.push(TestTask(2));

    std::atomic<bool> push_completed{false};
    std::thread producer([&] {
        // This should block until a consumer pops an item
        queue.push(TestTask(3));
        push_completed = true;
    });

    // Give the thread a moment to start and block
    std::this_thread::sleep_for(50ms);
    ASSERT_FALSE(push_completed.load());

    // Pop an item, which should unblock the producer
    auto popped = queue.try_pop(100ms);
    ASSERT_TRUE(popped.has_value());
    ASSERT_EQ(popped->value, 1);

    producer.join();
    ASSERT_TRUE(push_completed.load());
}

// =================================================================
// 2. Latency Interface Tests
// =================================================================

TEST(ThreadSafeBoundedQueueTest, OldestArrivalTracking) {
    thread_safe_bounded_queue<TestTask> queue(10);

    auto t1 = std::chrono::steady_clock::now();
    queue.push(TestTask(100));
    std::this_thread::sleep_for(10ms);

    auto t2 = std::chrono::steady_clock::now();
    queue.push(TestTask(200));

    auto oldest = queue.oldest_arrival();

    // The oldest arrival should be close to t1, certainly before t2
    ASSERT_GE(oldest, t1);
    ASSERT_LT(oldest, t2);
}

// =================================================================
// 3. Stop and Signaling Tests
// =================================================================

TEST(ThreadSafeBoundedQueueTest, StopUnblocksBlockedProducers) {
    thread_safe_bounded_queue<TestTask> queue(1);
    queue.push(TestTask(1)); // Fill it

    std::atomic<bool> producer_exited{false};
    std::thread producer([&] {
        // This blocks because queue is full
        size_t result = queue.push(TestTask(2));
        // result should be 0 because the queue was stopped
        if (result == 0) {
            producer_exited = true;
        }
    });

    std::this_thread::sleep_for(50ms);
    queue.stop(); // This should wake the producer up

    producer.join();
    ASSERT_TRUE(producer_exited.load());
}

TEST(ThreadSafeBoundedQueueTest, StopReturnsNullOptToConsumers) {
    thread_safe_bounded_queue<TestTask> queue(5);

    std::thread consumer([&] {
        auto result = queue.try_pop(1s);
        ASSERT_FALSE(result.has_value());
    });

    std::this_thread::sleep_for(20ms);
    queue.stop();
    consumer.join();
}

// =================================================================
// 4. Stress Tests (Concurrent Producers and Consumers)
// =================================================================

TEST(ThreadSafeBoundedQueueTest, HighContentionStressTest) {
    const int num_producers = 4;
    const int num_consumers = 4;
    const int items_per_producer = 1000;
    const size_t queue_limit = 10;

    thread_safe_bounded_queue<TestTask> queue(queue_limit);
    std::atomic<int> total_popped{0};
    std::atomic<int> sum_popped{0};

    std::vector<std::thread> workers;

    // Consumers
    for (int i = 0; i < num_consumers; ++i) {
        workers.emplace_back([&] {
            while (total_popped < (num_producers * items_per_producer)) {
                auto val = queue.try_pop(10ms);
                if (val) {
                    sum_popped += val->value;
                    total_popped++;
                }
            }
        });
    }

    // Producers
    for (int i = 0; i < num_producers; ++i) {
        workers.emplace_back([&] {
            for (int j = 0; j < items_per_producer; ++j) {
                queue.push(TestTask(1));
            }
        });
    }

    for (auto& w : workers)
        w.join();

    ASSERT_EQ(total_popped.load(), num_producers * items_per_producer);
    ASSERT_EQ(sum_popped.load(), num_producers * items_per_producer);
}
