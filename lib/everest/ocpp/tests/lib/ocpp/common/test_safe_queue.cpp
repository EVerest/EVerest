// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <future>
#include <thread>

#include <ocpp/common/safe_queue.hpp>

using namespace std::chrono_literals;

namespace ocpp {
namespace {

// The predicate is slow while it returns false, so the state change and notify_waiting_thread() land after the
// waiter checked its predicate but before it blocks. That notification must still wake the waiter.
TEST(SafeQueue, NotifyWakesWaiterThatAlreadyCheckedItsPredicate) {
    SafeQueue<int> queue;
    std::atomic_bool stop{false};
    std::atomic_bool checked{false};

    auto waited = std::async(std::launch::async, [&queue, &stop, &checked] {
        const auto start = std::chrono::steady_clock::now();
        queue.wait_on_queue_element_or_predicate(
            [&stop, &checked] {
                const bool result = stop.load();
                if (not result) {
                    checked = true;
                    std::this_thread::sleep_for(200ms);
                }
                return result;
            },
            5s);
        return std::chrono::steady_clock::now() - start;
    });

    while (not checked.load()) {
        std::this_thread::yield();
    }
    stop = true;
    queue.notify_waiting_thread();

    EXPECT_LT(waited.get(), 2s) << "the waiter missed the notification and only returned on its timeout";
}

} // namespace
} // namespace ocpp
