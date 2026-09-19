// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#ifndef MAIN_SLAC_IMPL_HPP
#define MAIN_SLAC_IMPL_HPP

//
// AUTO GENERATED - MARKED REGIONS WILL BE KEPT
// template version 4
//

#include <generated/interfaces/slac/Implementation.hpp>

#include "../McsDataLink.hpp"

// ev@75ac1216-19eb-4182-a85c-820f1fc2c091:v1
// insert your custom include headers here
#include <atomic>
#include <functional>
#include <memory>
#include <string>

#include <everest/io/event/event_fd.hpp>
#include <everest/util/async/lifecycle_gate.hpp>
#include <everest/util/async/monitor.hpp>

#include "datalink_controller.hpp"
// ev@75ac1216-19eb-4182-a85c-820f1fc2c091:v1

namespace module {
namespace main {

struct Conf {
    std::string device;
    int link_detect_timeout_ms;
    int sync_repetition_ms;
    int conn_retry_max;
    int retry_wait_ms;
    bool neighbor_liveness;
    int liveness_grace_ms;
    bool publish_ev_mac;
};

class slacImpl : public slacImplBase {
public:
    slacImpl() = delete;
    slacImpl(Everest::ModuleAdapter* ev, const Everest::PtrContainer<McsDataLink>& mod, Conf& config) :
        slacImplBase(ev, "main"), mod(mod), config(config){};

    // ev@8ea32d28-373f-4c90-ae5e-b4fcc74e2a61:v1
    // insert your public definitions here
    /// Calls the idempotent shutdown() in case the framework did not.
    ~slacImpl() override;
    // ev@8ea32d28-373f-4c90-ae5e-b4fcc74e2a61:v1

protected:
    // command handler functions (virtual)
    virtual void handle_reset(bool& enable) override;
    virtual void handle_enter_bcd() override;
    virtual void handle_leave_bcd() override;
    virtual void handle_count_bc(int& count) override;
    virtual void handle_dlink_terminate() override;
    virtual void handle_dlink_error() override;
    virtual void handle_dlink_pause() override;

    // ev@d2d1847a-7b88-41dd-ad07-92785f06f5c4:v1
    // insert your protected definitions here
    // ev@d2d1847a-7b88-41dd-ad07-92785f06f5c4:v1

private:
    const Everest::PtrContainer<McsDataLink>& mod;
    const Conf& config;

    virtual void init() override;
    virtual void ready() override;
    void shutdown() override;

    // ev@3370e4dd-95f4-47a9-aaec-ea76f34a66c9:v1
    datalink_controller::config controller_config() const;
    datalink_controller::callbacks controller_callbacks();
    /// Runs the io event loop on the caller's thread until shutdown() stops it.
    void run_event_loop();
    /// Post to the controller with the lifecycle monitor held; drops with a warning if it is not usable.
    void post_command(char const* command, std::function<void(datalink_controller&)> const& post);
    void raise_communication_fault(std::string const& message);
    void clear_communication_fault();

    /// Loop-exit flag for fd_event_handler::run.
    std::atomic<bool> online{true};
    /// Wakes the loop out of poll() so it can observe `online`.
    everest::lib::io::event::event_fd exit_event;

    /// Lifecycle gate plus fault state; raise/clear run on both the event loop and framework threads.
    struct LifecycleState : everest::lib::util::LifecycleStateT<datalink_controller> {
        /// Whether generic/CommunicationFault is currently raised.
        bool communication_fault_raised{false};
        /// Message it was raised with; a changed reason replaces the fault.
        std::string communication_fault_message;
    };

    everest::lib::util::monitor<LifecycleState> lifecycle_state;

    std::unique_ptr<datalink_controller> controller;
    // ev@3370e4dd-95f4-47a9-aaec-ea76f34a66c9:v1
};

// ev@3d7da0ad-02c2-493d-9920-0bbbd56b9876:v1
// insert other definitions here
// ev@3d7da0ad-02c2-493d-9920-0bbbd56b9876:v1

} // namespace main
} // namespace module

#endif // MAIN_SLAC_IMPL_HPP
