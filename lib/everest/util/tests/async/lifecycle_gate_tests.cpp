// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest
//
// Tests for the shutdown handshake in async/lifecycle_gate.hpp: ready() first (shutdown waits) and
// shutdown() first (ready never enters the loop, shutdown does not wait).

#include <gtest/gtest.h>

#include <chrono>
#include <thread>

#include <everest/util/async/lifecycle_gate.hpp>
#include <everest/util/async/monitor.hpp>

namespace {

using namespace everest::lib::util;
using namespace std::chrono_literals;

struct DummyWorker {};
using LifecycleState = LifecycleStateT<DummyWorker>;
using LifecycleMonitor = monitor<LifecycleState>;

TEST(LifecycleGate, TheLoopIsEnteredOnceAndNotAfterShutdownWasRequested) {
    LifecycleState state;
    EXPECT_TRUE(state.may_enter_loop());

    state.ready_entered = true;
    EXPECT_FALSE(state.may_enter_loop()) << "a repeated ready must not start a second loop";

    state = LifecycleState{};
    state.shutting_down = true;
    EXPECT_FALSE(state.may_enter_loop()) << "shutdown won the race; ready has nothing to do";
}

TEST(LifecycleGate, NothingToWaitForUntilTheLoopWasActuallyEntered) {
    LifecycleState state;
    EXPECT_TRUE(state.loop_settled()) << "ready() never ran, so shutdown must not block";

    state.ready_entered = true;
    EXPECT_FALSE(state.loop_settled()) << "the loop is running and has to be waited for";

    state.loop_exited = true;
    EXPECT_TRUE(state.loop_settled());
}

TEST(LifecycleGate, HandlersStopReachingTheWorkerOnceShutdownStarted) {
    DummyWorker worker;
    LifecycleState state;
    EXPECT_EQ(nullptr, state.live_worker()) << "no worker before the module built one";

    state.worker = &worker;
    EXPECT_EQ(&worker, state.live_worker());

    state.shutting_down = true;
    EXPECT_EQ(nullptr, state.live_worker()) << "work must be dropped, not dispatched into a worker "
                                               "that is about to be destroyed";
}

TEST(LifecycleGate, WaitingReportsNotRunningWhenReadyNeverEnteredTheLoop) {
    LifecycleMonitor monitor;

    auto const start = std::chrono::steady_clock::now();
    auto const result = wait_for_loop_exit(monitor, 5000ms);
    auto const elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_EQ(LoopExitResult::NotRunning, result);
    EXPECT_LT(elapsed, 1000ms) << "it must return at once rather than time out";
}

TEST(LifecycleGate, WaitingReturnsImmediatelyWhenTheLoopAlreadyFinished) {
    LifecycleMonitor monitor;
    {
        auto lifecycle = monitor.handle();
        lifecycle->ready_entered = true;
        lifecycle->loop_exited = true;
    }

    auto const start = std::chrono::steady_clock::now();
    auto const result = wait_for_loop_exit(monitor, 5000ms);
    auto const elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_EQ(LoopExitResult::Stopped, result);
    EXPECT_LT(elapsed, 1000ms);
}

TEST(LifecycleGate, WaitingBlocksUntilTheLoopReportsItHasExited) {
    LifecycleMonitor monitor;
    {
        auto lifecycle = monitor.handle();
        lifecycle->ready_entered = true;
    }

    std::thread loop([&] {
        std::this_thread::sleep_for(50ms);
        {
            auto lifecycle = monitor.handle();
            lifecycle->loop_exited = true;
        }
        monitor.notify_all();
    });

    auto const start = std::chrono::steady_clock::now();
    auto const result = wait_for_loop_exit(monitor, 5000ms);
    auto const elapsed = std::chrono::steady_clock::now() - start;
    loop.join();

    EXPECT_EQ(LoopExitResult::Stopped, result);
    EXPECT_GE(elapsed, 40ms) << "it really waited";
}

// A loop that never returns: the wait times out and reports it.
TEST(LifecycleGate, WaitingTimesOutOnALoopThatDoesNotReturn) {
    LifecycleMonitor monitor;
    {
        auto lifecycle = monitor.handle();
        lifecycle->ready_entered = true;
    }

    auto const result = wait_for_loop_exit(monitor, 50ms);

    EXPECT_EQ(LoopExitResult::TimedOut, result);
}

} // namespace
