// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#ifndef MAIN_LIFECYCLE_GATE_HPP
#define MAIN_LIFECYCLE_GATE_HPP

#include <string>

#include <everest/util/async/lifecycle_gate.hpp>
#include <everest/util/async/monitor.hpp>

namespace module {
namespace main {

// Lifecycle flags shared between the framework threads (init/ready/command handlers) and the
// event loop, which runs on the framework's ready thread inside ready(). The generic base
// carries the loop handshake (ready_entered / loop_exited / shutting_down / worker); this adds
// the SLAC-specific bring-up bookkeeping. Always accessed through an
// everest::lib::util::monitor<>.
template <typename ControllerT> struct LifecycleStateT : everest::lib::util::LifecycleStateT<ControllerT> {
    bool slac_io_ready{false};
    bool communication_fault_raised{false};
    std::string communication_fault_message;
    // init() is driving the event loop on the init thread (bring-up phase). shutdown() must not
    // destroy the loop's objects while this is set; every writer must notify_all() on the monitor.
    bool bring_up_running{false};

    // The SLAC state machine must only run once BOTH the PLC I/O is up AND the framework has
    // announced global ready; it is started by whichever of the two events happens second.
    // Starting on I/O ready alone would put SET_KEY traffic on the wire before global ready;
    // starting on global ready alone would run the FSM against a socket that is not up.
    bool fsm_start_allowed() const {
        return slac_io_ready && this->ready_entered && !this->shutting_down;
    }

    // init() runs the event loop until the PLC I/O bring-up has settled one way or the other.
    bool io_bring_up_settled() const {
        return slac_io_ready || communication_fault_raised || this->shutting_down;
    }

    // The controller a command or frame may be handed to right now: live (set, not shutting down)
    // and with the PLC I/O up. Null means drop. The one condition every dispatch site uses.
    ControllerT* dispatch_target() const {
        return slac_io_ready ? this->live_worker() : nullptr;
    }
};

enum class IoBringUpResult {
    Ready,    // PLC I/O is up; interface commands will be accepted
    Fault,    // bring-up failed; a CommunicationFault has been raised
    ShutDown, // the module is shutting down
    TimedOut, // neither the ready nor the error callback arrived in time
};

// Classify the outcome of the bring-up phase from the lifecycle flags once the bring-up loop
// in init() has returned. Shutdown wins over everything else; an unsettled state means the
// bring-up timer expired first.
template <typename ControllerT> IoBringUpResult bring_up_result(LifecycleStateT<ControllerT> const& state) {
    if (state.shutting_down) {
        return IoBringUpResult::ShutDown;
    }
    if (state.slac_io_ready) {
        return IoBringUpResult::Ready;
    }
    if (state.communication_fault_raised) {
        return IoBringUpResult::Fault;
    }
    return IoBringUpResult::TimedOut;
}

// Run \p work on the live controller with the lifecycle monitor held for the whole call.
//
// Holding it is what makes the pointer safe: shutdown() clears the worker and destroys the
// controller under this same monitor, so it cannot get in between the lookup and the call. That
// requires one invariant of everything reachable from \p work, the state machine included: nothing
// in there takes the lifecycle monitor. send_raw_slac reads only the I/O object, the publish and
// log callbacks go to the framework, and the count_bc path is an atomic store.
//
// Returns false if there is no live controller or the PLC I/O is not ready; \p work did not run.
template <typename StateT, typename MTX, typename WorkT>
bool dispatch_to_controller(everest::lib::util::monitor<StateT, MTX>& lifecycle, WorkT&& work) {
    auto guard = lifecycle.handle();
    auto* const target = guard->dispatch_target();
    if (target == nullptr) {
        return false;
    }
    work(*target);
    return true;
}

} // namespace main
} // namespace module

#endif // MAIN_LIFECYCLE_GATE_HPP
