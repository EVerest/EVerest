// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

/**
 * @file lifecycle_gate.hpp
 * @brief Shutdown handshake for a module whose event loop runs on a borrowed framework thread.
 *
 * @details Shape of the problem this solves. A module that runs its event loop inside its own
 * `ready()` - rather than starting a worker thread - borrows the thread the framework spawned to
 * deliver global ready, and that thread is joined last during teardown. `shutdown()` then arrives
 * on a *different* framework thread and has to make the loop return and wait for it, because the
 * framework destroys the module afterwards and everything the loop touches lives in it.
 *
 * Waiting is the part that needs care, because two orders race and both must be decided under one
 * lock: if `ready()` got there first it will run the loop and `shutdown()` has to wait for it, and
 * if `shutdown()` got there first `ready()` must never enter the loop and `shutdown()` must not
 * wait for an exit that will never happen. \ref everest::lib::util::LifecycleStateT holds the two
 * flags that make that decidable and \ref everest::lib::util::wait_for_loop_exit does the waiting.
 *
 * The state is always reached through an \ref everest::lib::util::monitor, never a bare mutex, and
 * every writer of a flag that \ref everest::lib::util::LifecycleStateT::loop_settled looks at must
 * `notify_all()` on that monitor.
 */

#pragma once

#include <chrono>

namespace everest::lib::util {

/**
 * @brief Lifecycle flags shared between the threads of a module that runs its event loop in
 * `ready()`.
 *
 * @details Guard an instance with an \ref everest::lib::util::monitor and reach it only through that. The flags are
 * deliberately two (`ready_entered` and `loop_exited`) rather than one "running" flag: a single flag
 * cannot distinguish "the loop has not started yet" from "the loop has already finished", and
 * `shutdown()` has to treat those opposite ways.
 *
 * Anything the owner needs beyond these flags - a fault it is reporting, a retry counter - belongs
 * in a struct of its own that derives from this one, so the generic part stays generic.
 *
 * @tparam WorkerT The type owned by the module and reached from its command or request handlers -
 * whatever object the event loop drives and `shutdown()` destroys. Only ever held as a pointer
 * here, so an incomplete type is fine.
 */
template <typename WorkerT> struct LifecycleStateT {
    /// `ready()` committed to running the event loop.
    bool ready_entered{false};
    /// The event loop in `ready()` has returned.
    bool loop_exited{false};
    /// `shutdown()` was called.
    bool shutting_down{false};
    /// Valid between construction of the worker and `shutdown()`; handlers reach the loop
    /// through it.
    WorkerT* worker{nullptr};

    /**
     * @brief Whether `ready()` may run the event loop.
     * @details False for every subsequent `ready()`, and false if `shutdown()` won the race.
     * @return True if the loop may be entered, false otherwise
     */
    bool may_enter_loop() const {
        return not shutting_down and not ready_entered;
    }

    /**
     * @brief Whether there is nothing left for \ref wait_for_loop_exit to wait for.
     * @details True when the loop has finished, and also when it was never entered.
     * @return True if the loop is settled, false while it is still running
     */
    bool loop_settled() const {
        return loop_exited or not ready_entered;
    }

    /**
     * @brief The worker a handler may talk to.
     * @details Null once shutdown started, so handlers drop their work instead of reaching into an
     * object that is about to be destroyed. Callers must keep the monitor held across the whole use
     * of the returned pointer, not just across this call.
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
    /// The loop was never entered - `ready()` had not run yet, or had already been told to stop.
    NotRunning,
    /// The loop returned; everything it touched is safe to destroy.
    Stopped,
    /// It did not return in time. Destroying what it touches would be a use-after-free, so the
    /// caller must leave those objects alone.
    TimedOut,
};

/**
 * @brief Block until the event loop started in `ready()` has exited, or \p timeout elapsed.
 * @details Every writer of the flags \ref LifecycleStateT::loop_settled checks must `notify_all()`
 * on \p monitor. The wait releases the monitor while it blocks, so a handler holding it briefly
 * does not deadlock against this.
 * @tparam LifecycleMonitor An \ref everest::lib::util::monitor over a \ref LifecycleStateT (or over a
 * type derived from it)
 * @param[inout] monitor The monitor guarding the lifecycle flags
 * @param[in] timeout How long to wait before giving up
 * @return What happened; see \ref LoopExitResult
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
