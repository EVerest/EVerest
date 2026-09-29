// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Contributors to EVerest

#include <everest/util/async/thread_pool_scaling.hpp>
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <functional>
#include <future>
#include <memory>
#include <random>
#include <stdexcept>
#include <thread>
#include <vector>

#ifdef __linux__
#include <dirent.h>
#endif

using namespace std::chrono_literals;
using everest::lib::util::LatencyScaling;
using everest::lib::util::thread_pool_scaling;

namespace {

using Pool = thread_pool_scaling<LatencyScaling<1, 1>>;

template <typename Predicate> bool wait_until(Predicate&& predicate, std::chrono::milliseconds timeout = 2s) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!predicate()) {
        if (std::chrono::steady_clock::now() >= deadline) {
            return false;
        }
        std::this_thread::sleep_for(1ms);
    }
    return true;
}

void update_peak(std::atomic<int>& peak, int value) {
    auto observed = peak.load(std::memory_order_relaxed);
    while (observed < value && !peak.compare_exchange_weak(observed, value, std::memory_order_relaxed)) {
    }
}

int stress_rounds() {
    const char* configured = std::getenv("EVEREST_THREAD_POOL_STRESS_ROUNDS");
    if (configured == nullptr) {
        return 100;
    }
    const long parsed = std::strtol(configured, nullptr, 10);
    return parsed > 0 && parsed <= 1000000 ? static_cast<int>(parsed) : 100;
}

unsigned stress_seed() {
    const char* configured = std::getenv("EVEREST_THREAD_POOL_STRESS_SEED");
    if (configured == nullptr) {
        return 9826;
    }
    const unsigned long parsed = std::strtoul(configured, nullptr, 10);
    return parsed <= 0xffffffffUL ? static_cast<unsigned>(parsed) : 9826;
}

#ifdef __linux__
int linux_thread_count() {
    DIR* directory = opendir("/proc/self/task");
    if (directory == nullptr) {
        return -1;
    }

    int count = 0;
    while (auto* entry = readdir(directory)) {
        if (entry->d_name[0] != '.') {
            ++count;
        }
    }
    closedir(directory);
    return count;
}
#endif

class ThreadPoolScalingReliabilityTest : public ::testing::TestWithParam<std::size_t> {};

TEST_P(ThreadPoolScalingReliabilityTest, DeliversEveryTaskExactlyOnce) {
    constexpr int producer_count = 4;
    constexpr int tasks_per_producer = 48;
    constexpr int maximum_workers = 4;
    constexpr int task_count = producer_count * tasks_per_producer;

    std::vector<std::atomic<int>> deliveries(task_count);
    for (auto& delivery : deliveries) {
        delivery.store(0, std::memory_order_relaxed);
    }
    std::atomic<int> active{0};
    std::atomic<int> peak{0};
    std::atomic<int> duplicate_deliveries{0};

    {
        Pool pool(1, maximum_workers, 3ms, GetParam());
        std::vector<std::thread> producers;
        producers.reserve(producer_count);
        for (int producer = 0; producer < producer_count; ++producer) {
            producers.emplace_back([&, producer] {
                for (int task = 0; task < tasks_per_producer; ++task) {
                    const int id = producer * tasks_per_producer + task;
                    pool.run([&, id] {
                        const int now_active = active.fetch_add(1, std::memory_order_relaxed) + 1;
                        update_peak(peak, now_active);
                        if (deliveries[id].fetch_add(1, std::memory_order_relaxed) != 0) {
                            duplicate_deliveries.fetch_add(1, std::memory_order_relaxed);
                        }
                        if (id % 19 == 0) {
                            std::this_thread::yield();
                        }
                        active.fetch_sub(1, std::memory_order_relaxed);
                        if (id % 31 == 0) {
                            throw std::runtime_error("fire-and-forget exception");
                        }
                    });
                }
            });
        }
        for (auto& producer : producers) {
            producer.join();
        }
    }

    EXPECT_EQ(duplicate_deliveries.load(), 0);
    EXPECT_EQ(active.load(), 0);
    EXPECT_LE(peak.load(), maximum_workers);
    for (const auto& delivery : deliveries) {
        EXPECT_EQ(delivery.load(), 1);
    }
}

INSTANTIATE_TEST_SUITE_P(BoundedAndUnbounded, ThreadPoolScalingReliabilityTest, ::testing::Values(0, 1, 4));

TEST(ThreadPoolScalingReliability, HandoffStartsDependencyWithoutAnotherSubmission) {
    std::promise<void> entered;
    auto entered_future = entered.get_future();
    std::promise<void> release;
    auto released = release.get_future().share();
    std::atomic<int> complete{0};
    std::atomic<bool> timed_out{false};

    bool worker_entered = false;
    {
        Pool pool(1, 4, 50ms);
        pool.run([&] {
            entered.set_value();
            if (released.wait_for(2s) != std::future_status::ready) {
                timed_out.store(true, std::memory_order_relaxed);
            }
            complete.fetch_add(1, std::memory_order_relaxed);
        });
        worker_entered = entered_future.wait_for(1s) == std::future_status::ready;
        pool.run([&] {
            release.set_value();
            complete.fetch_add(1, std::memory_order_relaxed);
        });
        if (worker_entered) {
            EXPECT_TRUE(wait_until([&] { return complete.load() == 2; }));
        }
    }

    EXPECT_TRUE(worker_entered);
    EXPECT_FALSE(timed_out.load());
    EXPECT_EQ(complete.load(), 2);
}

TEST_P(ThreadPoolScalingReliabilityTest, NestedFuturesCompleteBelowWorkerLimit) {
    std::function<int(int)> nested;
    Pool pool(1, 8, 20ms, GetParam());
    nested = [&](int depth) -> int {
        if (depth == 0) {
            return 1;
        }
        auto child = pool(nested, depth - 1);
        if (child.wait_for(2s) != std::future_status::ready) {
            return -1000;
        }
        return child.get() + 1;
    };

    for (int round = 0; round < 3; ++round) {
        auto result = pool(nested, 5);
        ASSERT_EQ(result.wait_for(2s), std::future_status::ready);
        EXPECT_EQ(result.get(), 6);
    }
}

TEST(ThreadPoolScalingReliability, RetiresSurplusWorkersAndGrowsAgain) {
#ifndef __linux__
    GTEST_SKIP() << "thread-count observation is Linux-specific";
#else
    struct Cycle {
        std::promise<void> release;
        std::shared_future<void> released{release.get_future().share()};
        std::atomic<int> entered{0};
        std::atomic<int> complete{0};
    };

    Pool pool(1, 4, 10ms);
    const int baseline = linux_thread_count();
    ASSERT_GT(baseline, 0);
    for (int round = 0; round < 2; ++round) {
        const auto cycle = std::make_shared<Cycle>();
        int expanded = -1;
        bool all_workers_entered = false;
        bool all_workers_completed = false;
        bool workers_retired = false;
        for (int task = 0; task < 4; ++task) {
            pool.run([cycle] {
                cycle->entered.fetch_add(1, std::memory_order_relaxed);
                cycle->released.wait_for(2s);
                cycle->complete.fetch_add(1, std::memory_order_relaxed);
            });
        }
        all_workers_entered = wait_until([&] { return cycle->entered.load() == 4; });
        expanded = linux_thread_count();
        cycle->release.set_value();
        all_workers_completed = wait_until([&] { return cycle->complete.load() == 4; }, 3s);
        workers_retired = wait_until([&] { return linux_thread_count() <= baseline; }, 2s);
        EXPECT_TRUE(all_workers_entered);
        EXPECT_GE(expanded, baseline + 3);
        EXPECT_TRUE(all_workers_completed);
        EXPECT_TRUE(workers_retired);
    }
#endif
}

TEST_P(ThreadPoolScalingReliabilityTest, FuturesRetainValuesAndExceptionsAfterDrain) {
    constexpr int task_count = 72;
    std::vector<std::future<int>> results;
    results.reserve(task_count);
    {
        Pool pool(1, 4, 10ms, GetParam());
        for (int task = 0; task < task_count; ++task) {
            results.emplace_back(pool([task] {
                if (task % 3 == 0) {
                    throw std::runtime_error("expected task failure");
                }
                return task;
            }));
        }
    }

    for (int task = 0; task < task_count; ++task) {
        if (task % 3 == 0) {
            EXPECT_THROW((void)results[task].get(), std::runtime_error);
        } else {
            EXPECT_EQ(results[task].get(), task);
        }
    }
}

TEST(ThreadPoolScalingReliability, DestructorDrainsDependencyAndControl) {
    for (const bool release_before_destruction : {false, true}) {
        auto release = std::make_shared<std::promise<void>>();
        const auto released = release->get_future().share();
        std::promise<void> entered;
        auto entered_future = entered.get_future();
        std::atomic<bool> timed_out{false};

        {
            Pool pool(1, 4, 1s);
            pool.run([&] {
                entered.set_value();
                timed_out.store(released.wait_for(2s) != std::future_status::ready, std::memory_order_relaxed);
            });
            ASSERT_EQ(entered_future.wait_for(1s), std::future_status::ready);
            pool.run([release] { release->set_value(); });
            if (release_before_destruction) {
                ASSERT_EQ(released.wait_for(1s), std::future_status::ready);
            }
        }

        EXPECT_FALSE(timed_out.load());
    }
}

TEST(ThreadPoolScalingReliability, FrameworkLatencyPolicyDrainsDependency) {
    using FrameworkLatencyPool = thread_pool_scaling<LatencyScaling<50, 5>>;

    auto release = std::make_shared<std::promise<void>>();
    const auto released = release->get_future().share();
    std::promise<void> entered;
    auto entered_future = entered.get_future();
    std::atomic<bool> timed_out{false};
    {
        FrameworkLatencyPool pool(1, 4, 1s);
        pool.run([&] {
            entered.set_value();
            timed_out.store(released.wait_for(2s) != std::future_status::ready, std::memory_order_relaxed);
        });
        ASSERT_EQ(entered_future.wait_for(1s), std::future_status::ready);
        pool.run([release] { release->set_value(); });
    }
    EXPECT_FALSE(timed_out.load());
}

TEST(ThreadPoolScalingReliability, WorkerlessPoolRestartsAfterRetirement) {
    thread_pool_scaling<LatencyScaling<0, 1>> pool(0, 4, 5ms);
#ifdef __linux__
    const int baseline = linux_thread_count();
    ASSERT_GT(baseline, 0);
#endif
    for (int round = 0; round < 6; ++round) {
        auto result = pool([round] { return round; });
        ASSERT_EQ(result.wait_for(2s), std::future_status::ready);
        EXPECT_EQ(result.get(), round);
#ifdef __linux__
        EXPECT_TRUE(wait_until([&] { return linux_thread_count() <= baseline; }, 2s));
#else
        std::this_thread::sleep_for(15ms);
#endif
    }
}

TEST_P(ThreadPoolScalingReliabilityTest, DestructorDrainsTaskNearTimedPop) {
    for (int round = 0; round < 8; ++round) {
        std::atomic<int> complete{0};
        {
            Pool pool(0, 1, 1ms, GetParam());
            auto warmup = pool([] {});
            ASSERT_EQ(warmup.wait_for(1s), std::future_status::ready);
            std::this_thread::sleep_for(std::chrono::microseconds(800 + (round % 5) * 100));
            pool.run([&] { complete.fetch_add(1, std::memory_order_relaxed); });
        }
        EXPECT_EQ(complete.load(), 1);
    }
}

TEST_P(ThreadPoolScalingReliabilityTest, DestructorDrainsDependencyNearTimedPop) {
    for (int round = 0; round < 3; ++round) {
        std::promise<void> release;
        const auto released = release.get_future().share();
        std::promise<void> entered;
        auto entered_future = entered.get_future();
        std::atomic<bool> timed_out{false};
        {
            Pool pool(1, 2, 1ms, GetParam());
            pool.run([&] {
                entered.set_value();
                timed_out.store(released.wait_for(2s) != std::future_status::ready, std::memory_order_relaxed);
            });
            ASSERT_EQ(entered_future.wait_for(1s), std::future_status::ready);
            auto warmup = pool([] {});
            ASSERT_EQ(warmup.wait_for(1s), std::future_status::ready);
            std::this_thread::sleep_for(std::chrono::microseconds(800 + (round % 5) * 100));
            pool.run([&] { release.set_value(); });
        }
        EXPECT_FALSE(timed_out.load());
    }
}

TEST(ThreadPoolScalingReliability, ReentrantSubmitDuringShutdownCompletesOrIsRejected) {
    for (int round = 0; round < 8; ++round) {
        std::promise<void> entered;
        auto entered_future = entered.get_future();
        std::atomic<bool> submit{false};
        std::atomic<bool> child_ran{false};
        std::atomic<bool> rejected{false};
        std::atomic<bool> timed_out{false};
        bool worker_entered = false;
        {
            Pool pool(1, 2, 1s);
            pool.run([&] {
                entered.set_value();
                if (!wait_until([&] { return submit.load(std::memory_order_acquire); })) {
                    timed_out.store(true, std::memory_order_relaxed);
                    return;
                }
                auto child = pool([&] {
                    child_ran.store(true, std::memory_order_relaxed);
                    return 42;
                });
                if (child.wait_for(500ms) != std::future_status::ready) {
                    timed_out.store(true, std::memory_order_relaxed);
                    return;
                }
                try {
                    (void)child.get();
                } catch (const std::future_error& error) {
                    rejected.store(error.code() == std::make_error_code(std::future_errc::broken_promise),
                                   std::memory_order_relaxed);
                }
            });
            worker_entered = entered_future.wait_for(1s) == std::future_status::ready;
            submit.store(true, std::memory_order_release);
        }
        EXPECT_TRUE(worker_entered);
        EXPECT_FALSE(timed_out.load());
        EXPECT_TRUE(child_ran.load() || rejected.load());
    }
}

TEST_P(ThreadPoolScalingReliabilityTest, DestructorDrainsAcceptedDependencyChain) {
    for (int round = 0; round < 2; ++round) {
        std::vector<std::promise<void>> signals(4);
        std::vector<std::shared_future<void>> ready;
        ready.reserve(signals.size());
        for (auto& signal : signals) {
            ready.emplace_back(signal.get_future().share());
        }
        std::promise<void> entered;
        auto entered_future = entered.get_future();
        std::atomic<int> complete{0};
        std::atomic<bool> timed_out{false};
        {
            Pool pool(1, 4, 3ms, GetParam());
            pool.run([&] {
                entered.set_value();
                if (ready[1].wait_for(2s) != std::future_status::ready) {
                    timed_out.store(true, std::memory_order_relaxed);
                }
                signals[0].set_value();
                complete.fetch_add(1, std::memory_order_relaxed);
            });
            ASSERT_EQ(entered_future.wait_for(1s), std::future_status::ready);
            for (int task = 1; task < 4; ++task) {
                pool.run([&, task] {
                    if (task < 3 && ready[task + 1].wait_for(2s) != std::future_status::ready) {
                        timed_out.store(true, std::memory_order_relaxed);
                    }
                    signals[task].set_value();
                    complete.fetch_add(1, std::memory_order_relaxed);
                });
            }
        }
        EXPECT_FALSE(timed_out.load());
        EXPECT_EQ(complete.load(), 4);
    }
}

TEST(ThreadPoolScalingReliability, SingleWorkerKeepsSharedCallbackStateSerial) {
    int shared_counter = 0;
    std::atomic<int> active{0};
    std::atomic<int> peak{0};
    std::promise<void> entered;
    auto entered_future = entered.get_future();
    {
        Pool pool(1, 1, 1s);
        pool.run([&] {
            const int now_active = active.fetch_add(1, std::memory_order_relaxed) + 1;
            update_peak(peak, now_active);
            entered.set_value();
            std::this_thread::sleep_for(30ms);
            ++shared_counter;
            active.fetch_sub(1, std::memory_order_relaxed);
        });
        ASSERT_EQ(entered_future.wait_for(1s), std::future_status::ready);
        pool.run([&] {
            const int now_active = active.fetch_add(1, std::memory_order_relaxed) + 1;
            update_peak(peak, now_active);
            ++shared_counter;
            active.fetch_sub(1, std::memory_order_relaxed);
        });
    }
    EXPECT_EQ(peak.load(), 1);
    EXPECT_EQ(shared_counter, 2);
}

TEST(ThreadPoolScalingReliabilityStress, DISABLED_RepeatedCoreLifecycleAndShutdownRaces) {
    constexpr int task_count = 48;
    std::mt19937 random(stress_seed());
    for (int round = 0; round < stress_rounds(); ++round) {
        std::vector<std::atomic<int>> deliveries(task_count);
        for (auto& delivery : deliveries) {
            delivery.store(0, std::memory_order_relaxed);
        }
        {
            Pool pool(0, 4, std::chrono::milliseconds(1 + random() % 4), round % 3 == 0 ? 1 : 0);
            for (int task = 0; task < task_count; ++task) {
                pool.run([&, task] { deliveries[task].fetch_add(1, std::memory_order_relaxed); });
            }
        }
        for (const auto& delivery : deliveries) {
            EXPECT_EQ(delivery.load(), 1);
        }

        std::atomic<int> cold_complete{0};
        {
            Pool pool(0, 1, std::chrono::milliseconds(1 + random() % 4));
            auto warmup = pool([] {});
            ASSERT_EQ(warmup.wait_for(1s), std::future_status::ready);
            std::this_thread::sleep_for(std::chrono::microseconds(500 + random() % 1500));
            pool.run([&] { cold_complete.fetch_add(1, std::memory_order_relaxed); });
        }
        EXPECT_EQ(cold_complete.load(), 1);

        std::promise<void> release;
        const auto released = release.get_future().share();
        std::promise<void> entered;
        auto entered_future = entered.get_future();
        std::atomic<bool> timed_out{false};
        {
            Pool pool(1, 2, std::chrono::milliseconds(1 + random() % 4));
            pool.run([&] {
                entered.set_value();
                timed_out.store(released.wait_for(1s) != std::future_status::ready, std::memory_order_relaxed);
            });
            const bool worker_entered = entered_future.wait_for(1s) == std::future_status::ready;
            pool.run([&] { release.set_value(); });
            EXPECT_TRUE(worker_entered);
        }
        EXPECT_FALSE(timed_out.load());

        std::promise<void> handoff_entered;
        auto handoff_started = handoff_entered.get_future();
        std::promise<void> handoff_release;
        const auto handoff_released = handoff_release.get_future().share();
        std::atomic<int> handoff_complete{0};
        std::atomic<bool> handoff_timed_out{false};
        {
            Pool pool(1, 4, 50ms);
            pool.run([&] {
                handoff_entered.set_value();
                if (handoff_released.wait_for(1s) != std::future_status::ready) {
                    handoff_timed_out.store(true, std::memory_order_relaxed);
                }
                handoff_complete.fetch_add(1, std::memory_order_relaxed);
            });
            const bool worker_entered = handoff_started.wait_for(1s) == std::future_status::ready;
            std::this_thread::sleep_for(std::chrono::microseconds(random() % 101));
            pool.run([&] {
                handoff_release.set_value();
                handoff_complete.fetch_add(1, std::memory_order_relaxed);
            });
            EXPECT_TRUE(worker_entered);
            EXPECT_TRUE(wait_until([&] { return handoff_complete.load() == 2; }));
        }
        EXPECT_FALSE(handoff_timed_out.load());
    }
}

} // namespace
