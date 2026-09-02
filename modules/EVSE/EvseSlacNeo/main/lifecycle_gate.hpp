// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#ifndef MAIN_LIFECYCLE_GATE_HPP
#define MAIN_LIFECYCLE_GATE_HPP

#include <string>

#include <everest/util/async/lifecycle_gate.hpp>

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

} // namespace main
} // namespace module

#endif // MAIN_LIFECYCLE_GATE_HPP
