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

// =================================================================
// 5. Bounded Emplace Functionality and Backpressure Tests
// =================================================================

TEST(ThreadSafeBoundedQueueTest, EmplaceBlocksWhenFullAndForwardArgs) {
    const size_t limit = 2;
    thread_safe_bounded_queue<TestTask> queue(limit);

    // 1. Fill the queue using emplace, verify it returns the size post-insertion
    ASSERT_EQ(queue.emplace(10), 1);
    ASSERT_EQ(queue.emplace(20), 2);

    std::atomic<bool> emplace_completed{false};
    std::atomic<size_t> post_emplace_size{999};

    // 2. This worker thread will attempt to emplace into a full queue
    std::thread producer([&] {
        // This should block because limit is 2
        auto sz = queue.emplace(30);
        post_emplace_size = sz;
        emplace_completed = true;
    });

    // Give the thread a moment to spin up and block
    std::this_thread::sleep_for(50ms);
    ASSERT_FALSE(emplace_completed.load());

    // 3. Pop an item to make room for the blocked emplace worker
    auto popped = queue.try_pop(100ms);
    ASSERT_TRUE(popped.has_value());
    ASSERT_EQ(popped->value, 10);

    producer.join();

    // 4. Verify the producer successfully unblocked and filled the slot
    ASSERT_TRUE(emplace_completed.load());
    ASSERT_EQ(post_emplace_size.load(), 2); // Size should be 2 again after popping 1 and emplacing 1

    // Verify the emplaced item's value matches down the pipeline
    auto final_pop = queue.try_pop(0ms); // should get value 20
    final_pop = queue.try_pop(0ms);      // should get value 30
    ASSERT_TRUE(final_pop.has_value());
    ASSERT_EQ(final_pop->value, 30);
}

// =================================================================
// 6. Move Semantics Verification Test
// =================================================================

// A mock type that tracks copy and move actions to ensure perfect pipeline efficiency
struct BoundedCopyMoveTracker {
    int id;
    int* copy_count;
    int* move_count;
    std::chrono::steady_clock::time_point arrival; // Required by bounded queue interface

    BoundedCopyMoveTracker(int id, int* cc, int* mc) :
        id(id), copy_count(cc), move_count(mc), arrival(std::chrono::steady_clock::now()) {
    }

    BoundedCopyMoveTracker(const BoundedCopyMoveTracker& other) :
        id(other.id), copy_count(other.copy_count), move_count(other.move_count), arrival(other.arrival) {
        if (copy_count)
            (*copy_count)++;
    }

    BoundedCopyMoveTracker(BoundedCopyMoveTracker&& other) noexcept :
        id(other.id), copy_count(other.copy_count), move_count(other.move_count), arrival(other.arrival) {
        if (move_count)
            (*move_count)++;
    }

    BoundedCopyMoveTracker& operator=(const BoundedCopyMoveTracker&) = default;
    BoundedCopyMoveTracker& operator=(BoundedCopyMoveTracker&&) noexcept = default;
    ~BoundedCopyMoveTracker() = default;
};

TEST(ThreadSafeBoundedQueueTest, PopStrictlyMovesAndNeverCopies) {
    thread_safe_bounded_queue<BoundedCopyMoveTracker> move_proving_queue(5);

    int total_copies = 0;
    int total_moves = 0;

    // Emplace directly into the queue so no wrapper copies or moves happen during creation
    move_proving_queue.emplace(777, &total_copies, &total_moves);

    // Reset counters to clear any inner container adjustments during allocation setup
    total_copies = 0;
    total_moves = 0;

    // Pop the item out using the standard interface
    BoundedCopyMoveTracker retrieved = move_proving_queue.pop();

    // Verify properties and confirm zero copy footprint
    EXPECT_EQ(retrieved.id, 777);
    EXPECT_EQ(total_copies, 0) << "Failure: Bounded queue copied the object instead of moving it!";
    EXPECT_GE(total_moves, 1) << "Failure: Object was not safely moved during pop().";
}
