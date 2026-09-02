// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
//
// Tests for the module-readiness gate (main/lifecycle_gate.hpp): init() runs the event loop until
// the PLC I/O bring-up settled (ready callback, error callback, shutdown or timeout), and the SLAC
// state machine must start on whichever of {PLC I/O ready, global ready} happens second. The loop
// handshake itself (ready_entered / loop_exited) is covered by the generic lifecycle_gate tests in
// lib/everest/util.

#include <gtest/gtest.h>

#include "lifecycle_gate.hpp"

namespace {

using namespace module::main;

struct DummyController {};
using LifecycleState = LifecycleStateT<DummyController>;

TEST(LifecycleGate, FsmStartRequiresIoReadyAndGlobalReady) {
    LifecycleState state;
    EXPECT_FALSE(state.fsm_start_allowed());

    // Boot order with the init() gate: PLC I/O comes up first (while init() runs the bring-up loop) ...
    state.slac_io_ready = true;
    EXPECT_FALSE(state.fsm_start_allowed()) << "must not start before global ready";

    // ... and global ready arrives second: only now the FSM may start.
    state.ready_entered = true;
    EXPECT_TRUE(state.fsm_start_allowed());

    // Recovery order: I/O drops (fault) and comes back while ready_entered stays true.
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

TEST(LifecycleGate, BringUpResultClassifiesTheSettledState) {
    LifecycleState state;
    EXPECT_EQ(bring_up_result(state), IoBringUpResult::TimedOut) << "nothing happened: the timer must have fired";

    state.slac_io_ready = true;
    EXPECT_EQ(bring_up_result(state), IoBringUpResult::Ready);

    state = LifecycleState{};
    state.communication_fault_raised = true;
    state.communication_fault_message = "device open failed";
    EXPECT_EQ(bring_up_result(state), IoBringUpResult::Fault);

    // A fault that was later cleared by a ready callback counts as ready.
    state.slac_io_ready = true;
    EXPECT_EQ(bring_up_result(state), IoBringUpResult::Ready);

    // Shutdown wins even if the I/O came up.
    state.shutting_down = true;
    EXPECT_EQ(bring_up_result(state), IoBringUpResult::ShutDown);
}

TEST(LifecycleGate, BringUpRunningIsIndependentOfTheReadyLoop) {
    // shutdown() waits on this flag separately from loop_exited: the bring-up loop in init() runs
    // before ready_entered is ever set, so loop_settled() alone would let shutdown() destroy the
    // objects the init thread is still polling.
    LifecycleState state;
    EXPECT_FALSE(state.bring_up_running);
    state.bring_up_running = true;
    EXPECT_TRUE(state.loop_settled()) << "the ready loop was never entered";
    EXPECT_TRUE(state.may_enter_loop()) << "bring-up does not consume the ready loop";
}

TEST(LifecycleGate, GenericLoopHandshakeIsInherited) {
    LifecycleState state;
    EXPECT_TRUE(state.may_enter_loop());
    EXPECT_TRUE(state.loop_settled()) << "a loop that was never entered has nothing to wait for";
    EXPECT_EQ(state.live_worker(), nullptr);

    DummyController controller;
    state.worker = &controller;
    EXPECT_EQ(state.live_worker(), &controller);

    state.ready_entered = true;
    EXPECT_FALSE(state.may_enter_loop()) << "a second ready() must not start a second loop";
    EXPECT_FALSE(state.loop_settled()) << "the loop is running";

    state.shutting_down = true;
    EXPECT_EQ(state.live_worker(), nullptr) << "handlers must drop commands once shutdown started";
    EXPECT_FALSE(state.fsm_start_allowed());

    state.loop_exited = true;
    EXPECT_TRUE(state.loop_settled());
}

} // namespace
