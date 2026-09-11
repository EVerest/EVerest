// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#ifndef MAIN_EV_SLAC_IMPL_HPP
#define MAIN_EV_SLAC_IMPL_HPP

//
// AUTO GENERATED - MARKED REGIONS WILL BE KEPT
// template version 4
//

#include <generated/interfaces/ev_slac/Implementation.hpp>

#include "../EvSlacNeo.hpp"

// ev@75ac1216-19eb-4182-a85c-820f1fc2c091:v1
// insert your custom include headers here
#include <atomic>
#include <functional>
#include <memory>
#include <string>

#include <everest/io/event/event_fd.hpp>
#include <everest/io/event/fd_event_handler.hpp>
#include <everest/slac/fsm/ev/context.hpp>
#include <everest/slac/slac_event.hpp>
#include <everest/util/async/lifecycle_gate.hpp>
#include <everest/util/async/monitor.hpp>

#include "fsm_controller.hpp"

namespace slac_fsm = everest::lib::slac::fsm;
// ev@75ac1216-19eb-4182-a85c-820f1fc2c091:v1

namespace module {
namespace main {

struct Conf {
    std::string device;
    int set_key_timeout_ms;
    int parm_req_attempts;
};

class ev_slacImpl : public ev_slacImplBase {
public:
    ev_slacImpl() = delete;
    ev_slacImpl(Everest::ModuleAdapter* ev, const Everest::PtrContainer<EvSlacNeo>& mod, Conf& config) :
        ev_slacImplBase(ev, "main"), mod(mod), config(config){};

    // ev@8ea32d28-373f-4c90-ae5e-b4fcc74e2a61:v1
    // insert your public definitions here
    // The framework calls shutdown() during orderly teardown; the destructor calls it again as an idempotent fallback.
    ~ev_slacImpl() override;
    // ev@8ea32d28-373f-4c90-ae5e-b4fcc74e2a61:v1

protected:
    // command handler functions (virtual)
    virtual void handle_reset() override;
    virtual bool handle_trigger_matching() override;

    // ev@d2d1847a-7b88-41dd-ad07-92785f06f5c4:v1
    // insert your protected definitions here
    // ev@d2d1847a-7b88-41dd-ad07-92785f06f5c4:v1

private:
    const Everest::PtrContainer<EvSlacNeo>& mod;
    const Conf& config;

    virtual void init() override;
    virtual void ready() override;
    void shutdown() override;

    // ev@3370e4dd-95f4-47a9-aaec-ea76f34a66c9:v1
    // insert your private definitions here
    bool initialize_slac_io();
    void configure_callbacks();
    void configure_fsm_context();
    bool create_fsm_controller();
    void configure_slac_io_callbacks();
    /// Registers everything on event_handler and runs the loop on the caller's thread until
    /// shutdown() (or a fatal error) stops it.
    void run_event_loop();
    void handle_slac_io_ready();
    void handle_slac_io_error(bool on_error, const std::string& detail);
    /// Hand a command to the FSM controller with the lifecycle monitor held for the whole call, or
    /// drop it (with a warning naming \p command) if the controller or the PLC I/O is not usable.
    void post_command(char const* command, std::function<void(FSMController&)> const& post);
    void raise_communication_fault(const std::string& message);
    void clear_communication_fault();
    /// Fatal for the event loop: makes it return, tears the FSM down and (unless shutting down)
    /// raises a CommunicationFault. Commands are dropped from then on.
    void abort_event_loop(const std::string& reason);

    /// Loop-exit flag for fd_event_handler::run.
    std::atomic<bool> online{true};
    /// Wakes the loop out of poll() so it can observe `online`.
    everest::lib::io::event::event_fd exit_event;
    /// Runs on the framework's ready thread inside ready(); see run_event_loop().
    everest::lib::io::event::fd_event_handler event_handler;

    /// The generic loop handshake plus this module's own bookkeeping.
    struct LifecycleState : everest::lib::util::LifecycleStateT<FSMController> {
        bool slac_io_ready{false};
        bool slac_fsm_started{false};
        bool communication_fault_raised{false};
        std::string communication_fault_message;
    };
    everest::lib::util::monitor<LifecycleState> lifecycle_state;
    slac_fsm::ev::ContextCallbacks callbacks;
    std::unique_ptr<slac_fsm::ev::Context> fsm_ctx;
    std::unique_ptr<everest::lib::slac::SlacEvent> slac_io;
    std::unique_ptr<FSMController> fsm_ctrl;
    // ev@3370e4dd-95f4-47a9-aaec-ea76f34a66c9:v1
};

// ev@3d7da0ad-02c2-493d-9920-0bbbd56b9876:v1
// insert other definitions here
// ev@3d7da0ad-02c2-493d-9920-0bbbd56b9876:v1

} // namespace main
} // namespace module

#endif // MAIN_EV_SLAC_IMPL_HPP
