// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

/**
 * @file lifecycle_gate.hpp
 * @brief Shutdown handshake for a module whose event loop runs on a borrowed framework thread.
 *
 * @details The loop runs inside `ready()` on the framework thread that delivered global ready; `shutdown()`
 * runs on another framework thread, must make the loop return and wait for it, and must not wait if
 * `ready()` never entered the loop. Both orders are decided under one lock:
 * \ref everest::lib::util::LifecycleStateT holds the flags, \ref everest::lib::util::wait_for_loop_exit
 * waits. Reach the state only through an \ref everest::lib::util::monitor; every writer of a flag read by
 * \ref everest::lib::util::LifecycleStateT::loop_settled must `notify_all()` on it.
 */

#pragma once

#include <chrono>

namespace everest::lib::util {

/**
 * @brief Lifecycle flags shared between the threads of a module that runs its event loop in `ready()`.
 * @details Guard with an \ref everest::lib::util::monitor. `ready_entered` and `loop_exited` are two flags
 * because "not started yet" and "already finished" need opposite handling in `shutdown()`. Owner-specific
 * state goes in a derived struct.
 * @tparam WorkerT The object the event loop drives and `shutdown()` destroys; held as a pointer, may be incomplete.
 */
template <typename WorkerT> struct LifecycleStateT {
    /// `ready()` committed to running the event loop.
    bool ready_entered{false};
    /// The event loop in `ready()` has returned.
    bool loop_exited{false};
    /// `shutdown()` was called.
    bool shutting_down{false};
    /// Valid between construction of the worker and `shutdown()`.
    WorkerT* worker{nullptr};

    /**
     * @brief Whether `ready()` may run the event loop: not after a previous `ready()`, not after `shutdown()`.
     * @return True if the loop may be entered, false otherwise
     */
    bool may_enter_loop() const {
        return not shutting_down and not ready_entered;
    }

    /**
     * @brief Whether \ref wait_for_loop_exit has nothing to wait for: the loop finished or was never entered.
     * @return True if the loop is settled, false while it is still running
     */
    bool loop_settled() const {
        return loop_exited or not ready_entered;
    }

    /**
     * @brief The worker a handler may talk to; null once shutdown started.
     * @details Keep the monitor held across the whole use of the returned pointer.
     * @return The worker, or nullptr if it must not be used
     */
    WorkerT* live_worker() const {
        return shutting_down ? nullptr : worker;
    }
};

/**
 * @enum LoopExitResult
 * @brief Outcome of waiting for a module's event loop to exit.
 */
enum class LoopExitResult {
    /// The loop was never entered.
    NotRunning,
    /// The loop returned; what it touched may be destroyed.
    Stopped,
    /// The loop did not return in time; what it touches must not be destroyed.
    TimedOut,
};

/**
 * @brief Block until the event loop started in `ready()` has exited, or \p timeout elapsed.
 * @details Writers of the flags \ref LifecycleStateT::loop_settled checks must `notify_all()` on \p monitor.
 * The monitor is released while blocking.
 * @tparam LifecycleMonitor An \ref everest::lib::util::monitor over a \ref LifecycleStateT or a derived type
 * @param[inout] monitor The monitor guarding the lifecycle flags
 * @param[in] timeout How long to wait before giving up
 * @return See \ref LoopExitResult
 */
template <typename LifecycleMonitor>
LoopExitResult wait_for_loop_exit(LifecycleMonitor& monitor, std::chrono::milliseconds timeout) {
    auto lifecycle = monitor.handle();
    if (not lifecycle->ready_entered) {
        return LoopExitResult::NotRunning;
    }
    if (lifecycle.wait_for([&] { return lifecycle->loop_exited; }, timeout)) {
        return LoopExitResult::Stopped;
    }
    return LoopExitResult::TimedOut;
}

} // namespace everest::lib::util
