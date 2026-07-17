// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#ifndef MAIN_LIFECYCLE_GATE_HPP
#define MAIN_LIFECYCLE_GATE_HPP

#include <chrono>
#include <string>

namespace module {
namespace main {

// Lifecycle flags shared between the framework threads (init/ready/command handlers) and the
// SLAC worker. Always accessed through an everest::lib::util::monitor<>.
template <typename ControllerT> struct LifecycleStateT {
    bool ready_requested{false};
    bool shutting_down{false};
    bool slac_io_ready{false};
    bool communication_fault_raised{false};
    std::string communication_fault_message;
    ControllerT* fsm_ctrl{nullptr};

    // The SLAC state machine must only run once BOTH the PLC I/O is up AND the framework has
    // announced global ready; it is started by whichever of the two events happens second.
    // Starting on I/O ready alone would put SET_KEY traffic on the wire before global ready;
    // starting on global ready alone would run the FSM against a socket that is not up.
    bool fsm_start_allowed() const {
        return slac_io_ready && ready_requested && !shutting_down;
    }

    // init() blocks until the PLC I/O bring-up has settled one way or the other.
    bool io_bring_up_settled() const {
        return slac_io_ready || communication_fault_raised || shutting_down;
    }
};

enum class IoBringUpResult {
    Ready,    // PLC I/O is up; interface commands will be accepted
    Fault,    // bring-up failed; a CommunicationFault has been raised
    ShutDown, // the module is shutting down
    TimedOut, // neither the ready nor the error callback arrived in time
};

// Block until the PLC I/O bring-up settled (ready, fault or shutdown) or `timeout` elapsed.
// `monitor` is an everest::lib::util::monitor<LifecycleStateT<...>>; every writer of the flags
// checked by io_bring_up_settled() must notify_all() on it.
template <typename LifecycleMonitor>
IoBringUpResult wait_for_io_bring_up(LifecycleMonitor& monitor, std::chrono::milliseconds timeout) {
    auto lifecycle = monitor.handle();
    const bool settled = lifecycle.wait_for([&] { return lifecycle->io_bring_up_settled(); }, timeout);
    if (!settled) {
        return IoBringUpResult::TimedOut;
    }
    if (lifecycle->shutting_down) {
        return IoBringUpResult::ShutDown;
    }
    if (lifecycle->slac_io_ready) {
        return IoBringUpResult::Ready;
    }
    return IoBringUpResult::Fault;
}

} // namespace main
} // namespace module

#endif // MAIN_LIFECYCLE_GATE_HPP
