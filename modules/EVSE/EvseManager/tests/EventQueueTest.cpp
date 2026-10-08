// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <EventQueue.hpp>
#include <gtest/gtest.h>

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace {

enum class ErrorHandlingEvents : std::uint8_t {
    PreventCharging,
    PreventChargingWelded,
    AllErrorsCleared
};

TEST(EventQueue, init) {
    module::EventQueue<ErrorHandlingEvents> queue;
    auto events = queue.get_events();
    EXPECT_EQ(events.size(), 0);
}

TEST(EventQueue, one) {
    module::EventQueue<ErrorHandlingEvents> queue;
    auto events = queue.get_events();
    EXPECT_EQ(events.size(), 0);

    queue.push(ErrorHandlingEvents::PreventCharging);
    events = queue.get_events();
    ASSERT_EQ(events.size(), 1);
    EXPECT_EQ(events[0], ErrorHandlingEvents::PreventCharging);

    events = queue.get_events();
    EXPECT_EQ(events.size(), 0);
}

TEST(EventQueue, two) {
    module::EventQueue<ErrorHandlingEvents> queue;
    auto events = queue.get_events();
    EXPECT_EQ(events.size(), 0);

    queue.push(ErrorHandlingEvents::PreventCharging);
    queue.push(ErrorHandlingEvents::PreventChargingWelded);
    events = queue.get_events();
    ASSERT_EQ(events.size(), 2);
    EXPECT_EQ(events[0], ErrorHandlingEvents::PreventCharging);
    EXPECT_EQ(events[1], ErrorHandlingEvents::PreventChargingWelded);

    events = queue.get_events();
    EXPECT_EQ(events.size(), 0);
}

TEST(EventQueue, wait) {
    module::EventQueue<ErrorHandlingEvents> queue;
    auto events = queue.get_events();
    EXPECT_EQ(events.size(), 0);

    std::size_t count = 0;
    std::condition_variable cv;
    std::mutex mux;
    bool ready{false};

    std::thread wait_thread([&cv, &count, &queue, &ready, &mux]() {
        {
            std::lock_guard<std::mutex> lock(mux);
            ready = true;
        }
        cv.notify_one();
        auto events = queue.wait();
        count = events.size();
        {
            std::lock_guard<std::mutex> lock(mux);
            ready = false;
        }
        cv.notify_one();
    });

    std::unique_lock<std::mutex> ul(mux);
    cv.wait(ul, [&ready] { return ready; });
    ASSERT_EQ(count, 0U);

    queue.push(ErrorHandlingEvents::PreventCharging);

    cv.wait(ul, [&ready] { return !ready; });
    ASSERT_EQ(count, 1U);

    events = queue.get_events();
    EXPECT_EQ(events.size(), 0);

    wait_thread.join();
}

TEST(EventQueue, stop_unblocks_wait) {
    module::EventQueue<ErrorHandlingEvents> queue;

    std::condition_variable cv;
    std::mutex mux;
    bool waiting{false};
    bool returned{false};
    std::size_t count = 0;

    std::thread wait_thread([&cv, &mux, &queue, &waiting, &returned, &count]() {
        {
            std::lock_guard<std::mutex> lock(mux);
            waiting = true;
        }
        cv.notify_one();
        const auto events = queue.wait();
        {
            std::lock_guard<std::mutex> lock(mux);
            count = events.size();
            returned = true;
        }
        cv.notify_one();
    });

    std::unique_lock<std::mutex> ul(mux);
    cv.wait(ul, [&waiting] { return waiting; });
    EXPECT_FALSE(returned);

    // stop() must interrupt a blocking wait(), otherwise ~Charger() cannot join the error thread
    queue.stop();

    const auto unblocked = cv.wait_for(ul, std::chrono::seconds(1), [&returned] { return returned; });
    EXPECT_TRUE(unblocked) << "wait() was not interrupted by stop()";
    if (unblocked) {
        EXPECT_EQ(count, 0U);
    } else {
        // release the thread so that the test reports a failure instead of hanging in join()
        ul.unlock();
        queue.push(ErrorHandlingEvents::PreventCharging);
        ul.lock();
        cv.wait(ul, [&returned] { return returned; });
    }
    ul.unlock();

    wait_thread.join();
}

TEST(EventQueue, stop_keeps_pending_events) {
    module::EventQueue<ErrorHandlingEvents> queue;

    // an event queued before the stop must still be delivered, otherwise ~Charger() would drop
    // its ForceEmergencyShutdown
    queue.push(ErrorHandlingEvents::PreventCharging);
    queue.stop();
    EXPECT_TRUE(queue.is_stopped());

    // safe to block on, the event queued above satisfies the predicate either way
    auto events = queue.wait();
    ASSERT_EQ(events.size(), 1);
    EXPECT_EQ(events[0], ErrorHandlingEvents::PreventCharging);

    // the queue is drained now, stop_unblocks_wait_for covers that a further wait returns at once
    events = queue.get_events();
    EXPECT_EQ(events.size(), 0);
}

TEST(EventQueue, stop_unblocks_wait_for) {
    module::EventQueue<ErrorHandlingEvents> queue;
    queue.stop();

    const auto start = std::chrono::steady_clock::now();
    const auto events = queue.wait_for(std::chrono::seconds(5));
    const auto elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_EQ(events.size(), 0);
    EXPECT_LT(elapsed, std::chrono::seconds(1)) << "wait_for() ignored the stop and waited for the timeout";
}

} // namespace
