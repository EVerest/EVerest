// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <mutex>
#include <stdexcept>

#include "EnergyManagerTestHelpers.hpp"

namespace module {

namespace {

// Long enough that nothing here can pass by simply waiting for the periodic run: every
// assertion below is about a wake-up that must not wait for the interval.
constexpr int LONG_UPDATE_INTERVAL_S = 60;
// Generous compared to the work a run does, tiny compared to the interval above.
constexpr auto REACTION_BUDGET = std::chrono::seconds(5);

EnergyManagerConfig make_loop_config() {
    auto config = test::make_default_config();
    config.update_interval = LONG_UPDATE_INTERVAL_S;
    return config;
}

/// \brief Counts optimizer runs and lets a test block until a given number have happened.
class RunCounter {
public:
    void operator()(const std::vector<types::energy::EnforcedLimits>&) {
        {
            std::lock_guard<std::mutex> lock(mutex);
            runs++;
        }
        changed.notify_all();
    }

    /// \returns true if the count reached \p expected within \p budget
    bool wait_for_runs(int expected, std::chrono::seconds budget) {
        std::unique_lock<std::mutex> lock(mutex);
        return changed.wait_for(lock, budget, [this, expected] { return runs >= expected; });
    }

    int count() {
        std::lock_guard<std::mutex> lock(mutex);
        return runs;
    }

private:
    std::mutex mutex;
    std::condition_variable changed;
    int runs{0};
};

types::energy::EnergyFlowRequest make_tree(bool priority) {
    auto evse = test::make_evse_node("evse1", 32.0f, 6.0f);
    evse.priority_request = priority;
    auto root = test::make_root_node("grid", 32.0f, std::nullopt, {evse});
    root.priority_request = false;
    return root;
}

} // namespace

TEST(OptimizerLoop, RunsOnceOnStart) {
    RunCounter counter;
    EnergyManagerImpl impl(make_loop_config(), std::ref(counter));

    impl.on_energy_flow_request(make_tree(false));
    impl.start();

    EXPECT_TRUE(counter.wait_for_runs(1, REACTION_BUDGET));
    impl.stop();
}

TEST(OptimizerLoop, PriorityRequestTriggersARunBeforeTheInterval) {
    // A predicated wait re-sleeps on any notification its predicate does not cover.
    RunCounter counter;
    EnergyManagerImpl impl(make_loop_config(), std::ref(counter));

    impl.on_energy_flow_request(make_tree(false));
    impl.start();
    ASSERT_TRUE(counter.wait_for_runs(1, REACTION_BUDGET));

    impl.on_energy_flow_request(make_tree(true));

    EXPECT_TRUE(counter.wait_for_runs(2, REACTION_BUDGET));
    impl.stop();
}

TEST(OptimizerLoop, NonPriorityRequestDoesNotTriggerARun) {
    RunCounter counter;
    EnergyManagerImpl impl(make_loop_config(), std::ref(counter));

    impl.on_energy_flow_request(make_tree(false));
    impl.start();
    ASSERT_TRUE(counter.wait_for_runs(1, REACTION_BUDGET));

    impl.on_energy_flow_request(make_tree(false));

    EXPECT_FALSE(counter.wait_for_runs(2, std::chrono::seconds(2)));
    impl.stop();
}

TEST(OptimizerLoop, StopReturnsWithoutWaitingOutTheInterval) {
    RunCounter counter;
    EnergyManagerImpl impl(make_loop_config(), std::ref(counter));

    impl.on_energy_flow_request(make_tree(false));
    impl.start();
    ASSERT_TRUE(counter.wait_for_runs(1, REACTION_BUDGET));

    const auto before = std::chrono::steady_clock::now();
    impl.stop();
    EXPECT_LT(std::chrono::steady_clock::now() - before, REACTION_BUDGET);
}

TEST(OptimizerLoop, StartAndStopAreIdempotent) {
    RunCounter counter;
    EnergyManagerImpl impl(make_loop_config(), std::ref(counter));

    impl.on_energy_flow_request(make_tree(false));
    impl.start();
    impl.start();
    ASSERT_TRUE(counter.wait_for_runs(1, REACTION_BUDGET));

    impl.stop();
    impl.stop();
    SUCCEED();
}

TEST(OptimizerLoop, StopWithoutStartIsSafe) {
    RunCounter counter;
    EnergyManagerImpl impl(make_loop_config(), std::ref(counter));

    impl.stop();
    EXPECT_EQ(counter.count(), 0);
}

TEST(OptimizerLoop, DestructorJoinsTheWorker) {
    RunCounter counter;
    {
        EnergyManagerImpl impl(make_loop_config(), std::ref(counter));
        impl.on_energy_flow_request(make_tree(false));
        impl.start();
        ASSERT_TRUE(counter.wait_for_runs(1, REACTION_BUDGET));
    }
    SUCCEED();
}

TEST(OptimizerLoop, StopWaitsForARunningEnforceLimitsCall) {
    std::promise<void> release;
    std::shared_future<void> released = release.get_future().share();
    std::promise<void> entered;
    std::atomic<int> calls{0};
    auto blocking_callback = [&entered, &calls, released](const std::vector<types::energy::EnforcedLimits>&) {
        if (calls++ == 0) {
            entered.set_value();
        }
        released.wait();
    };

    EnergyManagerImpl impl(make_loop_config(), blocking_callback);
    impl.on_energy_flow_request(make_tree(false));
    impl.start();
    ASSERT_EQ(entered.get_future().wait_for(REACTION_BUDGET), std::future_status::ready);

    auto stopped = std::async(std::launch::async, [&impl] { impl.stop(); });
    EXPECT_EQ(stopped.wait_for(std::chrono::milliseconds(200)), std::future_status::timeout);

    release.set_value();
    EXPECT_EQ(stopped.wait_for(REACTION_BUDGET), std::future_status::ready);
    EXPECT_EQ(calls, 1);
}

TEST(OptimizerLoop, FailingEnforceLimitsKeepsTheLoopRunning) {
    RunCounter counter;
    auto throwing_callback = [&counter](const std::vector<types::energy::EnforcedLimits>& limits) {
        counter(limits);
        throw std::runtime_error("command timeout");
    };

    EnergyManagerImpl impl(make_loop_config(), throwing_callback);
    impl.on_energy_flow_request(make_tree(false));
    impl.start();
    ASSERT_TRUE(counter.wait_for_runs(1, REACTION_BUDGET));

    impl.on_energy_flow_request(make_tree(true));

    EXPECT_TRUE(counter.wait_for_runs(2, REACTION_BUDGET));
    impl.stop();
}

} // namespace module
