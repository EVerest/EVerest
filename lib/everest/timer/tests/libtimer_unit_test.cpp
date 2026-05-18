// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <atomic>
#include <chrono>
#include <thread>

#include <gtest/gtest.h>

#include <everest/timer.hpp>

namespace libtimer {
class LibTimerUnitTest : public ::testing::Test {
protected:
    void SetUp() override {
    }

    void TearDown() override {
    }
};

TEST_F(LibTimerUnitTest, just_an_example) {
    ASSERT_TRUE(1 == 1);
}

// Regression: callback re-arms its own timer; pre-fix destructor deadlocks here.
TEST_F(LibTimerUnitTest, destructor_does_not_deadlock_on_reentrant_callback) {
    std::atomic<bool> finished{false};
    std::atomic<int> fire_count{0};
    std::thread watchdog([&finished]() {
        for (int i = 0; i < 50; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if (finished.load()) {
                return;
            }
        }
        ADD_FAILURE() << "Timer destructor deadlocked on re-entrant callback";
        finished.store(true);
    });

    {
        Everest::SteadyTimer timer;
        std::atomic<bool> in_callback{false};

        timer.interval(
            [&timer, &fire_count, &in_callback]() {
                in_callback.store(true);
                fire_count.fetch_add(1);
                // Sleep long enough for the main thread to enter ~Timer and start
                // blocking on the mutex before this callback calls stop().
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                timer.stop();
                in_callback.store(false);
            },
            std::chrono::milliseconds(1));

        while (!in_callback.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    finished.store(true);
    watchdog.join();
    EXPECT_GE(fire_count.load(), 1);
}
} // namespace libtimer
