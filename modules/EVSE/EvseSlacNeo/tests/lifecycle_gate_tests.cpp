// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
//
// Tests for the module-readiness gate (main/lifecycle_gate.hpp): init() must block until the
// PLC I/O bring-up settled (ready callback, error callback, shutdown or timeout), and the SLAC
// state machine must start on whichever of {PLC I/O ready, global ready} happens second.

#include <gtest/gtest.h>

#include <chrono>
#include <thread>

#include <everest/util/async/monitor.hpp>

#include "lifecycle_gate.hpp"

namespace {

using namespace module::main;
using namespace std::chrono_literals;

struct DummyController {};
using LifecycleState = LifecycleStateT<DummyController>;
using LifecycleMonitor = everest::lib::util::monitor<LifecycleState>;

TEST(LifecycleGate, FsmStartRequiresIoReadyAndGlobalReady) {
    LifecycleState state;
    EXPECT_FALSE(state.fsm_start_allowed());

    // Boot order with the init() gate: PLC I/O comes up first (while init() blocks) ...
    state.slac_io_ready = true;
    EXPECT_FALSE(state.fsm_start_allowed()) << "must not start before global ready";

    // ... and global ready arrives second: only now the FSM may start.
    state.ready_requested = true;
    EXPECT_TRUE(state.fsm_start_allowed());

    // Recovery order: I/O drops (fault) and comes back while ready_requested stays true.
    state.slac_io_ready = false;
    EXPECT_FALSE(state.fsm_start_allowed()) << "must not start while PLC I/O is down";
    state.slac_io_ready = true;
    EXPECT_TRUE(state.fsm_start_allowed());

    state.shutting_down = true;
    EXPECT_FALSE(state.fsm_start_allowed()) << "must not start during shutdown";
}

TEST(LifecycleGate, BringUpSettledOnReadyFaultOrShutdown) {
    LifecycleState state;
    EXPECT_FALSE(state.io_bring_up_settled());

    state.slac_io_ready = true;
    EXPECT_TRUE(state.io_bring_up_settled());

    state = LifecycleState{};
    state.communication_fault_raised = true;
    EXPECT_TRUE(state.io_bring_up_settled());

    state = LifecycleState{};
    state.shutting_down = true;
    EXPECT_TRUE(state.io_bring_up_settled());
}

TEST(LifecycleGate, WaitReturnsReadyWhenIoComesUp) {
    LifecycleMonitor monitor;

    std::thread io_worker([&] {
        std::this_thread::sleep_for(20ms);
        {
            auto lifecycle = monitor.handle();
            lifecycle->slac_io_ready = true;
        }
        monitor.notify_all();
    });

    const auto start = std::chrono::steady_clock::now();
    const auto result = wait_for_io_bring_up(monitor, 5000ms);
    const auto elapsed = std::chrono::steady_clock::now() - start;

    io_worker.join();
    EXPECT_EQ(result, IoBringUpResult::Ready);
    EXPECT_LT(elapsed, 1000ms) << "must return on the ready notification, not the timeout";
}

TEST(LifecycleGate, WaitReturnsFaultWhenBringUpFails) {
    LifecycleMonitor monitor;

    std::thread io_worker([&] {
        std::this_thread::sleep_for(20ms);
        {
            auto lifecycle = monitor.handle();
            lifecycle->communication_fault_raised = true;
            lifecycle->communication_fault_message = "device open failed";
        }
        monitor.notify_all();
    });

    const auto result = wait_for_io_bring_up(monitor, 5000ms);

    io_worker.join();
    EXPECT_EQ(result, IoBringUpResult::Fault);
}

TEST(LifecycleGate, WaitReturnsShutDownEvenIfIoIsReady) {
    LifecycleMonitor monitor;
    {
        auto lifecycle = monitor.handle();
        lifecycle->slac_io_ready = true;
        lifecycle->shutting_down = true;
    }

    EXPECT_EQ(wait_for_io_bring_up(monitor, 5000ms), IoBringUpResult::ShutDown);
}

TEST(LifecycleGate, WaitTimesOutWhenNothingHappens) {
    LifecycleMonitor monitor;

    const auto start = std::chrono::steady_clock::now();
    const auto result = wait_for_io_bring_up(monitor, 50ms);
    const auto elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_EQ(result, IoBringUpResult::TimedOut);
    EXPECT_GE(elapsed, 50ms);
}

TEST(LifecycleGate, WaitReturnsImmediatelyWhenAlreadyReady) {
    LifecycleMonitor monitor;
    {
        auto lifecycle = monitor.handle();
        lifecycle->slac_io_ready = true;
    }

    const auto start = std::chrono::steady_clock::now();
    EXPECT_EQ(wait_for_io_bring_up(monitor, 5000ms), IoBringUpResult::Ready);
    EXPECT_LT(std::chrono::steady_clock::now() - start, 1000ms);
}

} // namespace
